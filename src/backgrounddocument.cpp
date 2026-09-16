#include "backgrounddocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMap>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>

class BackgroundChangeCommand : public QUndoCommand
{
public:
    BackgroundChangeCommand(BackgroundDocument *document, const BackgroundState &state, const QString &description)
        : QUndoCommand(description), m_document(document), m_before(document->state()), m_after(state) {}
    void undo() override { m_document->applyState(m_before); }
    void redo() override { m_document->applyState(m_after); }
private:
    BackgroundDocument *m_document;
    BackgroundState m_before;
    BackgroundState m_after;
};

static int backgroundValue(const QDomElement &root, const QString &tag, int defaultValue = 0)
{
    const QDomElement element = root.firstChildElement(tag);
    return element.isNull() ? defaultValue : element.text().toInt();
}

static void setBackgroundText(QDomDocument &document, QDomElement parent, const QString &tag, const QString &text)
{
    QDomElement element = parent.firstChildElement(tag);
    if (element.isNull()) { element = document.createElement(tag); parent.appendChild(element); }
    while (!element.firstChild().isNull()) element.removeChild(element.firstChild());
    element.appendChild(document.createTextNode(text));
}

static void writeBackgroundState(QDomDocument &document, const BackgroundState &state, const QString &imageReference, int configurationIndex)
{
    QDomElement root = document.documentElement();
    const QMap<QString, int> values = {
        {QStringLiteral("istileset"), state.isTileSet ? -1 : 0},
        {QStringLiteral("tilewidth"), state.tileWidth}, {QStringLiteral("tileheight"), state.tileHeight},
        {QStringLiteral("tilexoff"), state.horizontalOffset}, {QStringLiteral("tileyoff"), state.verticalOffset},
        {QStringLiteral("tilehsep"), state.horizontalSeparation}, {QStringLiteral("tilevsep"), state.verticalSeparation},
        {QStringLiteral("HTile"), state.tileHorizontal ? -1 : 0}, {QStringLiteral("VTile"), state.tileVertical ? -1 : 0},
        {QStringLiteral("For3D"), state.for3D ? -1 : 0},
        {QStringLiteral("width"), state.image.width()}, {QStringLiteral("height"), state.image.height()}
    };
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        setBackgroundText(document, root, it.key(), QString::number(it.value()));
    QDomElement groups = root.firstChildElement(QStringLiteral("TextureGroups"));
    if (groups.isNull()) { groups = document.createElement(QStringLiteral("TextureGroups")); root.appendChild(groups); }
    const QString initial = groups.firstChildElement(QStringLiteral("TextureGroup0")).text();
    for (int i = 0; i <= configurationIndex; ++i) {
        const QString tag = QStringLiteral("TextureGroup%1").arg(i);
        if (i == configurationIndex || groups.firstChildElement(tag).isNull()) setBackgroundText(document, groups, tag, i == configurationIndex ? QString::number(state.textureGroup) : (initial.isEmpty() ? QStringLiteral("0") : initial));
    }
    setBackgroundText(document, root, QStringLiteral("data"), imageReference);
}

BackgroundDocument::BackgroundDocument(QObject *parent) : QObject(parent), m_undoStack(this)
{ m_undoStack.setUndoLimit(100); }

bool BackgroundDocument::createEmpty(const QString &filePath, QString &error)
{
    error.clear();
    QDomDocument document;
    document.appendChild(document.createElement(QStringLiteral("background")));
    writeBackgroundState(document, BackgroundState(), QString(), 0);
    const QByteArray bytes = document.toByteArray(2);
    QTemporaryFile file(QFileInfo(filePath).absoluteDir().filePath(QStringLiteral(".qtgms-background-XXXXXX")));
    if (!file.open() || file.write(bytes) != bytes.size() || !file.flush()) { error = file.errorString(); return false; }
    file.close();
    if (!file.rename(filePath)) { error = file.errorString(); return false; }
    file.setAutoRemove(false);
    return true;
}

QString BackgroundDocument::name() const
{
    QString result = QFileInfo(m_filePath).fileName();
    result.chop(QStringLiteral(".background.gmx").size());
    return result;
}

bool BackgroundDocument::load(const QString &filePath, int configurationIndex, QString &error)
{
    m_configurationIndex = qMax(0, configurationIndex);
    error.clear();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    const QByteArray bytes = file.readAll();
    QDomDocument document;
    if (!document.setContent(bytes, false, &error)) return false;
    return loadFromXml(filePath, document, bytes, configurationIndex, error);
}

