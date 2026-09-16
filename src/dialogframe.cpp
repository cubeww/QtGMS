#include "dialogframe.h"
#include "windowtitlebar.h"
#include <QDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QSizeGrip>

class DialogBorder : public QWidget
{
public:
    explicit DialogBorder(QWidget *parent) : QWidget(parent)
    { setAttribute(Qt::WA_TransparentForMouseEvents); setFocusPolicy(Qt::NoFocus); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this); painter.setPen(QColor(70, 105, 24));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
};

DialogFrame::DialogFrame(QDialog *dialog)
    : QObject(dialog), m_dialog(dialog), m_titleBar(new WindowTitleBar(dialog)),
      m_border(new DialogBorder(dialog)), m_sizeGrip(new QSizeGrip(dialog))
{
    dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    // Keep the caption outside the internal layout, which Qt's standard dialogs
    // can rebuild while updating their contents.
    dialog->setContentsMargins(4, 28, 4, 4);
    dialog->installEventFilter(this); m_titleBar->installEventFilter(this);
    m_titleBar->updateWindowState(); updateGeometry();
}
void DialogFrame::updateGeometry()
{
    m_titleBar->setGeometry(4, 4, qMax(0, m_dialog->width() - 8), 24);
    m_border->setGeometry(m_dialog->rect());
    m_sizeGrip->setGeometry(qMax(0, m_dialog->width() - 16), qMax(0, m_dialog->height() - 16), 12, 12);
    m_sizeGrip->setVisible(!m_dialog->isMaximized() &&
        (m_dialog->minimumWidth() < m_dialog->maximumWidth() || m_dialog->minimumHeight() < m_dialog->maximumHeight()));
    m_sizeGrip->raise(); m_titleBar->raise(); m_border->raise();
}
bool DialogFrame::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_dialog) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest)
            updateGeometry();
        else if (event->type() == QEvent::WindowTitleChange || event->type() == QEvent::WindowIconChange ||
                 event->type() == QEvent::ActivationChange || event->type() == QEvent::WindowStateChange)
            m_titleBar->updateWindowState();
    } else if (watched == m_titleBar) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) { m_dragging = true; m_dragOffset = mouse->globalPos() - m_dialog->frameGeometry().topLeft(); return true; }
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->buttons() & Qt::LeftButton) m_dialog->move(mouse->globalPos() - m_dragOffset); else m_dragging = false;
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease) m_dragging = false;
    }
    return QObject::eventFilter(watched, event);
}
