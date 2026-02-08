#include <wx/wx.h>
#include "main_frame.h"

class WhisperAgentApp : public wxApp {
public:
    bool OnInit() override {
        wxString command;
        if (argc > 1)
            command = argv[1];
        auto* frame = new MainFrame(command);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(WhisperAgentApp);
