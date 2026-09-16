#include "editorwindow.h"

#include "windowtitlebar.h"

#include <QEvent>
#include <QMenuBar>
#include <QPainter>
#include <QRegion>
#include <QVBoxLayout>
#include <QtMath>

#include <qt_windows.h>
#include <windowsx.h>

static const int FrameWidth = 4;

static QPolygon frameOutline(const QSize &size)
{
    const int right = size.width() - 1;
    const int bottom = size.height() - 1;
    return QPolygon(QVector<QPoint>{QPoint(0, 4), QPoint(4, 0), QPoint(right - 4, 0),
        QPoint(right, 4), QPoint(right, bottom), QPoint(0, bottom)});
}

EditorWindow::EditorWindow(QWidget *parent)
    : QMainWindow(parent, Qt::Window | Qt::FramelessWindowHint
                  | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint
                  | Qt::WindowCloseButtonHint),
      m_titleBar(new WindowTitleBar(this)),
      m_editorMenuBar(new QMenuBar(this)),
      m_frameRect(rect())
{
    setAcceptDrops(true);
    setContentsMargins(FrameWidth, FrameWidth, FrameWidth, FrameWidth);
    for (QWidget *ancestor = parent; ancestor; ancestor = ancestor->parentWidget()) {
        if (auto *host = qobject_cast<EditorWindow *>(ancestor)) {
            connect(this, &EditorWindow::resourceNameActivated, host, &EditorWindow::resourceNameActivated);
            break;
        }
    }
    auto *header = new QWidget(this);
    auto *layout = new QVBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_titleBar);
    layout->addWidget(m_editorMenuBar);
    header->setMaximumWidth(contentsRect().width());
    setMenuWidget(header);
}

QMenuBar *EditorWindow::editorMenuBar() const
{
    return m_editorMenuBar;
}

void EditorWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    const HWND handle = reinterpret_cast<HWND>(winId());
    const LONG_PTR previousStyle = GetWindowLongPtrW(handle, GWL_STYLE);
    // Retain native move/size/system-menu behavior while WM_NCCALCSIZE removes
    // the native caption and borders. These APIs are available on Windows XP.
    const LONG_PTR frameStyle = previousStyle | WS_CAPTION | WS_THICKFRAME
        | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    if (frameStyle != previousStyle) {
        SetWindowLongPtrW(handle, GWL_STYLE, frameStyle);
        SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    updateFrameGeometry();
}

