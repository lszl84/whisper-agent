#pragma once

#include <wx/wx.h>
#include <wx/stc/stc.h>

class EditorPanel : public wxPanel {
public:
    EditorPanel(wxWindow* parent);

    /// Load and display a file (read-only).
    void LoadFile(const wxString& path);

private:
    void SetupStyles();
    void ApplyLexer(const wxString& path);
    void ApplyCppLexerStyles(const char* keywords);

    wxStyledTextCtrl* m_stc       = nullptr;
    wxStaticText*     m_pathLabel = nullptr;
};
