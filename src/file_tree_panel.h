#pragma once

#include <wx/wx.h>
#include <wx/treectrl.h>

// Custom event: fired when user double-clicks a file
wxDECLARE_EVENT(EVT_FILE_SELECTED, wxCommandEvent);

class FileTreePanel : public wxPanel {
public:
    FileTreePanel(wxWindow* parent, const wxString& rootDir);
    void SetRootDir(const wxString& dir);

private:
    void PopulateChildren(const wxTreeItemId& parent, const wxString& path);
    void OnItemExpanding(wxTreeEvent& evt);
    void OnItemActivated(wxTreeEvent& evt);

    wxTreeCtrl* m_tree = nullptr;
    wxString    m_rootDir;

    // Per-item data stored via wxTreeItemData
    struct ItemData : public wxTreeItemData {
        wxString fullPath;
        bool     isDir;
        bool     populated = false;
        ItemData(const wxString& p, bool d) : fullPath(p), isDir(d) {}
    };
};