bool EditorWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    const MSG *nativeMessage = static_cast<MSG *>(message);
    switch (nativeMessage->message) {
    case WM_NCCALCSIZE:
        *result = 0;
        return true;
    case WM_NCACTIVATE:
        // The caption is painted by Qt; do not let Windows repaint it.
        *result = TRUE;
        return true;
    case WM_NCPAINT:
        *result = 0;
        return true;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        // Enter Windows' own move/size loop and caption double-click handling.
        // Qt should only receive client clicks on the three custom buttons.
        *result = DefWindowProcW(nativeMessage->hwnd, nativeMessage->message,
                                 nativeMessage->wParam, nativeMessage->lParam);
        return true;
    case WM_NCHITTEST: {
        const QPoint position(GET_X_LPARAM(nativeMessage->lParam),
                              GET_Y_LPARAM(nativeMessage->lParam));
        RECT bounds;
        GetWindowRect(nativeMessage->hwnd, &bounds);
        const qreal scale = devicePixelRatioF();
        const int borderWidth = qMax(1, qRound(FrameWidth * scale));
        if (!IsZoomed(nativeMessage->hwnd)) {
            const bool left = position.x() < bounds.left + borderWidth;
            const bool right = position.x() >= bounds.right - borderWidth;
            const bool top = position.y() < bounds.top + borderWidth;
            const bool bottom = position.y() >= bounds.bottom - borderWidth;
            if (top && left) *result = HTTOPLEFT;
            else if (top && right) *result = HTTOPRIGHT;
            else if (bottom && left) *result = HTBOTTOMLEFT;
            else if (bottom && right) *result = HTBOTTOMRIGHT;
            else if (left) *result = HTLEFT;
            else if (right) *result = HTRIGHT;
            else if (top) *result = HTTOP;
            else if (bottom) *result = HTBOTTOM;
            else *result = HTCLIENT;
            if (*result != HTCLIENT)
                return true;
        }
        // Win32 supplies native screen pixels; widgets use logical pixels.
        // Convert relative to this window so screen origins are never scaled.
        POINT clientPosition = {position.x(), position.y()};
        ScreenToClient(nativeMessage->hwnd, &clientPosition);
        const QPoint windowPosition(qRound(clientPosition.x / scale),
                                    qRound(clientPosition.y / scale));
        const QPoint titlePosition = m_titleBar->mapFrom(this, windowPosition);
        if (m_titleBar->containsSystemMenu(titlePosition))
            *result = HTSYSMENU;
        else if (m_titleBar->containsCaption(titlePosition))
            *result = HTCAPTION;
        else
            *result = HTCLIENT;
        return true;
    }
    case WM_GETMINMAXINFO: {
        auto *limits = reinterpret_cast<MINMAXINFO *>(nativeMessage->lParam);
        MONITORINFO monitor = MONITORINFO();
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(nativeMessage->hwnd, MONITOR_DEFAULTTONEAREST),
                            &monitor)) {
            limits->ptMaxPosition.x = monitor.rcWork.left - monitor.rcMonitor.left;
            limits->ptMaxPosition.y = monitor.rcWork.top - monitor.rcMonitor.top;
            limits->ptMaxSize.x = monitor.rcWork.right - monitor.rcWork.left;
            limits->ptMaxSize.y = monitor.rcWork.bottom - monitor.rcWork.top;
        }
        const qreal scale = devicePixelRatioF();
        limits->ptMinTrackSize.x = qRound(minimumWidth() * scale);
        limits->ptMinTrackSize.y = qRound(minimumHeight() * scale);
        if (maximumWidth() < QWIDGETSIZE_MAX)
            limits->ptMaxTrackSize.x = qRound(maximumWidth() * scale);
        if (maximumHeight() < QWIDGETSIZE_MAX)
            limits->ptMaxTrackSize.y = qRound(maximumHeight() * scale);
        *result = 0;
        return true;
    }
    case WM_NCRBUTTONUP:
        if (nativeMessage->wParam == HTCAPTION || nativeMessage->wParam == HTSYSMENU) {
            showSystemMenu(QPoint(GET_X_LPARAM(nativeMessage->lParam),
                                 GET_Y_LPARAM(nativeMessage->lParam)));
            *result = 0;
            return true;
        }
        break;
    case WM_SYSKEYDOWN:
        if (nativeMessage->wParam == VK_SPACE) {
            const QPoint position = m_titleBar->mapTo(this, QPoint(0, m_titleBar->height()));
            const qreal scale = devicePixelRatioF();
            POINT nativePosition = {qRound(position.x() * scale), qRound(position.y() * scale)};
            ClientToScreen(nativeMessage->hwnd, &nativePosition);
            showSystemMenu(QPoint(nativePosition.x, nativePosition.y));
            *result = 0;
            return true;
        }
        break;
    case WM_SYSCHAR:
        if (nativeMessage->wParam == VK_SPACE) {
            *result = 0;
            return true;
        }
        break;
    default:
        break;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

