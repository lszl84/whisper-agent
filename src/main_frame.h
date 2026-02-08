#pragma once

#include <wx/wx.h>
#include <wx/splitter.h>
#include <wx/fileconf.h>
#include <vector>

#include "terminal_panel.h"
#include "file_tree_panel.h"
#include "editor_panel.h"
#include "transcriber.h"

// ---------------------------------------------------------------------------
// Overlay dialog shown during voice transcription
// ---------------------------------------------------------------------------

class TranscriptionDialog : public wxDialog {
public:
    TranscriptionDialog(wxWindow* parent);

    void UpdateText(const wxString& text);
    void Finalize();                          // recording done — let user edit & send
    wxString GetText() const;

private:
    wxTextCtrl*   m_text     = nullptr;
    wxStaticText* m_status   = nullptr;
    wxButton*     m_stopBtn  = nullptr;
    wxButton*     m_sendBtn  = nullptr;
};

// ---------------------------------------------------------------------------
// Main application frame
// ---------------------------------------------------------------------------

class MainFrame : public wxFrame {
public:
    MainFrame(const wxString& command = "");
    ~MainFrame();

private:
    void CreateUI(const wxString& command);
    void CreateMenuBar();

    // Menu handlers
    void OnOpenFolder(wxCommandEvent& evt);
    void OnOpenRecent(wxCommandEvent& evt);
    void OnClearRecent(wxCommandEvent& evt);
    void OnQuit(wxCommandEvent& evt);

    // Folder management
    void OpenFolder(const wxString& path);
    void AddRecentFolder(const wxString& path);
    void LoadRecentFolders();
    void SaveRecentFolders();
    void RebuildRecentMenu();

    // Toolbar
    void OnRecord(wxCommandEvent& evt);

    // File tree
    void OnFileSelected(wxCommandEvent& evt);

    // Transcription events (from background thread → main thread)
    void OnTranscription(wxThreadEvent& evt);

    // Dialog button handlers
    void OnDlgStop(wxCommandEvent& evt);
    void OnDlgSend(wxCommandEvent& evt);
    void OnDlgCancel(wxCommandEvent& evt);
    void OnDlgClose(wxCloseEvent& evt);
    void CloseDialog();

    FileTreePanel*  m_fileTree  = nullptr;
    EditorPanel*    m_editor    = nullptr;
    TerminalPanel*  m_terminal  = nullptr;
    Transcriber     m_transcriber;
    wxButton*       m_recordBtn = nullptr;

    TranscriptionDialog* m_dlg = nullptr;
    wxTimer              m_enterTimer;

    // Recent folders
    wxMenu*                 m_recentMenu = nullptr;
    std::vector<wxString>   m_recentFolders;
    static constexpr int    MAX_RECENT = 10;
    static constexpr int    ID_RECENT_BASE = wxID_HIGHEST + 100;
    static constexpr int    ID_CLEAR_RECENT = wxID_HIGHEST + 200;
};
