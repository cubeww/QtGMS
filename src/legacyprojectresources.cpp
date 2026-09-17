#include "legacyprojectimporter.h"
#include "actionxml.h"
#include "imageoperations.h"

#include <QBuffer>
#include <QObject>
#include <QtEndian>

QImage LegacyProjectImporter::readImage(bool bgra, int width, int height)
{
    if (!bgra) {
        QByteArray bytes = m_reader.compressed();
        // Some GM8.1 loading images contain a second zlib stream inside the
        // compressed image record. Recognize its header before decoding it.
        if (bytes.size() >= 2) {
            const quint16 header = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(bytes.constData()));
            if ((header >> 8 & 0x0f) == 8 && (header >> 12) <= 7 && header % 31 == 0)
                bytes = m_reader.decompress(bytes);
        }
        QImage image = QImage::fromData(bytes);
        if (image.isNull()) m_reader.fail(QObject::tr("Cannot decode the legacy bitmap."));
        return image.convertToFormat(QImage::Format_ARGB32);
    }
    if (width < 1 || height < 1 || qint64(width) * height > 64 * 1024 * 1024)
        m_reader.fail(QObject::tr("Invalid image dimensions: %1 x %2.").arg(width).arg(height));
    const int size = m_reader.count(256 * 1024 * 1024);
    if (qint64(width) * height * 4 != size) m_reader.fail(QObject::tr("Image byte count does not match its dimensions."));
    const QByteArray pixels = m_reader.bytes(size);
    QImage image(width, height, QImage::Format_ARGB32);
    if (image.isNull()) m_reader.fail(QObject::tr("Not enough memory to import the image."));
    const uchar *source = reinterpret_cast<const uchar *>(pixels.constData());
    for (int y = 0; y < height; ++y) {
        m_reader.checkCancelled();
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x, source += 4) row[x] = qRgba(source[2], source[1], source[0], source[3]);
    }
    return image;
}

void LegacyProjectImporter::saveImage(const QString &path, QImage image, bool transparent, bool smooth)
{
    if (image.isNull()) return;
    if (transparent) {
        const QRgb key = image.pixel(0, image.height() - 1) & 0x00ffffffu;
        for (int y = 0; y < image.height(); ++y) {
            m_reader.checkCancelled();
            QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x)
                if ((row[x] & 0x00ffffffu) == key) row[x] &= 0x00ffffffu;
        }
    }
    if (smooth) image = ImageOperations::apply(image, ImageOperations::Operation::SmoothEdges, ImageOperationSettings());
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        m_reader.fail(QObject::tr("Cannot encode image: %1").arg(path));
    write(path, bytes);
}

void LegacyProjectImporter::readSound(LegacyResource &resource, int version)
{
    if (version != 440 && version != 600 && version != 800)
        m_reader.fail(QObject::tr("Unsupported sound version: %1.").arg(version));
    auto root = resource.document.documentElement();
    const int kind = m_reader.integer();
    QString extension = m_reader.string();
    if (!extension.isEmpty() && !extension.startsWith(QLatin1Char('.'))) extension.prepend(QLatin1Char('.'));
    if (extension.contains(QLatin1Char('/')) || extension.contains(QLatin1Char('\\')) || extension.contains(QLatin1Char(':')))
        m_reader.fail(QObject::tr("Invalid sound extension: %1.").arg(extension));
    QString original;
    QByteArray data;
    int effects = 0;
    double volume = 1, pan = 0;
    bool preload;
    if (version == 440) {
        if (kind != -1) data = m_reader.compressed();
        m_reader.skip(8);
        preload = !m_reader.boolean();
    } else {
        original = m_reader.string();
        if (m_reader.boolean()) data = version == 600 ? m_reader.compressed() : m_reader.blob();
        effects = m_reader.integer();
        volume = m_reader.real();
        pan = m_reader.real();
        preload = m_reader.boolean();
    }
    const QString fileName = resource.name + extension;
    validateName(fileName);
    if (!data.isEmpty()) write(QStringLiteral("sound/audio/") + fileName, data);
    ActionXml::setText(root, QStringLiteral("extension"), extension);
    ActionXml::setText(root, QStringLiteral("origname"), original);
    ActionXml::setText(root, QStringLiteral("data"), data.isEmpty() ? QString() : fileName);
    ActionXml::setText(root, QStringLiteral("kind"), QString::number(version == 440 ? 0 : kind));
    ActionXml::setText(root, QStringLiteral("effects"), QString::number(effects));
    ActionXml::setText(root, QStringLiteral("pan"), QString::number(pan, 'g', 17));
    ActionXml::setText(root, QStringLiteral("preload"), preload ? QStringLiteral("-1") : QStringLiteral("0"));
    ActionXml::setText(ActionXml::child(root, QStringLiteral("volume")), QStringLiteral("volume"), QString::number(volume, 'g', 17));
    ActionXml::setText(ActionXml::child(root, QStringLiteral("bitRates")), QStringLiteral("bitRate"), QStringLiteral("192"));
    ActionXml::setText(ActionXml::child(root, QStringLiteral("sampleRates")), QStringLiteral("sampleRate"), QStringLiteral("44100"));
    ActionXml::setText(ActionXml::child(root, QStringLiteral("types")), QStringLiteral("type"), QStringLiteral("0"));
    ActionXml::setText(ActionXml::child(root, QStringLiteral("bitDepths")), QStringLiteral("bitDepth"), QStringLiteral("16"));
    for (const QString &tag : {QStringLiteral("compressed"), QStringLiteral("streamed"), QStringLiteral("uncompressOnLoad"), QStringLiteral("audioGroup")}) ActionXml::setText(root, tag, QStringLiteral("0"));
}

