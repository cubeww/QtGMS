#include "spritedocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>
#include <QtMath>

static QString relocatedSpritePath(const QString &path, const QString &oldDirectory, const QString &newDirectory)
{
    if (path.isEmpty()) return path;
    return QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(path));
}

static void relocateSpriteState(SpriteState &state, const QString &oldDirectory, const QString &newDirectory)
{
    for (SpriteFrame &frame : state.frames)
        frame.filePath = relocatedSpritePath(frame.filePath, oldDirectory, newDirectory);
}

class SpriteChangeCommand : public QUndoCommand
{
public:
    SpriteChangeCommand(SpriteDocument *document, const SpriteState &state, const QString &text)
        : QUndoCommand(text), m_document(document), m_before(document->state()), m_after(state) {}
    void undo() override { m_document->applyState(m_before); }
    void redo() override { m_document->applyState(m_after); }
    void relocate(const QString &oldDirectory, const QString &newDirectory)
    {
        relocateSpriteState(m_before, oldDirectory, newDirectory);
        relocateSpriteState(m_after, oldDirectory, newDirectory);
    }
private:
    SpriteDocument *m_document;
    SpriteState m_before;
    SpriteState m_after;
};

static int spriteValue(const QDomElement &root, const QString &tag, int defaultValue = 0)
{
    const QDomElement element = root.firstChildElement(tag);
    return element.isNull() ? defaultValue : element.text().toInt();
}

static void setSpriteValue(QDomDocument &document, const QString &tag, int value)
{
    QDomElement root = document.documentElement();
    QDomElement element = root.firstChildElement(tag);
    if (element.isNull()) { element = document.createElement(tag); root.appendChild(element); }
    while (!element.firstChild().isNull()) element.removeChild(element.firstChild());
    element.appendChild(document.createTextNode(QString::number(value)));
}

SpriteDocument::SpriteDocument(QObject *parent) : QObject(parent), m_undoStack(this)
{ m_undoStack.setUndoLimit(100); }

void SpriteDocument::relocate(const QString &oldDirectory, const QString &newDirectory)
{
    m_filePath = relocatedSpritePath(m_filePath, oldDirectory, newDirectory);
    relocateSpriteState(m_state, oldDirectory, newDirectory);
    for (int i = 0; i < m_undoStack.count(); ++i) {
        auto *command = const_cast<SpriteChangeCommand *>(static_cast<const SpriteChangeCommand *>(m_undoStack.command(i)));
        command->relocate(oldDirectory, newDirectory);
    }
    emit changed();
}

bool SpriteDocument::createEmpty(const QString &filePath, QString &error)
{
    error.clear();
    QDomDocument document;
    QDomElement root = document.createElement(QStringLiteral("sprite"));
    document.appendChild(root);
    for (const QString &tag : {QStringLiteral("type"), QStringLiteral("xorig"), QStringLiteral("yorigin"),
         QStringLiteral("colkind"), QStringLiteral("coltolerance"), QStringLiteral("bboxmode"),
         QStringLiteral("bbox_left"), QStringLiteral("bbox_right"), QStringLiteral("bbox_top"),
         QStringLiteral("bbox_bottom"), QStringLiteral("HTile"), QStringLiteral("VTile"),
         QStringLiteral("For3D"), QStringLiteral("width"), QStringLiteral("height")})
        setSpriteValue(document, tag, 0);
    setSpriteValue(document, QStringLiteral("sepmasks"), -1);
    QDomElement groups = document.createElement(QStringLiteral("TextureGroups"));
    QDomElement group = document.createElement(QStringLiteral("TextureGroup0"));
    group.appendChild(document.createTextNode(QStringLiteral("0")));
    groups.appendChild(group);
    root.appendChild(groups);
    root.appendChild(document.createElement(QStringLiteral("frames")));
    const QByteArray bytes = document.toByteArray(2);
    // Rename a complete file into place; QFile::rename refuses an existing target.
    QTemporaryFile output(QFileInfo(filePath).absoluteDir().filePath(QStringLiteral(".qtgms-sprite-XXXXXX")));
    if (!output.open() || output.write(bytes) != bytes.size() || !output.flush()) {
        error = tr("Cannot create sprite:\n%1").arg(output.errorString());
        return false;
    }
    output.close();
    if (!output.rename(filePath)) {
        error = tr("Cannot create sprite:\n%1").arg(output.errorString());
        return false;
    }
    output.setAutoRemove(false);
    return true;
}

