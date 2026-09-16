#include "windowtitlebar.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLinearGradient>
#include <QPainter>
#include <QStyle>
#include <QToolButton>

static QIcon captionIcon(QStyle::StandardPixmap button)
{
    QPixmap pixmap(14, 10);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(QColor(238, 238, 238));
    switch (button) {
    case QStyle::SP_TitleBarMinButton:
        painter.fillRect(QRect(3, 7, 8, 2), QColor(238, 238, 238));
        break;
    case QStyle::SP_TitleBarMaxButton:
        painter.drawRect(3, 1, 8, 7);
        painter.drawLine(3, 2, 11, 2);
        break;
    case QStyle::SP_TitleBarNormalButton:
        painter.drawRect(5, 1, 6, 5);
        painter.fillRect(QRect(2, 3, 7, 6), QColor(35, 35, 35));
        painter.drawRect(2, 3, 6, 5);
        painter.drawLine(2, 4, 8, 4);
        break;
    case QStyle::SP_TitleBarCloseButton:
        painter.setPen(QColor(95, 150, 26));
        painter.drawLine(4, 2, 10, 8);
        painter.drawLine(4, 8, 10, 2);
        break;
    default:
        break;
    }
    return QIcon(pixmap);
}

WindowTitleBar::WindowTitleBar(QWidget *parent)
    : QWidget(parent),
      m_minimizeButton(new QToolButton(this)),
      m_maximizeButton(new QToolButton(this)),
      m_closeButton(new QToolButton(this))
{
    setObjectName(QStringLiteral("windowTitleBar"));
    setFixedHeight(24);
    QFont titleFont(QStringLiteral("Tahoma"));
    titleFont.setPixelSize(11);
    setFont(titleFont);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 2, 1, 0);
    layout->setSpacing(1);
    layout->addStretch();
    for (QToolButton *button : {m_minimizeButton, m_maximizeButton, m_closeButton}) {
        button->setFixedSize(24, 14);
        button->setIconSize(QSize(14, 10));
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button, 0, Qt::AlignTop);
    }
    m_minimizeButton->setIcon(captionIcon(QStyle::SP_TitleBarMinButton));
    m_minimizeButton->setToolTip(tr("Minimize"));
    m_minimizeButton->setAccessibleName(tr("Minimize"));
    m_closeButton->setIcon(captionIcon(QStyle::SP_TitleBarCloseButton));
    m_closeButton->setToolTip(tr("Close"));
    m_closeButton->setAccessibleName(tr("Close"));

    connect(m_minimizeButton, &QToolButton::clicked, this, [this] {
        window()->showMinimized();
    });
    connect(m_maximizeButton, &QToolButton::clicked, this, [this] {
        if (window()->isMaximized())
            window()->showNormal();
        else
            window()->showMaximized();
    });
    connect(m_closeButton, &QToolButton::clicked, this, [this] { window()->close(); });
    updateWindowState();
}

void WindowTitleBar::updateWindowState()
{
    m_minimizeButton->setVisible(window()->windowFlags() & Qt::WindowMinimizeButtonHint);
    m_maximizeButton->setVisible(window()->windowFlags() & Qt::WindowMaximizeButtonHint);
    const bool maximized = window()->isMaximized();
    m_maximizeButton->setIcon(captionIcon(maximized ? QStyle::SP_TitleBarNormalButton
                                                  : QStyle::SP_TitleBarMaxButton));
    m_maximizeButton->setToolTip(maximized ? tr("Restore") : tr("Maximize"));
    m_maximizeButton->setAccessibleName(m_maximizeButton->toolTip());
    update();
}

bool WindowTitleBar::containsSystemMenu(const QPoint &position) const
{
    return QRect(0, 0, 20, height()).contains(position);
}

bool WindowTitleBar::containsCaption(const QPoint &position) const
{
    return rect().contains(position) && position.x() >= 20
        && position.x() < captionRight();
}

int WindowTitleBar::captionRight() const
{
    if (!m_minimizeButton->isHidden()) return m_minimizeButton->geometry().left();
    if (!m_maximizeButton->isHidden()) return m_maximizeButton->geometry().left();
    return m_closeButton->geometry().left();
}

void WindowTitleBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    QLinearGradient gradient(0, 0, 0, 15);
    gradient.setColorAt(0, QColor(91, 91, 91));
    gradient.setColorAt(0.35, QColor(47, 47, 47));
    gradient.setColorAt(1, QColor(20, 20, 20));
    painter.fillRect(rect(), gradient);
    window()->windowIcon().paint(&painter, QRect(1, 1, 16, 16));

    const QRect textRect(21, 0, qMax(0, captionRight() - 27), 18);
    painter.setPen(window()->isActiveWindow() ? QColor(245, 245, 245)
                                             : QColor(155, 155, 155));
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                     fontMetrics().elidedText(window()->windowTitle(), Qt::ElideRight,
                                              textRect.width()));
}