void EditorWindow::showSystemMenu(const QPoint &nativeScreenPosition)
{
    const HWND handle = reinterpret_cast<HWND>(winId());
    const HMENU menu = GetSystemMenu(handle, FALSE);
    const bool maximized = IsZoomed(handle);
    EnableMenuItem(menu, SC_RESTORE, MF_BYCOMMAND | (maximized ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, SC_MOVE, MF_BYCOMMAND | (maximized ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(menu, SC_SIZE, MF_BYCOMMAND | (maximized ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(menu, SC_MAXIMIZE, MF_BYCOMMAND | (maximized ? MF_GRAYED : MF_ENABLED));
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       nativeScreenPosition.x(), nativeScreenPosition.y(),
                                       0, handle, nullptr);
    if (command)
        PostMessageW(handle, WM_SYSCOMMAND, command, 0);
}

void EditorWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateFrameGeometry();
}

void EditorWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    if (isMaximized())
        updateFrameGeometry();
}

void EditorWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    switch (event->type()) {
    case QEvent::ActivationChange:
    case QEvent::WindowStateChange:
    case QEvent::WindowTitleChange:
    case QEvent::WindowIconChange:
        m_titleBar->updateWindowState();
        updateFrameGeometry();
        update();
        break;
    default:
        break;
    }
}

void EditorWindow::updateFrameGeometry()
{
    QMargins insets;
    m_frameRect = rect();
    RECT nativeFrame = RECT();
    bool hasNativeFrame = false;
    if (isMaximized() && testAttribute(Qt::WA_WState_Created)) {
        const HWND handle = reinterpret_cast<HWND>(winId());
        RECT bounds;
        MONITORINFO monitor = MONITORINFO();
        monitor.cbSize = sizeof(monitor);
        if (GetWindowRect(handle, &bounds)
            && GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor)) {
            // Windows retains an off-screen resize border when maximized.
            // Our client area includes it, so keep layout and painting inside
            // the actual visible work area. Native coordinates are physical pixels.
            const qreal scale = devicePixelRatioF();
            const LONG left = qMax(0L, monitor.rcWork.left - bounds.left);
            const LONG top = qMax(0L, monitor.rcWork.top - bounds.top);
            const LONG right = qMax(0L, bounds.right - monitor.rcWork.right);
            const LONG bottom = qMax(0L, bounds.bottom - monitor.rcWork.bottom);
            nativeFrame = RECT{left, top, bounds.right - bounds.left - right,
                               bounds.bottom - bounds.top - bottom};
            hasNativeFrame = true;
            m_frameRect = QRectF(left / scale, top / scale,
                                 (nativeFrame.right - left) / scale,
                                 (nativeFrame.bottom - top) / scale);
            // Widget layouts require integers; painting and the native region
            // must retain fractional logical coordinates (e.g. 11 / 2 = 5.5).
            insets = QMargins(qCeil(left / scale), qCeil(top / scale),
                              qCeil(right / scale), qCeil(bottom / scale));
        }
    }
    setContentsMargins(insets.left() + FrameWidth, insets.top() + FrameWidth,
                       insets.right() + FrameWidth, insets.bottom() + FrameWidth);
    // Qt 5.6 uses the full window width for the menu widget despite contents margins.
    QWidget *header = menuWidget();
    header->setMaximumWidth(contentsRect().width());
    header->resize(contentsRect().width(), header->height());

    if (hasNativeFrame) {
        // QRegion uses integer logical pixels, so it cannot represent every
        // physical boundary at fractional scaling. Set the exact Win32 region.
        clearMask();
        const HWND handle = reinterpret_cast<HWND>(winId());
        const HRGN region = CreateRectRgnIndirect(&nativeFrame);
        if (!region)
            return;
        const HRGN currentRegion = CreateRectRgn(0, 0, 0, 0);
        const bool unchanged = currentRegion && GetWindowRgn(handle, currentRegion) != ERROR
            && EqualRgn(currentRegion, region);
        if (currentRegion)
            DeleteObject(currentRegion);
        // Windows takes ownership only when SetWindowRgn succeeds.
        if (unchanged || !SetWindowRgn(handle, region, TRUE))
            DeleteObject(region);
    } else {
        // Polygon regions exclude their right/bottom edge; retain those frame pixels.
        setMask(QRegion(frameOutline(size() + QSize(1, 1))));
    }
}

void EditorWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    QPainter painter(this);
    const QRectF frame = m_frameRect;
    painter.translate(frame.topLeft());
    const qreal frameWidth = frame.width();
    const qreal frameHeight = frame.height();
    const QColor frameColor(38, 38, 38);
    painter.fillRect(QRectF(0, 0, frameWidth, FrameWidth), frameColor);
    painter.fillRect(QRectF(0, frameHeight - FrameWidth, frameWidth, FrameWidth), frameColor);
    painter.fillRect(QRectF(0, FrameWidth, FrameWidth, frameHeight - FrameWidth * 2), frameColor);
    painter.fillRect(QRectF(frameWidth - FrameWidth, FrameWidth,
                          FrameWidth, frameHeight - FrameWidth * 2), frameColor);
    painter.setPen(isActiveWindow() ? QColor(70, 105, 24) : QColor(67, 67, 67));
    if (isMaximized())
        painter.drawRect(QRectF(QPointF(0, 0), frame.size()).adjusted(0.5, 0.5, -0.5, -0.5));
    else
        painter.drawPolygon(frameOutline(frame.size().toSize()));
    painter.setPen(QColor(89, 89, 89));
    painter.drawLine(QPointF(5, 1), QPointF(frameWidth - 6, 1));
    painter.setPen(QColor(54, 54, 54));
    painter.drawLine(QPointF(4, 2), QPointF(frameWidth - 5, 2));
}