bool BackgroundDocument::loadFromXml(const QString &filePath, const QDomDocument &document, const QByteArray &bytes,
                                     int configurationIndex, QString &error)
{
    m_configurationIndex = qMax(0, configurationIndex);
    error.clear();
    const QDomElement root = document.documentElement();
    if (root.tagName() != QStringLiteral("background")) { error = tr("Expected a background resource."); return false; }
    BackgroundState state;
    state.isTileSet = backgroundValue(root, QStringLiteral("istileset")) != 0;
    state.tileWidth = backgroundValue(root, QStringLiteral("tilewidth"), 16);
    state.tileHeight = backgroundValue(root, QStringLiteral("tileheight"), 16);
    state.horizontalOffset = backgroundValue(root, QStringLiteral("tilexoff"));
    state.verticalOffset = backgroundValue(root, QStringLiteral("tileyoff"));
    state.horizontalSeparation = backgroundValue(root, QStringLiteral("tilehsep"));
    state.verticalSeparation = backgroundValue(root, QStringLiteral("tilevsep"));
    if (state.tileWidth < 1 || state.tileWidth > 32000 || state.tileHeight < 1 || state.tileHeight > 32000
        || state.horizontalOffset < 0 || state.horizontalOffset > 32000 || state.verticalOffset < 0 || state.verticalOffset > 32000
        || state.horizontalSeparation < 0 || state.horizontalSeparation > 32000 || state.verticalSeparation < 0 || state.verticalSeparation > 32000) {
        error = tr("The background has invalid tile dimensions, offsets or separation."); return false;
    }
    state.tileHorizontal = backgroundValue(root, QStringLiteral("HTile")) != 0;
    state.tileVertical = backgroundValue(root, QStringLiteral("VTile")) != 0;
    state.for3D = backgroundValue(root, QStringLiteral("For3D")) != 0;
    const auto groups = root.firstChildElement(QStringLiteral("TextureGroups"));
    state.textureGroup = backgroundValue(groups, QStringLiteral("TextureGroup%1").arg(m_configurationIndex), backgroundValue(groups, QStringLiteral("TextureGroup0")));
    const QString reference = root.firstChildElement(QStringLiteral("data")).text().trimmed();
    QString imagePath;
    if (!reference.isEmpty()) {
        imagePath = QDir::cleanPath(QFileInfo(filePath).absoluteDir().absoluteFilePath(QString(reference).replace(QLatin1Char('\\'), QLatin1Char('/'))));
        // An absent image represents an empty background, even with a stored path.
        if (!QFileInfo::exists(imagePath)) imagePath.clear();
    }
    if (!imagePath.isEmpty()) {
        QImageReader reader(imagePath);
        state.image = reader.read().convertToFormat(QImage::Format_ARGB32);
        if (state.image.isNull()) { error = tr("Cannot read background image %1:\n%2").arg(imagePath, reader.errorString()); return false; }
    }
    if (!state.image.isNull() && (state.image.width() != backgroundValue(root, QStringLiteral("width"))
        || state.image.height() != backgroundValue(root, QStringLiteral("height")))) {
        error = tr("The image dimensions do not match the background resource."); return false;
    }
    m_filePath = QFileInfo(filePath).absoluteFilePath();
    m_imagePath = imagePath;
    m_sourceBytes = bytes;
    m_xml = document;
    m_savedImage = state.image;
    m_undoStack.clear();
    applyState(state);
    return true;
}

void BackgroundDocument::applyState(const BackgroundState &state) { m_state = state; emit changed(); }
void BackgroundDocument::edit(const BackgroundState &state, const QString &description)
{ m_undoStack.push(new BackgroundChangeCommand(this, state, description)); }
void BackgroundDocument::replaceImage(const QImage &image)
{
    const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
    if (converted.isNull() || converted == m_state.image) return;
    BackgroundState state = m_state;
    state.image = converted;
    edit(state, tr("Edit background image"));
}
void BackgroundDocument::relocate(const QString &oldDirectory, const QString &newDirectory)
{
    m_filePath = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_filePath));
    if (!m_imagePath.isEmpty()) m_imagePath = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_imagePath));
}

bool BackgroundDocument::save(QString &error)
{
    error.clear();
    if (!isModified()) return true;
    QFile source(m_filePath);
    if (!source.open(QIODevice::ReadOnly)) { error = source.errorString(); return false; }
    if (source.readAll() != m_sourceBytes) { error = tr("The background file changed outside the editor. Reopen it before saving."); return false; }
    source.close();
    const QDir directory = QFileInfo(m_filePath).absoluteDir();
    QString imagePath = m_imagePath;
    bool createdImage = false;
    if (m_state.image.isNull()) {
        imagePath.clear();
    } else if (m_state.image != m_savedImage) {
        if (!directory.mkpath(QStringLiteral("images"))) { error = tr("Cannot create the background images directory."); return false; }
        QString token = QUuid::createUuid().toString();
        token.remove(QLatin1Char('{')).remove(QLatin1Char('}')).remove(QLatin1Char('-'));
        imagePath = directory.absoluteFilePath(QStringLiteral("images/%1_%2.png").arg(name(), token));
        QSaveFile output(imagePath);
        if (!output.open(QIODevice::WriteOnly) || !m_state.image.save(&output, "PNG") || !output.commit()) {
            error = tr("Cannot save background image:\n%1").arg(output.errorString()); return false;
        }
        createdImage = true;
    }
    QDomDocument document = m_xml.cloneNode(true).toDocument();
    writeBackgroundState(document, m_state, imagePath.isEmpty() ? QString() : QDir::toNativeSeparators(directory.relativeFilePath(imagePath)), m_configurationIndex);
    const QByteArray bytes = document.toByteArray(2);
    QSaveFile output(m_filePath);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        error = tr("Cannot save background:\n%1").arg(output.errorString());
        if (createdImage) QFile::remove(imagePath);
        return false;
    }
    m_imagePath = imagePath;
    m_savedImage = m_state.image;
    m_xml = document;
    m_sourceBytes = bytes;
    m_undoStack.setClean();
    emit saved();
    return true;
}
