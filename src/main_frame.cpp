#include "main_frame.h"

// ===================================================================
// TranscriptionDialog
// ===================================================================

TranscriptionDialog::TranscriptionDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "Transcription",
               wxDefaultPosition, wxSize(620, 240),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetMinSize(wxSize(400, 180));
    SetBackgroundColour(wxColour(45, 45, 45));

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // Status label
    m_status = new wxStaticText(this, wxID_ANY, "  Listening...");
    m_status->SetForegroundColour(wxColour(180, 180, 180));
    m_status->SetFont(m_status->GetFont().Bold());
    sizer->Add(m_status, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);

    // Editable text area (read-only while recording)
    m_text = new wxTextCtrl(this, wxID_ANY, "",
        wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxBORDER_SIMPLE);
    m_text->SetBackgroundColour(wxColour(30, 30, 30));
    m_text->SetForegroundColour(wxColour(220, 220, 220));
    m_text->SetFont(wxFont(wxFontInfo(12).Family(wxFONTFAMILY_TELETYPE)));
    m_text->SetEditable(false);
    sizer->Add(m_text, 1, wxEXPAND | wxALL, 10);

    // Buttons
    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    m_stopBtn = new wxButton(this, wxID_STOP, "Stop");
    m_sendBtn = new wxButton(this, wxID_OK, "Send");
    m_sendBtn->SetDefault();
    auto* cancelBtn = new wxButton(this, wxID_CANCEL, "Cancel");

    btnSizer->Add(m_stopBtn, 0, wxRIGHT, 4);
    btnSizer->AddStretchSpacer();
    btnSizer->Add(m_sendBtn, 0, wxRIGHT, 4);
    btnSizer->Add(cancelBtn, 0);
    sizer->Add(btnSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    SetSizer(sizer);

    // Enter → always send immediately (even while still recording)
    // Esc   → stop recording and let user edit
    m_text->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& evt) {
        if (evt.GetKeyCode() == WXK_RETURN && !evt.ShiftDown()) {
            wxCommandEvent cmd(wxEVT_BUTTON, wxID_OK);
            ProcessWindowEvent(cmd);
        } else if (evt.GetKeyCode() == WXK_ESCAPE) {
            wxCommandEvent cmd(wxEVT_BUTTON, wxID_STOP);
            ProcessWindowEvent(cmd);
        } else {
            evt.Skip();
        }
    });
}

void TranscriptionDialog::UpdateText(const wxString& text) {
    m_text->SetValue(text);
}

void TranscriptionDialog::Finalize() {
    m_status->SetLabel("  Edit then press Enter to send, Esc to cancel");
    m_stopBtn->Hide();
    m_sendBtn->Enable();
    m_sendBtn->SetDefault();
    m_text->SetEditable(true);
    m_text->SetFocus();
    m_text->SetInsertionPointEnd();
    GetSizer()->Layout();
}

wxString TranscriptionDialog::GetText() const {
    return m_text->GetValue();
}

// ===================================================================
// MainFrame
// ===================================================================

MainFrame::MainFrame(const wxString& command)
    : wxFrame(nullptr, wxID_ANY, "Whisper Agent", wxDefaultPosition, wxSize(1400, 900))
{
    SetMinSize(wxSize(800, 600));
    CreateUI(command);

    if (!m_transcriber.Init(WHISPER_MODEL_PATH)) {
        wxLogWarning("Could not load whisper model from:\n%s\n\n"
                     "Voice transcription will be unavailable.\n"
                     "The model is downloaded during CMake configure.",
                     WHISPER_MODEL_PATH);
    }

    // Background thread → main-thread event.
    // Int: 0 = partial, 1 = final.
    m_transcriber.SetCallback([this](const std::string& text, bool isFinal) {
        auto* evt = new wxThreadEvent(wxEVT_THREAD);
        evt->SetString(wxString::FromUTF8(text));
        evt->SetInt(isFinal ? 1 : 0);
        wxQueueEvent(this, evt);
    });

    Bind(EVT_FILE_SELECTED, &MainFrame::OnFileSelected, this);
    Bind(wxEVT_THREAD,      &MainFrame::OnTranscription, this);

    // Delayed Enter keypress after injecting text into the terminal
    m_enterTimer.SetOwner(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        m_terminal->InjectText("\r");
    }, m_enterTimer.GetId());

    Centre();

    // Give the terminal keyboard focus once the window is fully shown.
    CallAfter([this]() { m_terminal->SetFocus(); });
}

MainFrame::~MainFrame() {
    m_transcriber.SetCallback(nullptr);
    if (m_transcriber.IsRecording())
        m_transcriber.StopRecording();
    if (m_dlg) {
        m_dlg->Destroy();
        m_dlg = nullptr;
    }
}

// -------------------------------------------------------------------
// UI
// -------------------------------------------------------------------

