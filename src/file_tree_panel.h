#pragma once

#include <wx/wx.h>
#include <wx/treectrl.h>
#include <wx/fswatcher.h>
#include <vector>

// Custom event: fired when user double-clicks a file
wxDECLARE_EVENT(EVT_FILE_SELECTED, wxCommandEvent);

class FileTreePanel : public wxPanel {
public:
    FileTreePanel(wxWindow* parent, const wxString& rootDir);
    ~FileTreePanel();
    void SetRootDir(const wxString& dir);

private:
    void PopulateChildren(const wxTreeItemId& parent, const wxString& path);
    void OnItemExpanding(wxTreeEvent& evt);
    void OnItemActivated(wxTreeEvent& evt);

    void StartWatching();
    void OnFileSystemEvent(wxFileSystemWatcherEvent& evt);
    void OnRefreshTimer(wxTimerEvent& evt);
    void OnPollTimer(wxTimerEvent& evt);
    std::vector<wxString> GetExpandedPaths();
    void RestoreExpandedPaths(const std::vector<wxString>& paths);

    wxTreeCtrl*            m_tree = nullptr;
    wxString               m_rootDir;
    wxFileSystemWatcher*   m_watcher = nullptr;
    wxTimer                m_refreshTimer;
    wxTimer                m_pollTimer;

    // Per-item data stored via wxTreeItemData
    struct ItemData : public wxTreeItemData {
        wxString fullPath;
        bool     isDir;
        bool     populated = false;
        ItemData(const wxString& p, bool d) : fullPath(p), isDir(d) {}
    };
};