QString SpriteDocument::name() const
{
    QString result = QFileInfo(m_filePath).fileName();
    result.chop(QStringLiteral(".sprite.gmx").size());
    return result;
}
QString SpriteDocument::thumbnailPath() const
{ return m_state.frames.isEmpty() ? QString() : m_state.frames.first().filePath; }
void SpriteDocument::applyState(const SpriteState &state) { m_state = state; emit changed(); }
void SpriteDocument::edit(const SpriteState &state, const QString &description)
{ m_undoStack.push(new SpriteChangeCommand(this, state, description)); }
SpriteFrame SpriteDocument::createFrame(const QImage &image)
{
    SpriteFrame frame;
    frame.id = QUuid::createUuid().toString();
    frame.image = image.convertToFormat(QImage::Format_ARGB32);
    frame.modified = true;
    return frame;
}
int SpriteDocument::frameIndex(const QString &id) const
{
    for (int i = 0; i < m_state.frames.size(); ++i)
        if (m_state.frames.at(i).id == id) return i;
    return -1;
}
void SpriteDocument::replaceFrame(const QString &id, const QImage &image)
{
    const int index = frameIndex(id);
    if (index < 0 || image.isNull() || image.size() != m_state.size || m_state.frames.at(index).image == image) return;
    SpriteState next = m_state;
    next.frames[index].image = image.convertToFormat(QImage::Format_ARGB32);
    next.frames[index].modified = true;
    edit(next, tr("Edit subimage"));
}

bool SpriteDocument::load(const QString &filePath, int configurationIndex, QString &error)
{
    m_configurationIndex = qMax(0, configurationIndex);
    error.clear();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    const QByteArray source = file.readAll();
    QDomDocument document;
    int line = 0, column = 0;
    QString xmlError;
    if (!document.setContent(source, false, &xmlError, &line, &column)) {
        error = tr("%1\nLine %2, column %3: %4").arg(filePath).arg(line).arg(column).arg(xmlError);
        return false;
    }
    return loadFromXml(filePath, document, source, configurationIndex, error);
}

