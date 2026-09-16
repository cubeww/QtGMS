#include "shadercompiler.h"
#include "datawriter.h"
#include <GLSLANG/ShaderLang.h>
#include <GLES2/gl2.h>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

class ShaderTranslator
{
public:
    ShaderTranslator(unsigned int stage, const QString &name, const QString &source)
    {
        ShBuiltInResources limits;
        ShInitBuiltInResources(&limits);
        limits.MaxVertexAttribs = 16;
        limits.MaxVertexUniformVectors = 256;
        limits.MaxFragmentUniformVectors = 224;
        limits.MaxVaryingVectors = 10;
        limits.MaxTextureImageUnits = 16;
        limits.MaxCombinedTextureImageUnits = 20;
        limits.MaxVertexTextureImageUnits = 4;
        limits.FragmentPrecisionHigh = 1;
        limits.OES_standard_derivatives = 1;
        limits.EXT_shader_texture_lod = 1;
        limits.EXT_frag_depth = 1;
        limits.EXT_draw_buffers = 1;
        limits.MaxDrawBuffers = 4;
        m_handle = ShConstructCompiler(stage, SH_GLES2_SPEC, SH_HLSL9_OUTPUT, &limits);
        if (!m_handle)
            throw CompileError(name + ": cannot initialize the shader translator");
        const QByteArray utf8 = source.toUtf8();
        const char *strings[] = { utf8.constData() };
        if (!ShCompile(m_handle, strings, 1,
                SH_OBJECT_CODE | SH_VARIABLES | SH_LIMIT_EXPRESSION_COMPLEXITY | SH_LIMIT_CALL_STACK_DEPTH)) {
            const QString error = QString::fromStdString(ShGetInfoLog(m_handle));
            ShDestruct(m_handle);
            m_handle = nullptr;
            throw CompileError(name + ": " + error);
        }
    }
    ~ShaderTranslator() { ShDestruct(m_handle); }
    ShHandle handle() const { return m_handle; }
    QString output() const { return QString::fromStdString(ShGetObjectCode(m_handle)); }

private:
    ShHandle m_handle = nullptr;
};

struct ShaderLinkRow
{
    QString variable;
    int row = 0, column = 0, width = 0;
};

static int shaderVectorWidth(unsigned int type)
{
    switch (type) {
    case GL_FLOAT:
        return 1;
    case GL_FLOAT_VEC2:
    case GL_FLOAT_MAT2:
        return 2;
    case GL_FLOAT_VEC3:
    case GL_FLOAT_MAT3:
        return 3;
    case GL_FLOAT_VEC4:
    case GL_FLOAT_MAT4:
        return 4;
    }
    throw CompileError(QStringLiteral("Unsupported GLSL ES interface type"));
}
static QString shaderType(unsigned int type)
{
    int width = shaderVectorWidth(type);
    QString result = width == 1 ? QStringLiteral("float") : QStringLiteral("float%1").arg(width);
    if (type == GL_FLOAT_MAT2 || type == GL_FLOAT_MAT3 || type == GL_FLOAT_MAT4)
        result += QStringLiteral("x%1").arg(width);
    return result;
}
static QString shaderName(const std::string &name)
{
    return QStringLiteral("_") + QString::fromStdString(name);
}

