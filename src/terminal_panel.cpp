#include "terminal_panel.h"

#include <wx/dcbuffer.h>
#include <pty.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <cstring>
#include <cerrno>
#include <algorithm>

enum { TIMER_PTY_POLL = 1 };

// ============================================================================
// Construction / destruction
// ============================================================================

TerminalPanel::TerminalPanel(wxWindow* parent, const wxString& command)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxWANTS_CHARS | wxNO_BORDER)
    , m_pollTimer(this, TIMER_PTY_POLL)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(wxColour(30, 30, 30));

    // Monospace fonts
    m_font     = wxFont(wxFontInfo(11).Family(wxFONTFAMILY_TELETYPE).FaceName("Monospace"));
    m_fontBold = m_font.Bold();
    RecalcCellSize();

    // --- VTerm setup ---
    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);

    // Output callback: keyboard input → bytes to write to PTY
    vterm_output_set_callback(m_vt, &TerminalPanel::OnVtOutput, this);

    // Screen callbacks
    m_screenCbs.damage      = &TerminalPanel::OnVtDamage;
    m_screenCbs.movecursor  = &TerminalPanel::OnVtMoveCursor;
    m_screenCbs.bell        = &TerminalPanel::OnVtBell;
    m_screenCbs.sb_pushline = &TerminalPanel::OnVtSbPushLine;
    m_screenCbs.sb_popline  = &TerminalPanel::OnVtSbPopLine;

    m_vtScreen = vterm_obtain_screen(m_vt);
    vterm_screen_set_callbacks(m_vtScreen, &m_screenCbs, this);
    vterm_screen_reset(m_vtScreen, 1);

    // --- Scrollbar ---
    m_scrollbar = new wxScrollBar(this, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, wxSB_VERTICAL);
    m_scrollbar->SetScrollbar(0, m_rows, m_rows, m_rows);

    // --- Event bindings ---
    Bind(wxEVT_PAINT,       &TerminalPanel::OnPaint,      this);
    Bind(wxEVT_SIZE,        &TerminalPanel::OnSize,        this);
    Bind(wxEVT_CHAR,        &TerminalPanel::OnChar,        this);
    Bind(wxEVT_KEY_DOWN,    &TerminalPanel::OnKeyDown,     this);
    Bind(wxEVT_TIMER,       &TerminalPanel::OnTimer,       this, TIMER_PTY_POLL);
    Bind(wxEVT_SET_FOCUS,   &TerminalPanel::OnFocus,       this);
    Bind(wxEVT_KILL_FOCUS,  &TerminalPanel::OnFocus,       this);
    Bind(wxEVT_MOUSEWHEEL,  &TerminalPanel::OnMouseWheel,  this);
    m_scrollbar->Bind(wxEVT_SCROLL_THUMBTRACK,   &TerminalPanel::OnScrollbar, this);
    m_scrollbar->Bind(wxEVT_SCROLL_CHANGED,      &TerminalPanel::OnScrollbar, this);
    m_scrollbar->Bind(wxEVT_SCROLL_LINEUP,       &TerminalPanel::OnScrollbar, this);
    m_scrollbar->Bind(wxEVT_SCROLL_LINEDOWN,     &TerminalPanel::OnScrollbar, this);
    m_scrollbar->Bind(wxEVT_SCROLL_PAGEUP,       &TerminalPanel::OnScrollbar, this);
    m_scrollbar->Bind(wxEVT_SCROLL_PAGEDOWN,     &TerminalPanel::OnScrollbar, this);

    // --- Spawn child process in a PTY ---
    if (SpawnChild(command))
        m_pollTimer.Start(16);   // ~60 fps polling
}

TerminalPanel::~TerminalPanel() {
    m_pollTimer.Stop();
    if (m_childPid > 0)
        kill(m_childPid, SIGHUP);
    if (m_masterFd >= 0)
        close(m_masterFd);
    if (m_vt)
        vterm_free(m_vt);
}