bool SpriteDocument::loadFromXml(const QString &filePath, const QDomDocument &document, const QByteArray &source,
                                 int configurationIndex, QString &error)
{
    m_configurationIndex = qMax(0, configurationIndex);
    error.clear();
    const QDomElement root = document.documentElement();
    if (root.tagName() != QStringLiteral("sprite")) { error = tr("Expected a sprite resource."); return false; }
    if (spriteValue(root, QStringLiteral("type")) != 0) {
        error = tr("This editor currently supports bitmap sprites. Vector and skeletal sprites cannot be edited yet.");
        return false;
    }
    SpriteState state;
    state.size = QSize(spriteValue(root, QStringLiteral("width")), spriteValue(root, QStringLiteral("height")));
    state.origin = QPoint(spriteValue(root, QStringLiteral("xorig")), spriteValue(root, QStringLiteral("yorigin")));
    state.collisionKind = spriteValue(root, QStringLiteral("colkind"));
    state.alphaTolerance = spriteValue(root, QStringLiteral("coltolerance"));
    state.boundingBoxMode = spriteValue(root, QStringLiteral("bboxmode"));
    state.boundingBox = QRect(QPoint(spriteValue(root, QStringLiteral("bbox_left")), spriteValue(root, QStringLiteral("bbox_top"))),
        QPoint(spriteValue(root, QStringLiteral("bbox_right")), spriteValue(root, QStringLiteral("bbox_bottom"))));
    state.separateMasks = spriteValue(root, QStringLiteral("sepmasks")) != 0;
    state.tileHorizontal = spriteValue(root, QStringLiteral("HTile")) != 0;
    state.tileVertical = spriteValue(root, QStringLiteral("VTile")) != 0;
    state.for3D = spriteValue(root, QStringLiteral("For3D")) != 0;
    const QDomElement groups = root.firstChildElement(QStringLiteral("TextureGroups"));
    state.textureGroup = spriteValue(groups, QStringLiteral("TextureGroup%1").arg(m_configurationIndex), spriteValue(groups, QStringLiteral("TextureGroup0")));
    QMap<int, SpriteFrame> orderedFrames;
    const QDir directory = QFileInfo(filePath).absoluteDir();
    for (QDomElement entry = root.firstChildElement(QStringLiteral("frames")).firstChildElement(QStringLiteral("frame"));
         !entry.isNull(); entry = entry.nextSiblingElement(QStringLiteral("frame"))) {
        bool validIndex;
        const int index = entry.attribute(QStringLiteral("index")).toInt(&validIndex);
        if (!validIndex || index < 0 || orderedFrames.contains(index)) { error = tr("Invalid or duplicate subimage index."); return false; }
        if (state.size.isEmpty()) { error = tr("Invalid sprite dimensions."); return false; }
        const QString path = directory.absoluteFilePath(QString(entry.text().trimmed()).replace(QLatin1Char('\\'), QLatin1Char('/')));
        QImageReader reader(path);
        QImage image = reader.read();
        if (image.isNull()) { error = tr("Cannot read subimage %1:\n%2").arg(path, reader.errorString()); return false; }
        // GMX frames may differ from the declared sprite size. Keep their pixels
        // intact for texture compilation; only collision masks use state.size.
        SpriteFrame frame = createFrame(image);
        frame.modified = false;
        frame.filePath = QDir::cleanPath(path);
        orderedFrames.insert(index, frame);
    }
    for (auto it = orderedFrames.constBegin(); it != orderedFrames.constEnd(); ++it) {
        if (it.key() != state.frames.size()) { error = tr("Subimage indices must start at zero and be consecutive."); return false; }
        state.frames.append(it.value());
    }
    m_filePath = QFileInfo(filePath).absoluteFilePath();
    m_xml = document;
    m_sourceBytes = source;
    m_undoStack.clear();
    applyState(state);
    return true;
}

bool SpriteDocument::importImages(const QStringList &paths, QList<SpriteFrame> &frames, QString &error)
{
    error.clear();
    QList<SpriteFrame> imported;
    for (const QString &path : paths) {
        QImageReader reader(path);
        const QRegularExpressionMatch strip = QRegularExpression(QStringLiteral("_strip(\\d+)$"),
            QRegularExpression::CaseInsensitiveOption).match(QFileInfo(path).completeBaseName());
        const int stripCount = strip.hasMatch() ? strip.captured(1).toInt() : 1;
        do {
            const QImage image = reader.read();
            if (image.isNull()) { error = tr("Cannot read %1:\n%2").arg(path, reader.errorString()); return false; }
            if (stripCount < 1 || stripCount > image.width() || image.width() % stripCount) {
                error = tr("The image width does not match the _strip frame count in %1.").arg(path); return false;
            }
            for (int index = 0; index < stripCount; ++index)
                imported.append(createFrame(stripCount == 1 ? image : image.copy(index * (image.width() / stripCount), 0,
                    image.width() / stripCount, image.height())));
        } while (reader.supportsAnimation() && reader.canRead());
    }
    if (imported.isEmpty()) { error = tr("No images were selected."); return false; }
    const QSize size = imported.first().image.size();
    for (const SpriteFrame &frame : imported) {
        if (frame.image.size() != size) { error = tr("All subimages must have the same dimensions."); return false; }
    }
    frames = imported;
    return true;
}

