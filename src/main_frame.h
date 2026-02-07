#pragma once

#include <wx/wx.h>
#include <wx/splitter.h>

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
    MainFrame();
    ~MainFrame();

private:
    void CreateUI();

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
};