// ============================================================================
// Public API
// ============================================================================

void TerminalPanel::InjectText(const std::string& text) {
    if (m_masterFd >= 0 && !text.empty())
        ::write(m_masterFd, text.c_str(), text.length());
}

// ============================================================================
// PTY management
// ============================================================================

bool TerminalPanel::SpawnChild(const wxString& command) {
    struct winsize ws = {};
    ws.ws_row   = static_cast<unsigned short>(m_rows);
    ws.ws_col   = static_cast<unsigned short>(m_cols);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    m_childPid = forkpty(&m_masterFd, nullptr, nullptr, &ws);
    if (m_childPid < 0) {
        wxLogError("forkpty failed: %s", strerror(errno));
        return false;
    }

    if (m_childPid == 0) {
        // --- Child process ---
        setenv("TERM",      "xterm-256color", 1);
        setenv("COLORTERM", "truecolor",      1);

        wxString cmd = command.IsEmpty() ? wxString("bash") : command;
        execlp("/bin/sh", "sh", "-c", cmd.mb_str().data(), nullptr);
        _exit(127);
    }

    return true;   // parent
}

void TerminalPanel::ReadPTY() {
    if (m_masterFd < 0) return;

    fd_set fds;
    struct timeval tv = {0, 0};     // non-blocking poll
    bool didRead = false;

    for (;;) {
        FD_ZERO(&fds);
        FD_SET(m_masterFd, &fds);

        if (select(m_masterFd + 1, &fds, nullptr, nullptr, &tv) <= 0)
            break;

        char buf[4096];
        ssize_t n = ::read(m_masterFd, buf, sizeof(buf));
        if (n <= 0) {
            // Child exited
            m_pollTimer.Stop();
            close(m_masterFd);
            m_masterFd = -1;
            const char* msg = "\r\n\033[1;33m[Process exited]\033[0m\r\n";
            vterm_input_write(m_vt, msg, strlen(msg));
            didRead = true;
            break;
        }

        vterm_input_write(m_vt, buf, static_cast<size_t>(n));
        didRead = true;
    }

    if (didRead) {
        // New output → snap to bottom
        m_scrollOffset = 0;
        UpdateScrollbar();
        Refresh();
    }
}

// ============================================================================
// Geometry helpers
// ============================================================================

void TerminalPanel::RecalcCellSize() {
    wxClientDC dc(this);
    dc.SetFont(m_font);
    wxSize sz = dc.GetTextExtent("M");
    m_cellW = std::max(1, sz.GetWidth());
    m_cellH = std::max(1, sz.GetHeight());
}

void TerminalPanel::ResizeTerminal() {
    wxSize cs = GetClientSize();
    if (cs.GetWidth() <= 0 || cs.GetHeight() <= 0) return;

    // Position scrollbar on the right edge
    int sbWidth = m_scrollbar ? m_scrollbar->GetBestSize().GetWidth() : 0;
    if (m_scrollbar)
        m_scrollbar->SetSize(cs.GetWidth() - sbWidth, 0, sbWidth, cs.GetHeight());

    int usableWidth = cs.GetWidth() - sbWidth;
    int newCols = std::max(2, usableWidth / m_cellW);
    int newRows = std::max(1, cs.GetHeight() / m_cellH);
    if (newRows == m_rows && newCols == m_cols) return;

    m_rows = newRows;
    m_cols = newCols;
    vterm_set_size(m_vt, m_rows, m_cols);

    if (m_masterFd >= 0) {
        struct winsize ws = {};
        ws.ws_row = static_cast<unsigned short>(m_rows);
        ws.ws_col = static_cast<unsigned short>(m_cols);
        ioctl(m_masterFd, TIOCSWINSZ, &ws);
    }
}

// ============================================================================
// Color conversion
// ============================================================================

