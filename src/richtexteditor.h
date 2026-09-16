#ifndef QTGMS_RICHTEXTEDITOR_H
#define QTGMS_RICHTEXTEDITOR_H
#include <QWidget>
#include <QColor>
#include <QFont>
#include <memory>
struct NativeRichText;
struct RichTextFormat {
    QString family;
    qreal pointSize = 10;
    bool bold = false, italic = false, underline = false, bullets = false;
    Qt::Alignment alignment = Qt::AlignLeft;
    QColor color;
};
class RichTextEditor : public QWidget
{
    Q_OBJECT
public:
    explicit RichTextEditor(QWidget *parent = nullptr);
    ~RichTextEditor() override;
    bool isAvailable() const;
    bool setRtf(const QByteArray &bytes, bool undoable, QString &error);
    QByteArray rtf(QString &error) const;
    bool isModified() const;
    void setModified(bool modified);
    bool canUndo() const;
    bool canRedo() const;
    bool hasSelection() const;
    bool canPaste() const;
    RichTextFormat currentFormat() const;
    void setFamily(const QString &family);
    void setPointSize(qreal size);
    void setBold(bool enabled);
    void setItalic(bool enabled);
    void setUnderline(bool enabled);
    void setTextColor(const QColor &color);
    void setFontFormat(const QFont &font);
    void setAlignment(Qt::Alignment alignment);
    void setBullets(bool enabled);
    void setPageColor(const QColor &color);
    QColor pageColor() const;
    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void selectAll();
    int lineCount() const;
    void goToLine(int line);
    bool print(QString &error);
    void focusEditor();
signals:
    void changed();
    void selectionChanged();
    void saveRequested();
    void commandRequested(const QString &command);
    void contextMenuRequested(const QPoint &globalPosition);
private:
    std::unique_ptr<NativeRichText> m_native;
};
#endif
