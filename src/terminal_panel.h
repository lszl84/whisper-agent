#pragma once

#include <wx/wx.h>
#include <wx/scrolbar.h>
#include <vterm.h>
#include <sys/types.h>
#include <vector>

class TerminalPanel : public wxPanel {
public:
    TerminalPanel(wxWindow* parent, const wxString& command = "bash");
    ~TerminalPanel();

    /// Write text directly to the PTY as if the user typed it.
    void InjectText(const std::string& text);

private:
    // wx event handlers
    void OnPaint(wxPaintEvent& evt);
    void OnSize(wxSizeEvent& evt);
    void OnChar(wxKeyEvent& evt);
    void OnKeyDown(wxKeyEvent& evt);
    void OnTimer(wxTimerEvent& evt);
    void OnFocus(wxFocusEvent& evt);
    void OnMouseWheel(wxMouseEvent& evt);
    void OnScrollbar(wxScrollEvent& evt);
    void UpdateScrollbar();

    // PTY helpers
    bool SpawnChild(const wxString& command);
    void ReadPTY();
    void RecalcCellSize();
    void ResizeTerminal();

    // Rendering
public:
    wxColour VTermColorToWx(VTermColor col, bool isFg);
private:

    // VTerm screen callbacks (static, user-data = this)
    static int  OnVtDamage(VTermRect rect, void* user);
    static int  OnVtMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
    static int  OnVtBell(void* user);
    static int  OnVtSbPushLine(int cols, const VTermScreenCell* cells, void* user);
    static int  OnVtSbPopLine(int cols, VTermScreenCell* cells, void* user);
    // VTerm output callback — keyboard → PTY
    static void OnVtOutput(const char* s, size_t len, void* user);

    // VTerm state
    VTerm*                m_vt         = nullptr;
    VTermScreen*          m_vtScreen   = nullptr;
    VTermScreenCallbacks  m_screenCbs  = {};

    // PTY state
    int    m_masterFd  = -1;
    pid_t  m_childPid  = -1;

    // Grid geometry
    int  m_rows  = 24;
    int  m_cols  = 80;
    int  m_cellW = 8;
    int  m_cellH = 16;

    // Cursor
    VTermPos m_cursorPos     = {0, 0};
    bool     m_cursorVisible = true;

    // Scrollback buffer
    struct ScrollbackLine {
        std::vector<VTermScreenCell> cells;
    };
    std::vector<ScrollbackLine> m_scrollback;
    int  m_scrollOffset = 0;         // 0 = bottom, >0 = scrolled up
    static constexpr int MAX_SCROLLBACK = 2000;

    wxTimer      m_pollTimer;
    wxFont       m_font;
    wxFont       m_fontBold;
    wxScrollBar* m_scrollbar  = nullptr;
    int          m_wheelAccum = 0;
};