QRect SpriteDocument::boundingBox(int frame) const
{
    const QRect full(QPoint(), m_state.size);
    if (m_state.boundingBoxMode == 1) return full;
    if (m_state.boundingBoxMode == 2) return m_state.boundingBox.intersected(full);
    QRect bounds;
    for (int i = 0; i < m_state.frames.size(); ++i) {
        if (frame >= 0 && m_state.separateMasks && i != frame) continue;
        const QImage &image = m_state.frames.at(i).image;
        const int width = qMin(image.width(), m_state.size.width());
        const int height = qMin(image.height(), m_state.size.height());
        int left = width, right = -1, top = height, bottom = -1;
        for (int y = 0; y < height; ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < width; ++x) {
                if (qAlpha(row[x]) > m_state.alphaTolerance) {
                    left = qMin(left, x); right = qMax(right, x);
                    top = qMin(top, y); bottom = qMax(bottom, y);
                }
            }
        }
        if (right >= left) bounds = bounds.united(QRect(QPoint(left, top), QPoint(right, bottom)));
    }
    return bounds;
}

QImage SpriteDocument::collisionMask(int frame) const
{
    if (frame < 0 || frame >= m_state.frames.size()) return QImage();
    QImage mask(m_state.size, QImage::Format_ARGB32);
    mask.fill(Qt::transparent);
    const QRect bounds = boundingBox(frame);
    const qreal cx = (bounds.left() + bounds.right()) / 2.0;
    const qreal cy = (bounds.top() + bounds.bottom()) / 2.0;
    for (int y = bounds.top(); !bounds.isEmpty() && y <= bounds.bottom(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(mask.scanLine(y));
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            bool solid = false;
            for (int i = 0; i < m_state.frames.size(); ++i) {
                if (m_state.separateMasks && i != frame) continue;
                const QImage &image = m_state.frames.at(i).image;
                // Missing pixels in smaller frames are transparent. Larger
                // frames are clipped to the declared collision canvas by bounds.
                if (!image.valid(x, y)) continue;
                if (m_state.collisionKind != 0 || qAlpha(image.pixel(x, y)) > m_state.alphaTolerance) {
                    solid = true; break;
                }
            }
            if (solid && m_state.collisionKind != 0 && m_state.collisionKind != 1 && m_state.collisionKind != 5) {
                const qreal dx = (x - cx) / (bounds.width() / 2.0);
                const qreal dy = (y - cy) / (bounds.height() / 2.0);
                solid = m_state.collisionKind == 2 ? dx * dx + dy * dy <= 1 : qAbs(dx) + qAbs(dy) <= 1;
            }
            if (solid) row[x] = qRgba(50, 200, 40, 150);
        }
    }
    return mask;
}

