#include "fontdocument.h"
#include "fontatlas.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>

bool FontState::operator==(const FontState &other) const
{
    return family == other.family && size == other.size && bold == other.bold && italic == other.italic
        && highQuality == other.highQuality && antiAlias == other.antiAlias && includeTTF == other.includeTTF
        && textureGroup == other.textureGroup && ranges == other.ranges;
}
class FontChangeCommand : public QUndoCommand
{
public:
    FontChangeCommand(FontDocument *document, const FontState &state, const QString &description)
        : QUndoCommand(description), m_document(document), m_before(document->state()), m_after(state) {}
    void undo() override { m_document->m_state = m_before; emit m_document->changed(); }
    void redo() override { m_document->m_state = m_after; emit m_document->changed(); }
private:
    FontDocument *m_document;
    FontState m_before, m_after;
};
static QString fontImagePath(const QString &path)
{ QString result = path; result.chop(QStringLiteral(".font.gmx").size()); return result + QStringLiteral(".png"); }
static void setFontText(QDomDocument &xml, QDomElement parent, const QString &tag, const QString &value)
{
    QDomElement element = parent.firstChildElement(tag);
    if (element.isNull()) { element = xml.createElement(tag); parent.appendChild(element); }
    while (!element.firstChild().isNull()) element.removeChild(element.firstChild());
    element.appendChild(xml.createTextNode(value));
}
static QDomElement fontContainer(QDomDocument &xml, const QString &name)
{
    QDomElement root = xml.documentElement(); QDomElement element = root.firstChildElement(name);
    if (element.isNull()) { element = xml.createElement(name); root.appendChild(element); }
    return element;
}
static void writeFontState(QDomDocument &xml, const FontState &state, int configurationIndex)
{
    const QDomElement root = xml.documentElement();
    setFontText(xml, root, QStringLiteral("name"), state.family);
    setFontText(xml, root, QStringLiteral("size"), QString::number(state.size));
    setFontText(xml, root, QStringLiteral("bold"), state.bold ? QStringLiteral("-1") : QStringLiteral("0"));
    setFontText(xml, root, QStringLiteral("italic"), state.italic ? QStringLiteral("-1") : QStringLiteral("0"));
    setFontText(xml, root, QStringLiteral("renderhq"), state.highQuality ? QStringLiteral("-1") : QStringLiteral("0"));
    setFontText(xml, root, QStringLiteral("aa"), QString::number(state.antiAlias));
    setFontText(xml, root, QStringLiteral("includeTTF"), state.includeTTF ? QStringLiteral("-1") : QStringLiteral("0"));
    QDomElement groups = fontContainer(xml, QStringLiteral("texgroups"));
    const QString initial = groups.firstChildElement(QStringLiteral("texgroup0")).isNull() ? QStringLiteral("0")
        : groups.firstChildElement(QStringLiteral("texgroup0")).text();
    for (int i = 0; i <= configurationIndex; ++i) {
        const QString tag = QStringLiteral("texgroup%1").arg(i);
        if (i == configurationIndex || groups.firstChildElement(tag).isNull())
            setFontText(xml, groups, tag, i == configurationIndex ? QString::number(state.textureGroup) : initial);
    }
    QDomElement ranges = fontContainer(xml, QStringLiteral("ranges"));
    while (!ranges.firstChild().isNull()) ranges.removeChild(ranges.firstChild());
    for (int i = 0; i < state.ranges.size(); ++i)
        setFontText(xml, ranges, QStringLiteral("range%1").arg(i), QStringLiteral("%1,%2").arg(state.ranges.at(i).first).arg(state.ranges.at(i).last));
}
static void writeFontGlyphs(QDomDocument &xml, const FontAtlasData &atlas, const QString &imageName)
{
    QDomElement glyphs = fontContainer(xml, QStringLiteral("glyphs"));
    while (!glyphs.firstChild().isNull()) glyphs.removeChild(glyphs.firstChild());
    for (const FontGlyph &glyph : atlas.glyphs) {
        QDomElement entry = xml.createElement(QStringLiteral("glyph"));
        entry.setAttribute(QStringLiteral("character"), glyph.character);
        entry.setAttribute(QStringLiteral("x"), glyph.rectangle.x()); entry.setAttribute(QStringLiteral("y"), glyph.rectangle.y());
        entry.setAttribute(QStringLiteral("w"), glyph.rectangle.width()); entry.setAttribute(QStringLiteral("h"), glyph.rectangle.height());
        entry.setAttribute(QStringLiteral("shift"), glyph.shift); entry.setAttribute(QStringLiteral("offset"), glyph.offset);
        glyphs.appendChild(entry);
    }
    // Rendering uses individual unkerned glyph advances. Old pairs belong to the old font.
    QDomElement pairs = fontContainer(xml, QStringLiteral("kerningPairs"));
    while (!pairs.firstChild().isNull()) pairs.removeChild(pairs.firstChild());
    setFontText(xml, xml.documentElement(), QStringLiteral("image"), imageName);
}
static bool encodeFontAtlas(const FontAtlasData &atlas, QByteArray &bytes, QString &error)
{
    QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    if (!atlas.image.save(&buffer, "PNG")) { error = QObject::tr("Cannot encode the font texture."); return false; }
    return true;
}
static bool createFontFile(const QString &path, const QByteArray &bytes, QString &error)
{
    QTemporaryFile file(QFileInfo(path).absoluteDir().filePath(QStringLiteral(".qtgms-font-XXXXXX")));
    if (!file.open() || file.write(bytes) != bytes.size() || !file.flush()) { error = file.errorString(); return false; }
    file.close(); if (!file.rename(path)) { error = file.errorString(); return false; }
    file.setAutoRemove(false); return true;
}
static bool writeFontFile(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; }
    return true;
}
FontDocument::FontDocument(QObject *parent) : QObject(parent), m_undoStack(this) { m_undoStack.setUndoLimit(100); }
bool FontDocument::createEmpty(const QString &filePath, QString &error)
{
    FontState state; FontAtlasData atlas; if (!FontAtlas::build(state, atlas, error)) return false;
    QDomDocument xml; xml.appendChild(xml.createElement(QStringLiteral("font")));
    setFontText(xml, xml.documentElement(), QStringLiteral("charset"), QStringLiteral("1"));
    setFontText(xml, xml.documentElement(), QStringLiteral("TTFName"), QString());
    writeFontState(xml, state, 0); writeFontGlyphs(xml, atlas, QFileInfo(fontImagePath(filePath)).fileName());
    QByteArray png; if (!encodeFontAtlas(atlas, png, error)) return false;
    if (!createFontFile(fontImagePath(filePath), png, error)) return false;
    if (!createFontFile(filePath, xml.toByteArray(2), error)) {
        if (!QFile::remove(fontImagePath(filePath))) error += QObject::tr("\nCannot remove the new font texture: %1").arg(fontImagePath(filePath));
        return false;
    }
    return true;
}
static int fontValue(QDomElement parent, const QString &tag, int initial)
{ const QDomElement element = parent.firstChildElement(tag); return element.isNull() ? initial : element.text().toInt(); }
bool FontDocument::load(const QString &filePath, int configurationIndex, QString &error)
{
    error.clear(); QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    const QByteArray bytes = file.readAll(); QDomDocument xml;
    if (!xml.setContent(bytes, false, &error)) return false;
    return loadFromXml(filePath, xml, bytes, configurationIndex, error);
}

