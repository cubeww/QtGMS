#include "richtexteditor.h"
#include <QHBoxLayout>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QResizeEvent>
#include <QFocusEvent>
#include <QApplication>
#include <QClipboard>
#include <qt_windows.h>
#include <richedit.h>
#include <commdlg.h>
#include <cstring>

class RichTextHost;
struct NativeRichText {
    RichTextEditor *owner;
    RichTextHost *host = nullptr;
    QScrollBar *scroll = nullptr;
    HMODULE library = nullptr;
    HWND edit = nullptr;
    WNDPROC original = nullptr;
    bool loading = false, scrollPending = false;
    QColor page = QColor(33, 33, 33);
    void synchronizeScroll();
    void queueScroll();
};
class RichTextHost : public QWidget
{
public:
    RichTextHost(NativeRichText *native, QWidget *parent) : QWidget(parent), m_native(native) { setAttribute(Qt::WA_NativeWindow); setFocusPolicy(Qt::StrongFocus); }
protected:
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        // The native scrollbar lives outside this child host's clipping area;
        // its range is exposed through the application's styled Qt scrollbar.
        if (m_native->edit) MoveWindow(m_native->edit, 0, 0, qRound(width() * devicePixelRatioF()) + GetSystemMetrics(SM_CXVSCROLL), qRound(height() * devicePixelRatioF()), TRUE);
        m_native->queueScroll();
    }
    void focusInEvent(QFocusEvent *event) override { QWidget::focusInEvent(event); if (m_native->edit) SetFocus(m_native->edit); }
    bool nativeEvent(const QByteArray &type, void *message, long *result) override {
        auto *msg = static_cast<MSG *>(message);
        if (msg->message == WM_COMMAND && reinterpret_cast<HWND>(msg->lParam) == m_native->edit) {
            if (HIWORD(msg->wParam) == EN_CHANGE && !m_native->loading) emit m_native->owner->changed();
            if (HIWORD(msg->wParam) == EN_VSCROLL || HIWORD(msg->wParam) == EN_CHANGE) m_native->queueScroll();
        } else if (msg->message == WM_NOTIFY) {
            auto *header = reinterpret_cast<NMHDR *>(msg->lParam);
            if (header->hwndFrom == m_native->edit && header->code == EN_SELCHANGE && !m_native->loading) { emit m_native->owner->selectionChanged(); m_native->queueScroll(); }
        }
        return QWidget::nativeEvent(type, message, result);
    }