wxColour TerminalPanel::VTermColorToWx(VTermColor col, bool isFg) {
    if (VTERM_COLOR_IS_DEFAULT_FG(&col) || VTERM_COLOR_IS_DEFAULT_BG(&col))
        return isFg ? wxColour(204, 204, 204) : wxColour(30, 30, 30);

    if (VTERM_COLOR_IS_INDEXED(&col))
        vterm_screen_convert_color_to_rgb(m_vtScreen, &col);

    if (VTERM_COLOR_IS_RGB(&col))
        return wxColour(col.rgb.red, col.rgb.green, col.rgb.blue);

    return isFg ? wxColour(204, 204, 204) : wxColour(30, 30, 30);
}

// ============================================================================
// Helper: draw a single row of cells
// ============================================================================

static void DrawCellRow(wxDC& dc, const VTermScreenCell* cells, int cols,
                        int y, int cellW, int cellH,
                        wxFont& font, wxFont& fontBold,
                        TerminalPanel* panel)
{
    for (int col = 0; col < cols; ) {
        const VTermScreenCell& cell = cells[col];
        int width = cell.width > 0 ? cell.width : 1;
        int x = col * cellW;
        int w = width * cellW;

        wxColour fg = panel->VTermColorToWx(cell.fg, true);
        wxColour bg = panel->VTermColorToWx(cell.bg, false);
        if (cell.attrs.reverse) std::swap(fg, bg);

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(bg));
        dc.DrawRectangle(x, y, w, cellH);

        if (cell.chars[0] != 0) {
            dc.SetFont(cell.attrs.bold ? fontBold : font);
            dc.SetTextForeground(fg);

            wxString ch;
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
                ch += wxUniChar(cell.chars[i]);
            dc.DrawText(ch, x, y);

            if (cell.attrs.underline) {
                dc.SetPen(wxPen(fg));
                dc.DrawLine(x, y + cellH - 1, x + w, y + cellH - 1);
            }
            if (cell.attrs.strike) {
                dc.SetPen(wxPen(fg));
                dc.DrawLine(x, y + cellH / 2, x + w, y + cellH / 2);
            }
        }
        col += width;
    }
}

// ============================================================================
// wx event handlers
// ============================================================================

void TerminalPanel::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(wxColour(30, 30, 30)));
    dc.Clear();

    if (!m_vtScreen) return;

    int sbSize = static_cast<int>(m_scrollback.size());

    for (int row = 0; row < m_rows; ++row) {
        int y = row * m_cellH;

        // Which logical line does this screen row correspond to?
        int logicalRow = row - m_scrollOffset;
        int sbRow = sbSize - m_scrollOffset + row;

        if (m_scrollOffset > 0 && sbRow >= 0 && sbRow < sbSize) {
            // Drawing from scrollback buffer
            auto& line = m_scrollback[sbRow];
            // Pad line to m_cols if needed
            std::vector<VTermScreenCell> cells(m_cols);
            memset(cells.data(), 0, sizeof(VTermScreenCell) * m_cols);
            int copyLen = std::min(static_cast<int>(line.cells.size()), m_cols);
            for (int c = 0; c < copyLen; ++c)
                cells[c] = line.cells[c];
            DrawCellRow(dc, cells.data(), m_cols, y, m_cellW, m_cellH,
                        m_font, m_fontBold, this);
        } else {
            // Drawing from live VTerm screen
            int vtRow = row - m_scrollOffset;
            if (m_scrollOffset > 0)
                vtRow = row - (m_scrollOffset - std::min(m_scrollOffset, sbSize));

            // Simpler: when scrolled up, first m_scrollOffset rows come from
            // scrollback, rest from vterm.  The vterm row index is:
            //   row - min(m_scrollOffset, sbSize)
            // but only if that's >= 0.
            int effectiveSbRows = std::min(m_scrollOffset, sbSize);
            if (row < effectiveSbRows) {
                // Already handled above... skip
                continue;
            }
            vtRow = row - effectiveSbRows;
            if (vtRow < 0 || vtRow >= m_rows) continue;

            VTermScreenCell cells[512];
            int ncols = std::min(m_cols, 512);
            for (int c = 0; c < ncols; ++c) {
                VTermPos pos = {vtRow, c};
                vterm_screen_get_cell(m_vtScreen, pos, &cells[c]);
            }
            DrawCellRow(dc, cells, ncols, y, m_cellW, m_cellH,
                        m_font, m_fontBold, this);
        }
    }

    // Cursor (only when at bottom / not scrolled up)
    if (m_scrollOffset == 0 && m_cursorVisible && HasFocus() &&
        m_cursorPos.row >= 0 && m_cursorPos.row < m_rows &&
        m_cursorPos.col >= 0 && m_cursorPos.col < m_cols)
    {
        int cx = m_cursorPos.col * m_cellW;
        int cy = m_cursorPos.row * m_cellH;
        dc.SetPen(wxPen(wxColour(200, 200, 200)));
        dc.SetBrush(wxBrush(wxColour(200, 200, 200, 120)));
        dc.DrawRectangle(cx, cy, m_cellW, m_cellH);
    }
}