bool FontDocument::loadFromXml(const QString &filePath, const QDomDocument &xml, const QByteArray &bytes,
                               int configurationIndex, QString &error)
{
    error.clear();
    const QDomElement root = xml.documentElement();
    if (root.tagName() != QStringLiteral("font")) { error = tr("Expected a font resource."); return false; }
    FontState state; state.family = root.firstChildElement(QStringLiteral("name")).text();
    state.size = fontValue(root, QStringLiteral("size"), 12); state.bold = fontValue(root, QStringLiteral("bold"), 0) != 0;
    state.italic = fontValue(root, QStringLiteral("italic"), 0) != 0; state.highQuality = fontValue(root, QStringLiteral("renderhq"), -1) != 0;
    state.antiAlias = fontValue(root, QStringLiteral("aa"), 3); state.includeTTF = fontValue(root, QStringLiteral("includeTTF"), 0) != 0;
    if (state.family.isEmpty() || state.size < 1 || state.size > 499 || state.antiAlias < 0 || state.antiAlias > 3) {
        error = tr("Invalid font family, size or anti-aliasing setting."); return false;
    }
    const int config = qMax(0, configurationIndex); const QDomElement groups = root.firstChildElement(QStringLiteral("texgroups"));
    state.textureGroup = fontValue(groups, QStringLiteral("texgroup%1").arg(config), fontValue(groups, QStringLiteral("texgroup0"), 0));
    state.ranges.clear();
    const QDomElement ranges = root.firstChildElement(QStringLiteral("ranges"));
    for (QDomElement range = ranges.firstChildElement(); !range.isNull(); range = range.nextSiblingElement()) {
        const QStringList values = range.text().split(QLatin1Char(',')); bool firstOk = false, lastOk = false; FontRange value;
        if (values.size() == 2) { value.first = values.at(0).toInt(&firstOk); value.last = values.at(1).toInt(&lastOk); }
        if (!firstOk || !lastOk || value.first < 0 || value.last > 65535 || value.first > value.last) { error = tr("Invalid font character range."); return false; }
        state.ranges.append(value);
    }
    QFile image(fontImagePath(filePath)); const bool exists = image.exists(); QByteArray imageBytes;
    if (exists) {
        if (!image.open(QIODevice::ReadOnly)) { error = image.errorString(); return false; }
        imageBytes = image.readAll(); if (image.error() != QFile::NoError) { error = image.errorString(); return false; }
    }
    m_state = state; m_savedState = state; m_xml = xml; m_sourceBytes = bytes; m_imageBytes = imageBytes;
    m_filePath = QFileInfo(filePath).absoluteFilePath(); m_imageExists = exists; m_configurationIndex = config;
    m_undoStack.clear(); emit changed(); return true;
}
QString FontDocument::name() const
{ QString result = QFileInfo(m_filePath).fileName(); result.chop(QStringLiteral(".font.gmx").size()); return result; }
void FontDocument::edit(const FontState &state, const QString &description)
{ if (!(state == m_state)) m_undoStack.push(new FontChangeCommand(this, state, description)); }
void FontDocument::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_filePath = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_filePath)); }
bool FontDocument::save(QString &error)
{
    error.clear(); if (!isModified() && m_imageExists) return true;
    QFile source(m_filePath);
    if (!source.open(QIODevice::ReadOnly)) { error = source.errorString(); return false; }
    if (source.readAll() != m_sourceBytes || source.error() != QFile::NoError) { error = tr("The font file changed outside the editor. Reopen it before saving."); return false; }
    source.close();
    FontState rasterState = m_state; rasterState.includeTTF = m_savedState.includeTTF; rasterState.textureGroup = m_savedState.textureGroup;
    const bool rebuild = !(rasterState == m_savedState) || !m_imageExists;
    QDomDocument xml = m_xml.cloneNode(true).toDocument(); writeFontState(xml, m_state, m_configurationIndex);
    if (m_state.family != m_savedState.family) setFontText(xml, xml.documentElement(), QStringLiteral("TTFName"), QString());
    QByteArray png;
    const QString imagePath = fontImagePath(m_filePath);
    if (rebuild) {
        QFile image(imagePath);
        if (image.exists() != m_imageExists || (m_imageExists && (!image.open(QIODevice::ReadOnly) || image.readAll() != m_imageBytes || image.error() != QFile::NoError))) {
            error = tr("The font texture changed outside the editor. Reopen the font before saving."); return false;
        }
        image.close(); FontAtlasData atlas;
        if (!FontAtlas::build(m_state, atlas, error) || !encodeFontAtlas(atlas, png, error)) return false;
        writeFontGlyphs(xml, atlas, QFileInfo(imagePath).fileName());
        if (!writeFontFile(imagePath, png, error)) return false;
    }
    const QByteArray bytes = xml.toByteArray(2);
    if (!writeFontFile(m_filePath, bytes, error)) {
        // The original compiler requires a fixed PNG basename, so roll back that
        // file when committing the XML fails; never leave old glyphs with a new page.
        if (rebuild) {
            QString rollbackError; QFile image(imagePath);
            const bool unchanged = image.open(QIODevice::ReadOnly) && image.readAll() == png;
            image.close();
            const bool restored = unchanged && (m_imageExists ? writeFontFile(imagePath, m_imageBytes, rollbackError) : QFile::remove(imagePath));
            if (!restored) error += tr("\nThe font texture could not be restored: %1\n%2").arg(imagePath, rollbackError);
        }
        return false;
    }
    if (rebuild) { m_imageBytes = png; m_imageExists = true; }
    m_xml = xml; m_sourceBytes = bytes; m_savedState = m_state; m_undoStack.setClean(); emit saved(); return true;
}