bool SpriteDocument::save(QString &error)
{
    error.clear();
    if (!isModified()) return true;
    QFile source(m_filePath);
    if (!source.open(QIODevice::ReadOnly)) { error = source.errorString(); return false; }
    if (source.readAll() != m_sourceBytes) {
        error = tr("The sprite file changed outside the editor. Reopen it before saving.");
        return false;
    }
    source.close();
    QDomDocument document = m_xml.cloneNode(true).toDocument();
    SpriteState savedState = m_state;
    QDir directory = QFileInfo(m_filePath).absoluteDir();
    QStringList createdFiles;
    for (int i = 0; i < savedState.frames.size(); ++i) {
        SpriteFrame &frame = savedState.frames[i];
        if (!frame.modified) continue;
        if (!directory.mkpath(QStringLiteral("images"))) { error = tr("Cannot create the sprite images directory."); break; }
        QString token = QUuid::createUuid().toString();
        token.remove(QLatin1Char('{')).remove(QLatin1Char('}')).remove(QLatin1Char('-'));
        const QString path = directory.absoluteFilePath(QStringLiteral("images/%1_%2_%3.png").arg(name(), token).arg(i));
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || !frame.image.save(&output, "PNG") || !output.commit()) {
            error = tr("Cannot save subimage %1:\n%2").arg(path, output.errorString());
            break;
        }
        createdFiles.append(path);
        frame.filePath = path;
        frame.modified = false;
    }
    if (error.isEmpty()) {
        const QRect bounds = boundingBox();
        const QMap<QString, int> values = {
            {QStringLiteral("xorig"), m_state.origin.x()}, {QStringLiteral("yorigin"), m_state.origin.y()},
            {QStringLiteral("width"), m_state.size.width()}, {QStringLiteral("height"), m_state.size.height()},
            {QStringLiteral("colkind"), m_state.collisionKind}, {QStringLiteral("coltolerance"), m_state.alphaTolerance},
            {QStringLiteral("bboxmode"), m_state.boundingBoxMode}, {QStringLiteral("sepmasks"), m_state.separateMasks ? -1 : 0},
            {QStringLiteral("bbox_left"), bounds.isEmpty() ? 0 : bounds.left()},
            {QStringLiteral("bbox_right"), bounds.isEmpty() ? 0 : bounds.right()},
            {QStringLiteral("bbox_top"), bounds.isEmpty() ? 0 : bounds.top()},
            {QStringLiteral("bbox_bottom"), bounds.isEmpty() ? 0 : bounds.bottom()},
            {QStringLiteral("HTile"), m_state.tileHorizontal ? -1 : 0}, {QStringLiteral("VTile"), m_state.tileVertical ? -1 : 0},
            {QStringLiteral("For3D"), m_state.for3D ? -1 : 0}
        };
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) setSpriteValue(document, it.key(), it.value());
        QDomElement root = document.documentElement();
        QDomElement groups = root.firstChildElement(QStringLiteral("TextureGroups"));
        if (groups.isNull()) { groups = document.createElement(QStringLiteral("TextureGroups")); root.appendChild(groups); }
        const QString initial = groups.firstChildElement(QStringLiteral("TextureGroup0")).text();
        for (int i = 0; i <= m_configurationIndex; ++i) {
            const QString tag = QStringLiteral("TextureGroup%1").arg(i); auto group = groups.firstChildElement(tag);
            if (i != m_configurationIndex && !group.isNull()) continue;
            if (group.isNull()) { group = document.createElement(tag); groups.appendChild(group); }
            while (!group.firstChild().isNull()) group.removeChild(group.firstChild());
            group.appendChild(document.createTextNode(i == m_configurationIndex ? QString::number(m_state.textureGroup) : (initial.isEmpty() ? QStringLiteral("0") : initial)));
        }
        QDomElement frames = root.firstChildElement(QStringLiteral("frames"));
        if (frames.isNull()) { frames = document.createElement(QStringLiteral("frames")); root.appendChild(frames); }
        for (QDomElement frame = frames.firstChildElement(QStringLiteral("frame")); !frame.isNull();) {
            const QDomElement next = frame.nextSiblingElement(QStringLiteral("frame"));
            frames.removeChild(frame); frame = next;
        }
        for (int i = 0; i < savedState.frames.size(); ++i) {
            QDomElement frame = document.createElement(QStringLiteral("frame"));
            frame.setAttribute(QStringLiteral("index"), i);
            frame.appendChild(document.createTextNode(QDir::toNativeSeparators(directory.relativeFilePath(savedState.frames.at(i).filePath))));
            frames.appendChild(frame);
        }
        const QByteArray bytes = document.toByteArray(2);
        QSaveFile output(m_filePath);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
            error = tr("Cannot save sprite:\n%1").arg(output.errorString());
        else {
            m_xml = document;
            m_sourceBytes = bytes;
            m_state = savedState;
            m_undoStack.setClean();
            emit changed();
            emit saved();
            return true;
        }
    }
    // Only files created by this save attempt are removed on failure.
    for (const QString &path : createdFiles) QFile::remove(path);
    return false;
}