private:
    NativeRichText *m_native;
};
void NativeRichText::synchronizeScroll()
{
    if (!edit) return; SCROLLINFO info = SCROLLINFO(); info.cbSize = sizeof(info); info.fMask = SIF_ALL;
    if (GetScrollInfo(edit, SB_VERT, &info)) {
        const QSignalBlocker blocker(scroll); scroll->setRange(info.nMin, qMax(info.nMin, info.nMax - int(info.nPage) + 1));
        scroll->setPageStep(info.nPage); scroll->setSingleStep(20); scroll->setValue(info.nPos);
    }
}
void NativeRichText::queueScroll()
{
    if (scrollPending) return; scrollPending = true;
    QTimer::singleShot(0, owner, [this] { scrollPending = false; synchronizeScroll(); });
}
static LRESULT CALLBACK richTextProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *native = reinterpret_cast<NativeRichText *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!native) return DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        QString command;
        if (wParam == 'S') { emit native->owner->saveRequested(); return 0; }
        if (wParam == 'B') command = QStringLiteral("bold");
        if (wParam == 'I') command = QStringLiteral("italic");
        if (wParam == 'U') command = QStringLiteral("underline");
        if (wParam == 'P') command = QStringLiteral("print");
        if (wParam == 'O') command = QStringLiteral("load");
        if (wParam == 'G') command = QStringLiteral("goto");
        if (!command.isEmpty()) { emit native->owner->commandRequested(command); return 0; }
        if (wParam == 'A') { SendMessageW(window, EM_SETSEL, 0, -1); return 0; }
    }
    if (message == WM_CHAR && (wParam == 1 || wParam == 2 || wParam == 7 || wParam == 9 || wParam == 15 || wParam == 16 || wParam == 19 || wParam == 21) && (GetKeyState(VK_CONTROL) & 0x8000)) return 0;
    if (message == WM_CONTEXTMENU) {
        POINT position; GetCursorPos(&position);
        emit native->owner->contextMenuRequested(QPoint(position.x, position.y) / native->owner->devicePixelRatioF()); return 0;
    }
    const LRESULT result = CallWindowProcW(native->original, window, message, wParam, lParam);
    if (message == WM_MOUSEWHEEL || message == WM_VSCROLL || message == WM_KEYUP || message == WM_SIZE) native->queueScroll();
    return result;
}
RichTextEditor::RichTextEditor(QWidget *parent) : QWidget(parent), m_native(new NativeRichText)
{
    setAttribute(Qt::WA_StyledBackground);
    m_native->owner = this; auto *layout = new QHBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1); layout->setSpacing(0);
    m_native->host = new RichTextHost(m_native.get(), this); m_native->scroll = new QScrollBar(Qt::Vertical, this);
    layout->addWidget(m_native->host, 1); layout->addWidget(m_native->scroll); setFocusProxy(m_native->host);
    // Riched20 is present on the Windows XP baseline and handles RTF/codepages.
    m_native->library = LoadLibraryW(L"Riched20.dll");
    if (!m_native->library) return;
    m_native->edit = CreateWindowExW(0, L"RichEdit20W", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_DISABLENOSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
        0, 0, 100, 100, reinterpret_cast<HWND>(m_native->host->winId()), nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m_native->edit) return;
    SetWindowLongPtrW(m_native->edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(m_native.get()));
    m_native->original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_native->edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(richTextProcedure)));
    SendMessageW(m_native->edit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_SCROLL);
    SendMessageW(m_native->edit, EM_EXLIMITTEXT, 0, 64 * 1024 * 1024); SendMessageW(m_native->edit, EM_SETUNDOLIMIT, 100, 0);
    setPageColor(m_native->page);
    connect(m_native->scroll, &QScrollBar::valueChanged, this, [this](int value) { POINT point = POINT(); SendMessageW(m_native->edit, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&point)); point.y = value; SendMessageW(m_native->edit, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&point)); m_native->queueScroll(); });
}
RichTextEditor::~RichTextEditor()
{
    if (m_native->edit) { SetWindowLongPtrW(m_native->edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_native->original)); DestroyWindow(m_native->edit); m_native->edit = nullptr; }
    delete m_native->host; delete m_native->scroll;
    if (m_native->library) FreeLibrary(m_native->library);
}
bool RichTextEditor::isAvailable() const { return m_native->edit != nullptr; }
struct RichTextStream { QByteArray bytes; int offset = 0; };
static DWORD CALLBACK readRichText(DWORD_PTR cookie, LPBYTE buffer, LONG count, LONG *read)
{
    auto *stream = reinterpret_cast<RichTextStream *>(cookie); *read = qMin(int(count), stream->bytes.size() - stream->offset);
    if (*read) std::memcpy(buffer, stream->bytes.constData() + stream->offset, *read); stream->offset += *read; return 0;
}
static DWORD CALLBACK writeRichText(DWORD_PTR cookie, LPBYTE buffer, LONG count, LONG *written)
{
    auto *bytes = reinterpret_cast<QByteArray *>(cookie); bytes->append(reinterpret_cast<char *>(buffer), count); *written = count; return 0;
}
bool RichTextEditor::setRtf(const QByteArray &bytes, bool undoable, QString &error)
{
    if (!isAvailable()) { error = tr("Windows RichEdit could not be initialized."); return false; }
    if (!bytes.trimmed().startsWith("{\\rtf")) { error = tr("This file is not an RTF document."); return false; }
    // Validate in a separate native control before replacing the live document.
    HWND validator = CreateWindowExW(0, L"RichEdit20W", L"", ES_MULTILINE, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!validator) { error = tr("Cannot initialize the RTF reader."); return false; }
    RichTextStream input; input.bytes = bytes; EDITSTREAM stream = EDITSTREAM(); stream.dwCookie = reinterpret_cast<DWORD_PTR>(&input); stream.pfnCallback = readRichText;
    SendMessageW(validator, EM_EXLIMITTEXT, 0, 64 * 1024 * 1024);
    SendMessageW(validator, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream)); DestroyWindow(validator);
    if (stream.dwError) { error = tr("The RTF document could not be read (error %1).").arg(stream.dwError); return false; }
    m_native->loading = true;
    CHARRANGE range = {0, -1}; if (undoable) SendMessageW(m_native->edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    input.offset = 0; stream.dwError = 0;
    SendMessageW(m_native->edit, EM_STREAMIN, SF_RTF | (undoable ? SFF_SELECTION : 0), reinterpret_cast<LPARAM>(&stream));
    if (stream.dwError) { m_native->loading = false; setModified(true); emit changed(); error = tr("The RTF document could not be loaded (error %1).").arg(stream.dwError); return false; }
    if (!undoable) SendMessageW(m_native->edit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(m_native->edit, EM_SETSEL, 0, 0); setModified(undoable); m_native->loading = false; m_native->queueScroll();
    emit changed(); emit selectionChanged(); return true;
}
QByteArray RichTextEditor::rtf(QString &error) const
{
    if (!isAvailable()) { error = tr("Windows RichEdit is unavailable."); return QByteArray(); }
    QByteArray bytes; EDITSTREAM stream = EDITSTREAM(); stream.dwCookie = reinterpret_cast<DWORD_PTR>(&bytes); stream.pfnCallback = writeRichText;
    SendMessageW(m_native->edit, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&stream));
    if (stream.dwError) { error = tr("Cannot encode the RTF document (error %1).").arg(stream.dwError); return QByteArray(); } return bytes;
}
bool RichTextEditor::isModified() const { return SendMessageW(m_native->edit, EM_GETMODIFY, 0, 0) != 0; }
void RichTextEditor::setModified(bool value) { SendMessageW(m_native->edit, EM_SETMODIFY, value, 0); }
bool RichTextEditor::canUndo() const { return SendMessageW(m_native->edit, EM_CANUNDO, 0, 0) != 0; }
bool RichTextEditor::canRedo() const { return SendMessageW(m_native->edit, EM_CANREDO, 0, 0) != 0; }
bool RichTextEditor::hasSelection() const { CHARRANGE range = CHARRANGE(); SendMessageW(m_native->edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range)); return range.cpMin != range.cpMax; }
bool RichTextEditor::canPaste() const { return SendMessageW(m_native->edit, EM_CANPASTE, 0, 0) != 0; }
RichTextFormat RichTextEditor::currentFormat() const
{
    CHARFORMAT2W format = CHARFORMAT2W(); format.cbSize = sizeof(format); SendMessageW(m_native->edit, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
    RichTextFormat result; result.family = QString::fromWCharArray(format.szFaceName); result.pointSize = format.yHeight > 0 ? format.yHeight / 20.0 : 10;
    result.bold = format.dwEffects & CFE_BOLD; result.italic = format.dwEffects & CFE_ITALIC; result.underline = format.dwEffects & CFE_UNDERLINE;
    result.color = QColor(GetRValue(format.crTextColor), GetGValue(format.crTextColor), GetBValue(format.crTextColor));
    PARAFORMAT2 paragraph = PARAFORMAT2(); paragraph.cbSize = sizeof(paragraph); SendMessageW(m_native->edit, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&paragraph));
    result.alignment = paragraph.wAlignment == PFA_CENTER ? Qt::AlignHCenter : paragraph.wAlignment == PFA_RIGHT ? Qt::AlignRight : Qt::AlignLeft;
    result.bullets = paragraph.wNumbering == PFN_BULLET; return result;
}
static void applyCharacter(HWND edit, CHARFORMAT2W &format) { format.cbSize = sizeof(format); SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format)); }
void RichTextEditor::setFamily(const QString &family) { CHARFORMAT2W f = CHARFORMAT2W(); f.dwMask = CFM_FACE; family.left(LF_FACESIZE - 1).toWCharArray(f.szFaceName); applyCharacter(m_native->edit, f); setModified(true); emit changed(); emit selectionChanged(); }
void RichTextEditor::setPointSize(qreal size) { CHARFORMAT2W f = CHARFORMAT2W(); f.dwMask = CFM_SIZE; f.yHeight = qRound(qBound(1.0, size, 1638.0) * 20); applyCharacter(m_native->edit, f); setModified(true); emit changed(); emit selectionChanged(); }
void RichTextEditor::setBold(bool enabled) { CHARFORMAT2W f = CHARFORMAT2W(); f.dwMask = CFM_BOLD; f.dwEffects = enabled ? CFE_BOLD : 0; applyCharacter(m_native->edit, f); setModified(true); emit changed(); emit selectionChanged(); }
void RichTextEditor::setItalic(bool enabled) { CHARFORMAT2W f = CHARFORMAT2W(); f.dwMask = CFM_ITALIC; f.dwEffects = enabled ? CFE_ITALIC : 0; applyCharacter(m_native->edit, f); setModified(true); emit changed(); emit selectionChanged(); }
void RichTextEditor::setUnderline(bool enabled) { CHARFORMAT2W f = CHARFORMAT2W(); f.dwMask = CFM_UNDERLINE; f.dwEffects = enabled ? CFE_UNDERLINE : 0; applyCharacter(m_native->edit, f); setModified(true); emit changed(); emit selectionChanged(); }
void RichTextEditor::setTextColor(const QColor &color) { CHARFORMAT2W f = CHARFORMAT2W(); f.dwMask = CFM_COLOR; f.crTextColor = RGB(color.red(), color.green(), color.blue()); applyCharacter(m_native->edit, f); setModified(true); emit changed(); emit selectionChanged(); }
void RichTextEditor::setFontFormat(const QFont &font) {
    CHARFORMAT2W f = CHARFORMAT2W(); f.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE; font.family().left(LF_FACESIZE - 1).toWCharArray(f.szFaceName); f.yHeight = qRound(font.pointSizeF() * 20);
    f.dwEffects = (font.bold() ? CFE_BOLD : 0) | (font.italic() ? CFE_ITALIC : 0) | (font.underline() ? CFE_UNDERLINE : 0); applyCharacter(m_native->edit, f); setModified(true); emit changed(); emit selectionChanged();
}
void RichTextEditor::setAlignment(Qt::Alignment alignment) { PARAFORMAT2 f = PARAFORMAT2(); f.cbSize = sizeof(f); f.dwMask = PFM_ALIGNMENT; f.wAlignment = alignment == Qt::AlignHCenter ? PFA_CENTER : alignment == Qt::AlignRight ? PFA_RIGHT : PFA_LEFT; SendMessageW(m_native->edit, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&f)); setModified(true); emit changed(); emit selectionChanged(); }
void RichTextEditor::setBullets(bool enabled) { PARAFORMAT2 f = PARAFORMAT2(); f.cbSize = sizeof(f); f.dwMask = PFM_NUMBERING | PFM_OFFSET; f.wNumbering = enabled ? PFN_BULLET : 0; f.dxOffset = enabled ? 240 : 0; SendMessageW(m_native->edit, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&f)); setModified(true); emit changed(); emit selectionChanged(); }
void RichTextEditor::setPageColor(const QColor &color) { m_native->page = color; SendMessageW(m_native->edit, EM_SETBKGNDCOLOR, 0, RGB(color.red(), color.green(), color.blue())); }
QColor RichTextEditor::pageColor() const { return m_native->page; }
void RichTextEditor::undo() { SendMessageW(m_native->edit, EM_UNDO, 0, 0); emit changed(); emit selectionChanged(); }
void RichTextEditor::redo() { SendMessageW(m_native->edit, EM_REDO, 0, 0); emit changed(); emit selectionChanged(); }
void RichTextEditor::cut() { SendMessageW(m_native->edit, WM_CUT, 0, 0); }
void RichTextEditor::copy() { SendMessageW(m_native->edit, WM_COPY, 0, 0); }
void RichTextEditor::paste() { SendMessageW(m_native->edit, WM_PASTE, 0, 0); }
void RichTextEditor::selectAll() { SendMessageW(m_native->edit, EM_SETSEL, 0, -1); }
int RichTextEditor::lineCount() const { return SendMessageW(m_native->edit, EM_GETLINECOUNT, 0, 0); }
void RichTextEditor::goToLine(int line) { const LRESULT position = SendMessageW(m_native->edit, EM_LINEINDEX, qMax(0, line - 1), 0); if (position >= 0) { SendMessageW(m_native->edit, EM_SETSEL, position, position); SendMessageW(m_native->edit, EM_SCROLLCARET, 0, 0); } focusEditor(); }
void RichTextEditor::focusEditor() { m_native->host->setFocus(); if (m_native->edit) SetFocus(m_native->edit); }
bool RichTextEditor::print(QString &error)
{
    PRINTDLGW dialog = PRINTDLGW(); dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = reinterpret_cast<HWND>(window()->winId()); dialog.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS | PD_USEDEVMODECOPIESANDCOLLATE;
    if (!PrintDlgW(&dialog)) { const DWORD code = CommDlgExtendedError(); if (dialog.hDevMode) GlobalFree(dialog.hDevMode); if (dialog.hDevNames) GlobalFree(dialog.hDevNames); if (code) error = tr("Cannot open the print dialog (error %1).").arg(code); return !code; }
    HDC dc = dialog.hDC; DOCINFOW info = DOCINFOW(); info.cbSize = sizeof(info); info.lpszDocName = L"Game Information"; bool success = StartDocW(dc, &info) > 0;
    FORMATRANGE range = FORMATRANGE(); range.hdc = range.hdcTarget = dc;
    const int dpiX = GetDeviceCaps(dc, LOGPIXELSX), dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    range.rcPage.right = MulDiv(GetDeviceCaps(dc, PHYSICALWIDTH), 1440, dpiX); range.rcPage.bottom = MulDiv(GetDeviceCaps(dc, PHYSICALHEIGHT), 1440, dpiY);
    range.rc.left = qMax(720, MulDiv(GetDeviceCaps(dc, PHYSICALOFFSETX), 1440, dpiX)); range.rc.top = qMax(720, MulDiv(GetDeviceCaps(dc, PHYSICALOFFSETY), 1440, dpiY));
    range.rc.right = qMin<LONG>(range.rcPage.right - 720, MulDiv(GetDeviceCaps(dc, HORZRES), 1440, dpiX)); range.rc.bottom = qMin<LONG>(range.rcPage.bottom - 720, MulDiv(GetDeviceCaps(dc, VERTRES), 1440, dpiY));
    GETTEXTLENGTHEX length = GETTEXTLENGTHEX(); length.flags = GTL_NUMCHARS; length.codepage = 1200; const LONG count = SendMessageW(m_native->edit, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&length), 0); range.chrg.cpMax = count;
    do {
        if (!success || StartPage(dc) <= 0) { success = false; break; }
        const LONG next = SendMessageW(m_native->edit, EM_FORMATRANGE, TRUE, reinterpret_cast<LPARAM>(&range));
        if (EndPage(dc) <= 0 || (next <= range.chrg.cpMin && count > 0)) { success = false; break; } range.chrg.cpMin = next;
    } while (range.chrg.cpMin < count);
    SendMessageW(m_native->edit, EM_FORMATRANGE, FALSE, 0);
    if (success) success = EndDoc(dc) > 0; else AbortDoc(dc);
    DeleteDC(dc); if (dialog.hDevMode) GlobalFree(dialog.hDevMode); if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
    if (!success) error = tr("The document could not be printed."); return success;
}