void LegacyProjectImporter::readSprite(LegacyResource &resource, int version)
{
    if (version != 400 && version != 542 && version != 800 && version != 810)
        m_reader.fail(QObject::tr("Unsupported sprite version: %1.").arg(version));
    auto root = resource.document.documentElement();
    for (const QString &tag : {QStringLiteral("type"), QStringLiteral("colkind"), QStringLiteral("coltolerance"), QStringLiteral("bboxmode"), QStringLiteral("bbox_left"), QStringLiteral("bbox_right"), QStringLiteral("bbox_top"), QStringLiteral("bbox_bottom"), QStringLiteral("HTile"), QStringLiteral("VTile"), QStringLiteral("For3D"), QStringLiteral("width"), QStringLiteral("height")})
        ActionXml::setText(root, tag, QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("sepmasks"), QStringLiteral("0"));
    ActionXml::setText(ActionXml::child(root, QStringLiteral("TextureGroups")), QStringLiteral("TextureGroup0"), QStringLiteral("0"));
    bool transparent = false, smooth = false;
    if (version < 800) {
        integerFields(root, QStringLiteral("width|height|bbox_left|bbox_right|bbox_bottom|bbox_top"));
        transparent = m_reader.boolean();
        if (version > 400) { smooth = m_reader.boolean(); m_reader.boolean(); }
        integerFields(root, QStringLiteral("bboxmode"));
        ActionXml::setText(root, QStringLiteral("colkind"), m_reader.boolean() ? QStringLiteral("0") : QStringLiteral("1"));
        if (version == 400) m_reader.skip(8);
    }
    integerFields(root, QStringLiteral("xorig|yorigin"));
    const int frames = m_reader.count(100000);
    auto container = ActionXml::child(root, QStringLiteral("frames"));
    int frameIndex = 0;
    QSize size;
    for (int i = 0; i < frames; ++i) {
        QImage image;
        if (version >= 800) {
            m_reader.version({800, 810});
            const int width = m_reader.count(65536), height = m_reader.count(65536);
            if (width && height) image = readImage(true, width, height);
        } else if (m_reader.integer() != -1) image = readImage(false);
        if (image.isNull()) continue;
        if (!size.isEmpty() && size != image.size()) m_reader.fail(QObject::tr("Sprite frames have different sizes."));
        size = image.size();
        const QString path = QStringLiteral("images/%1_%2.png").arg(resource.name).arg(frameIndex);
        saveImage(QStringLiteral("sprites/") + path, image, transparent, smooth);
        auto frame = resource.document.createElement(QStringLiteral("frame"));
        frame.setAttribute(QStringLiteral("index"), frameIndex++);
        frame.appendChild(resource.document.createTextNode(QString(path).replace(QLatin1Char('/'), QLatin1Char('\\'))));
        container.appendChild(frame);
    }
    if (!size.isEmpty()) {
        ActionXml::setText(root, QStringLiteral("width"), QString::number(size.width()));
        ActionXml::setText(root, QStringLiteral("height"), QString::number(size.height()));
    }
    if (version >= 800) {
        integerFields(root, QStringLiteral("colkind|coltolerance"));
        booleanFields(root, QStringLiteral("sepmasks"));
        integerFields(root, QStringLiteral("bboxmode|bbox_left|bbox_right|bbox_bottom|bbox_top"));
    }
}

