#include <wx/wx.h>
#include "main_frame.h"

class WhisperAgentApp : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new MainFrame();
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(WhisperAgentApp);