void TerminalPanel::OnSize(wxSizeEvent& evt) {
    RecalcCellSize();
    ResizeTerminal();
    Refresh();
    evt.Skip();
}

void TerminalPanel::OnChar(wxKeyEvent& evt) {
    if (m_masterFd < 0) return;

    // Any keypress snaps to bottom
    m_scrollOffset = 0;

    wxChar uc = evt.GetUnicodeKey();
    if (uc == WXK_NONE) { evt.Skip(); return; }

    VTermModifier mod = VTERM_MOD_NONE;
    if (evt.AltDown())
        mod = static_cast<VTermModifier>(mod | VTERM_MOD_ALT);

    vterm_keyboard_unichar(m_vt, static_cast<uint32_t>(uc), mod);
}

void TerminalPanel::OnKeyDown(wxKeyEvent& evt) {
    if (m_masterFd < 0) { evt.Skip(); return; }

    // Any keypress snaps to bottom
    m_scrollOffset = 0;

    VTermModifier mod = VTERM_MOD_NONE;
    if (evt.ControlDown()) mod = static_cast<VTermModifier>(mod | VTERM_MOD_CTRL);
    if (evt.AltDown())     mod = static_cast<VTermModifier>(mod | VTERM_MOD_ALT);
    if (evt.ShiftDown())   mod = static_cast<VTermModifier>(mod | VTERM_MOD_SHIFT);

    VTermKey key = VTERM_KEY_NONE;
    switch (evt.GetKeyCode()) {
        case WXK_RETURN:   key = VTERM_KEY_ENTER;     break;
        case WXK_TAB:      key = VTERM_KEY_TAB;       break;
        case WXK_BACK:     key = VTERM_KEY_BACKSPACE;  break;
        case WXK_ESCAPE:   key = VTERM_KEY_ESCAPE;    break;
        case WXK_UP:       key = VTERM_KEY_UP;        break;
        case WXK_DOWN:     key = VTERM_KEY_DOWN;      break;
        case WXK_LEFT:     key = VTERM_KEY_LEFT;      break;
        case WXK_RIGHT:    key = VTERM_KEY_RIGHT;     break;
        case WXK_INSERT:   key = VTERM_KEY_INS;       break;
        case WXK_DELETE:   key = VTERM_KEY_DEL;       break;
        case WXK_HOME:     key = VTERM_KEY_HOME;      break;
        case WXK_END:      key = VTERM_KEY_END;       break;
        case WXK_PAGEUP:   key = VTERM_KEY_PAGEUP;    break;
        case WXK_PAGEDOWN: key = VTERM_KEY_PAGEDOWN;  break;
        case WXK_F1: case WXK_F2: case WXK_F3: case WXK_F4:
        case WXK_F5: case WXK_F6: case WXK_F7: case WXK_F8:
        case WXK_F9: case WXK_F10: case WXK_F11: case WXK_F12:
            key = static_cast<VTermKey>(
                VTERM_KEY_FUNCTION_0 + (evt.GetKeyCode() - WXK_F1 + 1));
            break;
        default:
            evt.Skip();     // let OnChar handle regular characters
            return;
    }

    if (key != VTERM_KEY_NONE)
        vterm_keyboard_key(m_vt, key, mod);
}