void MainFrame::CreateUI(const wxString& command) {
    // Splitters
    auto* mainSplit = new wxSplitterWindow(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxSP_3D | wxSP_LIVE_UPDATE);

    m_fileTree = new FileTreePanel(mainSplit, wxGetCwd());

    // Right side: editor on top, terminal + record button on bottom
    auto* rightPanel = new wxPanel(mainSplit);
    auto* rightSizer = new wxBoxSizer(wxVERTICAL);

    auto* rightSplit = new wxSplitterWindow(
        rightPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxSP_3D | wxSP_LIVE_UPDATE);

    m_editor   = new EditorPanel(rightSplit);
    wxString cmd = command.IsEmpty() ? wxString(WHISPER_AGENT_DEFAULT_COMMAND) : command;
    m_terminal = new TerminalPanel(rightSplit, cmd);

    // Give most vertical space to the terminal
    rightSplit->SplitHorizontally(m_editor, m_terminal, 200);
    rightSplit->SetMinimumPaneSize(80);
    rightSplit->SetSashGravity(0.25);

    rightSizer->Add(rightSplit, 1, wxEXPAND);

    // Record button bar below the terminal
    auto* bottomBar = new wxPanel(rightPanel);
    bottomBar->SetBackgroundColour(wxColour(45, 45, 45));
    auto* barSizer = new wxBoxSizer(wxHORIZONTAL);

    m_recordBtn = new wxButton(bottomBar, wxID_ANY, "Record");
    m_recordBtn->SetToolTip("Record audio, transcribe, and send to terminal");
    m_recordBtn->Bind(wxEVT_BUTTON, &MainFrame::OnRecord, this);
    barSizer->Add(m_recordBtn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    auto* hint = new wxStaticText(bottomBar, wxID_ANY, "  Whisper Agent \u2014 press to dictate a command");
    hint->SetForegroundColour(wxColour(120, 120, 120));
    barSizer->Add(hint, 1, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    bottomBar->SetSizer(barSizer);
    rightSizer->Add(bottomBar, 0, wxEXPAND);

    rightPanel->SetSizer(rightSizer);

    mainSplit->SplitVertically(m_fileTree, rightPanel, 260);
    mainSplit->SetMinimumPaneSize(150);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(mainSplit, 1, wxEXPAND);
    SetSizer(sizer);

    CreateStatusBar(2);
    SetStatusText("Ready");
}

// -------------------------------------------------------------------
// Record button → open dialog
// -------------------------------------------------------------------

void MainFrame::OnRecord(wxCommandEvent&) {
    if (m_dlg) return;   // already open

    m_dlg = new TranscriptionDialog(this);

    // Bind dialog button events
    m_dlg->Bind(wxEVT_BUTTON,       &MainFrame::OnDlgStop,   this, wxID_STOP);
    m_dlg->Bind(wxEVT_BUTTON,       &MainFrame::OnDlgSend,   this, wxID_OK);
    m_dlg->Bind(wxEVT_BUTTON,       &MainFrame::OnDlgCancel, this, wxID_CANCEL);
    m_dlg->Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnDlgClose,  this);

    m_dlg->CentreOnParent();
    m_dlg->Show();
    m_recordBtn->Disable();

    m_transcriber.StartRecording();
    SetStatusText("Listening...");
}

// -------------------------------------------------------------------
// Dialog button handlers
// -------------------------------------------------------------------

void MainFrame::OnDlgStop(wxCommandEvent&) {
    if (m_transcriber.IsRecording()) {
        m_transcriber.StopRecording();
        // Final transcription will arrive via OnTranscription → Finalize()
    }
    // If already stopped, just let the user keep editing
}

void MainFrame::OnDlgSend(wxCommandEvent&) {
    if (!m_dlg) return;

    // If still recording, stop first
    if (m_transcriber.IsRecording())
        m_transcriber.StopRecording();

    wxString text = m_dlg->GetText();
    CloseDialog();
    if (!text.IsEmpty()) {
        // Write the text first
        m_terminal->InjectText(text.ToStdString(wxConvUTF8));
        SetStatusText("Sent: " + text.Left(60));
        // Send Enter after a short delay so the agent processes
        // the text before receiving the keypress
        m_enterTimer.StartOnce(150);
    }
}

void MainFrame::OnDlgCancel(wxCommandEvent&) {
    if (m_transcriber.IsRecording())
        m_transcriber.StopRecording();
    SetStatusText("Cancelled");
    CloseDialog();
}

void MainFrame::OnDlgClose(wxCloseEvent& evt) {
    if (m_transcriber.IsRecording())
        m_transcriber.StopRecording();
    CloseDialog();
    evt.Skip();
}

void MainFrame::CloseDialog() {
    if (m_dlg) {
        m_dlg->Destroy();
        m_dlg = nullptr;
    }
    m_recordBtn->Enable();
}

// -------------------------------------------------------------------
// File tree
// -------------------------------------------------------------------

void MainFrame::OnFileSelected(wxCommandEvent& evt) {
    m_editor->LoadFile(evt.GetString());
    SetStatusText(evt.GetString(), 1);
}

// -------------------------------------------------------------------
// Transcription events (partial + final)
// -------------------------------------------------------------------

void MainFrame::OnTranscription(wxThreadEvent& evt) {
    if (!m_dlg) return;

    const bool   isFinal = evt.GetInt() != 0;
    const wxString text  = evt.GetString();

    m_dlg->UpdateText(text);

    if (isFinal)
        m_dlg->Finalize();
}