void LegacyProjectImporter::readBackground(LegacyResource &resource, int version)
{
    if (version != 400 && version != 543 && version != 710)
        m_reader.fail(QObject::tr("Unsupported background version: %1.").arg(version));
    auto root = resource.document.documentElement();
    for (const QString &tag : {QStringLiteral("istileset"), QStringLiteral("tilexoff"), QStringLiteral("tileyoff"), QStringLiteral("tilehsep"), QStringLiteral("tilevsep"), QStringLiteral("HTile"), QStringLiteral("VTile"), QStringLiteral("For3D"), QStringLiteral("width"), QStringLiteral("height")})
        ActionXml::setText(root, tag, QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("tilewidth"), QStringLiteral("16"));
    ActionXml::setText(root, QStringLiteral("tileheight"), QStringLiteral("16"));
    ActionXml::setText(ActionXml::child(root, QStringLiteral("TextureGroups")), QStringLiteral("TextureGroup0"), QStringLiteral("0"));
    bool transparent = false, smooth = false;
    QImage image;
    if (version < 710) {
        integerFields(root, QStringLiteral("width|height"));
        transparent = m_reader.boolean();
        if (version > 400) {
            smooth = m_reader.boolean();
            m_reader.boolean();
            booleanFields(root, QStringLiteral("istileset"));
            integerFields(root, QStringLiteral("tilewidth|tileheight|tilexoff|tileyoff|tilehsep|tilevsep"));
        } else m_reader.skip(8);
        if (m_reader.boolean() && m_reader.integer() != -1) image = readImage(false);
    } else {
        booleanFields(root, QStringLiteral("istileset"));
        integerFields(root, QStringLiteral("tilewidth|tileheight|tilexoff|tileyoff|tilehsep|tilevsep"));
        m_reader.version({800});
        const int width = m_reader.count(65536), height = m_reader.count(65536);
        if (width && height) image = readImage(true, width, height);
    }
    if (!image.isNull()) {
        const QString path = QStringLiteral("images/") + resource.name + QStringLiteral(".png");
        saveImage(QStringLiteral("background/") + path, image, transparent, smooth);
        ActionXml::setText(root, QStringLiteral("data"), QString(path).replace(QLatin1Char('/'), QLatin1Char('\\')));
        ActionXml::setText(root, QStringLiteral("width"), QString::number(image.width()));
        ActionXml::setText(root, QStringLiteral("height"), QString::number(image.height()));
    } else ActionXml::setText(root, QStringLiteral("data"), QString());
}

void LegacyProjectImporter::readPath(LegacyResource &resource, int version)
{
    if (version != 530) m_reader.fail(QObject::tr("Unsupported path version: %1.").arg(version));
    auto root = resource.document.documentElement();
    ActionXml::setText(root, QStringLiteral("kind"), m_reader.boolean() ? QStringLiteral("1") : QStringLiteral("0"));
    booleanFields(root, QStringLiteral("closed"));
    integerFields(root, QStringLiteral("precision"));
    reference(root, QStringLiteral("backroom"), LegacyResourceKind::Room, m_reader.integer(), false, true);
    integerFields(root, QStringLiteral("hsnap|vsnap"));
    auto points = ActionXml::child(root, QStringLiteral("points"));
    const int count = m_reader.count();
    for (int i = 0; i < count; ++i) {
        QStringList values;
        for (int coordinate = 0; coordinate < 3; ++coordinate) values.append(QString::number(m_reader.real(), 'g', 17));
        auto point = resource.document.createElement(QStringLiteral("point"));
        point.appendChild(resource.document.createTextNode(values.join(QLatin1Char(','))));
        points.appendChild(point);
    }
}

void LegacyProjectImporter::readScript(LegacyResource &resource, int version)
{
    if (version != 400 && version != 800 && version != 810)
        m_reader.fail(QObject::tr("Unsupported script version: %1.").arg(version));
    write(resource.path, m_reader.string().toUtf8());
    resource.document.clear();
}

void LegacyProjectImporter::readFont(LegacyResource &resource, int version)
{
    if (version != 540 && version != 800) m_reader.fail(QObject::tr("Unsupported font version: %1.").arg(version));
    auto root = resource.document.documentElement();
    ActionXml::setText(root, QStringLiteral("name"), m_reader.string());
    integerFields(root, QStringLiteral("size"));
    booleanFields(root, QStringLiteral("bold|italic"));
    const quint32 range = quint32(m_reader.integer());
    const int first = int(range & 65535), charset = int((range >> 16) & 255), antiAlias = int(range >> 24);
    const int last = m_reader.integer();
    if (first > last || last > 65535) m_reader.fail(QObject::tr("Invalid font character range."));
    ActionXml::setText(root, QStringLiteral("charset"), QString::number(charset));
    ActionXml::setText(root, QStringLiteral("aa"), QString::number(antiAlias == 0 && m_version < 810 ? 3 : qBound(0, antiAlias - 1, 3)));
    ActionXml::setText(root, QStringLiteral("renderhq"), QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("includeTTF"), QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("TTFName"), QString());
    ActionXml::setText(ActionXml::child(root, QStringLiteral("texgroups")), QStringLiteral("texgroup0"), QStringLiteral("0"));
    ActionXml::setText(ActionXml::child(root, QStringLiteral("ranges")), QStringLiteral("range0"), QStringLiteral("%1,%2").arg(first).arg(last));
    // FontDocument/FontAtlas build the texture from these settings on demand.
}
