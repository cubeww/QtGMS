#include "shaderdocument.h"
#include "textfiledocument.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPlainTextDocumentLayout>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTextCursor>
#include <QTextDocument>

static const QString ShaderMarker = QStringLiteral("//######################_==_YOYO_SHADER_MARKER_==_######################@~");

ShaderDocument::ShaderDocument(QObject *parent) : QObject(parent), m_file(new TextFileDocument(this))
{
    m_file->textDocument()->setUndoRedoEnabled(false);
    for (int i = 0; i < 2; ++i) {
        m_stages[i] = new QTextDocument(this);
        m_stages[i]->setDocumentLayout(new QPlainTextDocumentLayout(m_stages[i]));
        connect(m_stages[i], &QTextDocument::modificationChanged, this, &ShaderDocument::modifiedChanged);
    }
}
bool ShaderDocument::createEmpty(const QString &filePath, QString &error)
{
    error.clear();
    QFile source(QStringLiteral(":/templates/default.shader"));
    if (!source.open(QIODevice::ReadOnly)) { error = source.errorString(); return false; }
    const QByteArray bytes = source.readAll();
    QTemporaryFile file(QFileInfo(filePath).absoluteDir().filePath(QStringLiteral(".qtgms-shader-XXXXXX")));
    if (!file.open() || file.write(bytes) != bytes.size() || !file.flush()) { error = file.errorString(); return false; }
    file.close();
    if (!file.rename(filePath)) { error = file.errorString(); return false; }
    file.setAutoRemove(false); return true;
}
bool ShaderDocument::load(const QString &filePath, QString &error)
{
    if (!m_file->load(filePath, error)) return false;
    const QString text = m_file->textDocument()->toPlainText();
    const int marker = text.indexOf(ShaderMarker);
    if (marker < 0 || text.indexOf(ShaderMarker, marker + ShaderMarker.size()) >= 0) {
        error = tr("Expected exactly one GameMaker vertex/fragment separator in the shader file."); return false;
    }
    m_stages[0]->setPlainText(text.left(marker));
    m_stages[1]->setPlainText(text.mid(marker + ShaderMarker.size()));
    for (QTextDocument *stage : m_stages) stage->setModified(false);
    return true;
}
bool ShaderDocument::save(QString &error)
{
    error.clear(); if (!isModified()) return true;
    const QString vertex = m_stages[0]->toPlainText(), fragment = m_stages[1]->toPlainText();
    if (vertex.contains(ShaderMarker) || fragment.contains(ShaderMarker)) {
        error = tr("The GameMaker shader separator cannot appear inside a shader stage."); return false;
    }
    m_file->textDocument()->setPlainText(vertex + ShaderMarker + fragment);
    m_file->textDocument()->setModified(true);
    if (!m_file->save(error)) return false;
    for (QTextDocument *stage : m_stages) stage->setModified(false);
    return true;
}
bool ShaderDocument::importStage(int stage, const QString &filePath, QString &error)
{
    QString text; if (!TextFileDocument::readText(filePath, text, error)) return false;
    if (text.contains(ShaderMarker)) {
        error = tr("Choose a single vertex or fragment source file to import into the current tab."); return false;
    }
    QTextCursor edit(stageDocument(stage)); edit.beginEditBlock(); edit.select(QTextCursor::Document);
    edit.insertText(text); edit.endEditBlock(); return true;
}
bool ShaderDocument::exportStage(int stage, const QString &filePath, QString &error)
{
    error.clear();
    if (QFileInfo(filePath).absoluteFilePath().compare(this->filePath(), Qt::CaseInsensitive) == 0) {
        error = tr("Choose another file: exporting one stage would replace the complete shader resource."); return false;
    }
    QSaveFile file(filePath); const QByteArray bytes = stageDocument(stage)->toPlainText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = file.errorString(); return false;
    }
    return true;
}
void ShaderDocument::relocate(const QString &oldDirectory, const QString &newDirectory) { m_file->relocate(oldDirectory, newDirectory); }
QTextDocument *ShaderDocument::stageDocument(int stage) const { Q_ASSERT(stage == 0 || stage == 1); return m_stages[stage]; }
bool ShaderDocument::isModified() const { return m_stages[0]->isModified() || m_stages[1]->isModified(); }
QString ShaderDocument::filePath() const { return m_file->filePath(); }
QString ShaderDocument::name() const { return m_file->name(); }
QString ShaderDocument::formatDescription() const { return m_file->formatDescription(); }