CompiledShader compileShader(const QString &name, const QString &vertex, const QString &fragment)
{
    // ANGLE's process initialization is global, and its parser allocation pool
    // is thread-local. Keep both stages and their link operation on one thread.
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    if (!ShInitialize())
        throw CompileError(QStringLiteral("Cannot initialize ANGLE"));
    struct Finalize
    {
        ~Finalize() { ShFinalize(); }
    } finalize;
    ShaderTranslator vs(GL_VERTEX_SHADER, name + " (vertex)", vertex),
        ps(GL_FRAGMENT_SHADER, name + " (fragment)", fragment);
    CompiledShader result;
    result.vertex = vs.output();
    result.fragment = ps.output();
    QMap<QString, sh::Varying> vertexVaryings;
    for (const auto &value : *ShGetVaryings(vs.handle()))
        vertexVaryings.insert(QString::fromStdString(value.name), value);
    QVector<ShaderLinkRow> links;
    int occupied[10] = { 0 };
    int rowCount = 0;
    for (const auto &value : *ShGetVaryings(ps.handle())) {
        if (!value.staticUse || value.name.compare(0, 3, "gl_") == 0)
            continue;
        const QString key = QString::fromStdString(value.name);
        const auto other = vertexVaryings.constFind(key);
        if (other == vertexVaryings.cend() || !other->staticUse || other->type != value.type
            || other->arraySize != value.arraySize)
            throw CompileError(name + ": shader stage interface does not match: " + key);
        const int width = shaderVectorWidth(value.type);
        const bool matrix = value.type == GL_FLOAT_MAT2 || value.type == GL_FLOAT_MAT3 || value.type == GL_FLOAT_MAT4;
        for (unsigned int a = 0; a < qMax(1u, value.arraySize); ++a)
            for (int m = 0; m < (matrix ? width : 1); ++m) {
                QString variable = shaderName(value.name);
                if (value.arraySize)
                    variable += QStringLiteral("[%1]").arg(a);
                if (matrix)
                    variable += QStringLiteral("[%1]").arg(m);
                bool placed = false;
                for (int row = 0; row < 10 && !placed; ++row)
                    for (int column = 0; column <= 4 - width && !placed; ++column) {
                        const int mask = ((1 << width) - 1) << column;
                        if (occupied[row] & mask)
                            continue;
                        occupied[row] |= mask;
                        ShaderLinkRow link;
                        link.variable = variable;
                        link.row = row;
                        link.column = column;
                        link.width = width;
                        links.append(link);
                        rowCount = qMax(rowCount, row + 1);
                        placed = true;
                    }
                if (!placed)
                    throw CompileError(name + ": shader exceeds ten varying registers");
            }
    }
    QString input = "struct VS_INPUT {\n", attributes;
    int textureAttribute = 1, colorAttribute = 1;
    for (const auto &value : *ShGetAttributes(vs.handle())) {
        result.attributes.append(QString::fromStdString(value.name));
        if (!value.staticUse)
            continue;
        QString semantic;
        if (value.name == "in_Position")
            semantic = "POSITION0";
        else if (value.name == "in_Normal")
            semantic = "NORMAL0";
        else if (value.name == "in_Colour" || value.name == "in_Color")
            semantic = "COLOR0";
        else if (value.name == "in_TextureCoord")
            semantic = "TEXCOORD0";
        else if (value.name.find("Colour") != std::string::npos || value.name.find("Color") != std::string::npos)
            semantic = QStringLiteral("COLOR%1").arg(colorAttribute++);
        else
            semantic = QStringLiteral("TEXCOORD%1").arg(textureAttribute++);
        if (value.arraySize)
            throw CompileError(name + ": array vertex attributes are not supported");
        const QString variable = shaderName(value.name);
        input += shaderType(value.type) + " " + variable + " : " + semantic + ";\n";
        attributes += variable + " = input." + variable + ";\n";
    }
    input += "};\n";
    const bool fragCoord = result.fragment.contains("static float4 gl_FragCoord"),
               pointSize = result.vertex.contains("static float gl_PointSize"),
               frontFacing = result.fragment.contains("static bool gl_FrontFacing"),
               depth = result.fragment.contains("static float gl_Depth");
    if (result.fragment.contains("static float2 gl_PointCoord"))
        throw CompileError(name + ": point-sprite coordinates require a Runner-specific shader path");
    if (fragCoord && rowCount == 10)
        throw CompileError(name + ": gl_FragCoord requires one more varying register");
    QString varyingFields;
    for (int row = 0; row < rowCount; ++row)
        varyingFields += QStringLiteral("float4 v%1 : TEXCOORD%1;\n").arg(row);
    if (fragCoord)
        varyingFields += QStringLiteral("float4 clipPosition : TEXCOORD%1;\n").arg(rowCount);
    result.vertex += "\n" + input + "struct VS_OUTPUT { float4 position : POSITION;\n" + varyingFields
        + (pointSize ? "float pointSize : PSIZE;\n" : "") + "};\nVS_OUTPUT main(VS_INPUT input) {\n" + attributes
        + "gl_main();\nVS_OUTPUT output=(VS_OUTPUT)0;\noutput.position=gl_Position;\n";
    result.fragment += "\nstruct PS_INPUT {\n" + varyingFields + (fragCoord ? "float2 pixelPosition : VPOS;\n" : "")
        + (frontFacing ? "float face : VFACE;\n" : "") + "};\nstruct PS_OUTPUT {\n";
    const QRegularExpression colorCount(QStringLiteral("static float4 gl_Color\\[(\\d+)\\]"));
    const int colors = qMax(1, colorCount.match(result.fragment).captured(1).toInt());
    for (int color = 0; color < colors; ++color)
        result.fragment += QStringLiteral("float4 color%1 : COLOR%1;\n").arg(color);
    if (depth)
        result.fragment += "float depth : DEPTH;\n";
    result.fragment += "};\nPS_OUTPUT main(PS_INPUT input) {\n";
    for (const auto &link : links) {
        const QString slot
            = QStringLiteral("v%1.%2").arg(link.row).arg(QStringLiteral("xyzw").mid(link.column, link.width));
        result.vertex += "output." + slot + " = " + link.variable + ";\n";
        result.fragment += link.variable + " = input." + slot + ";\n";
    }
    if (pointSize)
        result.vertex += "output.pointSize=gl_PointSize;\n";
    if (fragCoord) {
        result.vertex += "output.clipPosition=gl_Position;\n";
        result.fragment += "gl_FragCoord=float4(input.pixelPosition+float2(0.5,0.5),input.clipPosition.z/"
                           "input.clipPosition.w,1.0/input.clipPosition.w);\n";
    }
    if (frontFacing)
        result.fragment += "gl_FrontFacing=input.face>=0;\n";
    result.vertex += "return output;\n}\n";
    result.fragment += "gl_main();\nPS_OUTPUT output;\n";
    for (int color = 0; color < colors; ++color)
        result.fragment += QStringLiteral("output.color%1=gl_Color[%1];\n").arg(color);
    if (depth)
        result.fragment += "output.depth=gl_Depth;\n";
    result.fragment += "return output;\n}\n";
    // The Runner binds its base texture to sampler zero. Swap the two
    // declarations when another uniform sorts before gm_BaseTexture.
    const QRegularExpression baseSampler(QStringLiteral("(_gm_BaseTexture\\s*:\\s*register\\(s)(\\d+)(\\))"));
    const auto base = baseSampler.match(result.fragment);
    if (base.hasMatch() && base.captured(2) != "0") {
        const QString old = base.captured(2);
        result.fragment.replace("register(s0)", "register(s" + old + ")");
        const auto relocated = baseSampler.match(result.fragment);
        result.fragment.replace(relocated.capturedStart(2), relocated.capturedLength(2), QStringLiteral("0"));
    }
    return result;
}