void TerminalPanel::OnTimer(wxTimerEvent&) {
    ReadPTY();
}

void TerminalPanel::OnFocus(wxFocusEvent& evt) {
    Refresh();
    evt.Skip();
}

void TerminalPanel::OnMouseWheel(wxMouseEvent& evt) {
    // Accumulate fractional scroll for smooth high-res trackpads
    m_wheelAccum += evt.GetWheelRotation();
    int steps = m_wheelAccum / evt.GetWheelDelta();
    if (steps == 0) return;
    m_wheelAccum -= steps * evt.GetWheelDelta();

    int maxScroll = static_cast<int>(m_scrollback.size());
    m_scrollOffset = std::clamp(m_scrollOffset + steps * 3, 0, maxScroll);
    UpdateScrollbar();
    Refresh();
}

void TerminalPanel::OnScrollbar(wxScrollEvent&) {
    int pos = m_scrollbar->GetThumbPosition();
    int maxScroll = static_cast<int>(m_scrollback.size());
    // Scrollbar 0 = top of scrollback, max = bottom (live)
    m_scrollOffset = maxScroll - pos;
    Refresh();
}

void TerminalPanel::UpdateScrollbar() {
    int sbSize = static_cast<int>(m_scrollback.size());
    int range = sbSize + m_rows;
    int thumbSize = m_rows;
    int pos = sbSize - m_scrollOffset;
    m_scrollbar->SetScrollbar(pos, thumbSize, range, thumbSize);
}

// ============================================================================
// VTerm callbacks
// ============================================================================

int TerminalPanel::OnVtDamage(VTermRect, void*) {
    return 0;
}

int TerminalPanel::OnVtMoveCursor(VTermPos pos, VTermPos, int visible, void* user) {
    auto* self = static_cast<TerminalPanel*>(user);
    self->m_cursorPos     = pos;
    self->m_cursorVisible = visible;
    return 0;
}

int TerminalPanel::OnVtBell(void*) {
    wxBell();
    return 0;
}

int TerminalPanel::OnVtSbPushLine(int cols, const VTermScreenCell* cells, void* user) {
    auto* self = static_cast<TerminalPanel*>(user);

    ScrollbackLine line;
    line.cells.assign(cells, cells + cols);
    self->m_scrollback.push_back(std::move(line));

    // Cap scrollback size
    if (static_cast<int>(self->m_scrollback.size()) > MAX_SCROLLBACK)
        self->m_scrollback.erase(self->m_scrollback.begin());

    return 0;
}

int TerminalPanel::OnVtSbPopLine(int cols, VTermScreenCell* cells, void* user) {
    auto* self = static_cast<TerminalPanel*>(user);
    if (self->m_scrollback.empty()) return 0;

    auto& line = self->m_scrollback.back();
    int copyLen = std::min(cols, static_cast<int>(line.cells.size()));
    memcpy(cells, line.cells.data(), sizeof(VTermScreenCell) * copyLen);
    // Zero remaining cells
    if (copyLen < cols)
        memset(cells + copyLen, 0, sizeof(VTermScreenCell) * (cols - copyLen));

    self->m_scrollback.pop_back();
    return 1;
}

void TerminalPanel::OnVtOutput(const char* s, size_t len, void* user) {
    auto* self = static_cast<TerminalPanel*>(user);
    if (self->m_masterFd >= 0)
        ::write(self->m_masterFd, s, len);
}
