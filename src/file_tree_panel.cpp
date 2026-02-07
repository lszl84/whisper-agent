#include "file_tree_panel.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <algorithm>
#include <vector>

wxDEFINE_EVENT(EVT_FILE_SELECTED, wxCommandEvent);

static bool IsHiddenDir(const wxString& name) {
    return name == "." || name == "..";
}

// ============================================================================

FileTreePanel::FileTreePanel(wxWindow* parent, const wxString& rootDir)
    : wxPanel(parent, wxID_ANY)
    , m_rootDir(rootDir)
{
    SetBackgroundColour(wxColour(37, 37, 38));

    auto* label = new wxStaticText(this, wxID_ANY, " FILES");
    label->SetForegroundColour(wxColour(140, 140, 140));
    label->SetFont(label->GetFont().Bold());

    m_tree = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxTR_DEFAULT_STYLE | wxTR_HAS_BUTTONS | wxTR_NO_LINES | wxTR_HIDE_ROOT);
    m_tree->SetBackgroundColour(wxColour(37, 37, 38));
    m_tree->SetForegroundColour(wxColour(204, 204, 204));

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(label,  0, wxEXPAND | wxALL, 4);
    sizer->Add(m_tree, 1, wxEXPAND);
    SetSizer(sizer);

    m_tree->Bind(wxEVT_TREE_ITEM_EXPANDING,  &FileTreePanel::OnItemExpanding, this);
    m_tree->Bind(wxEVT_TREE_SEL_CHANGED,    &FileTreePanel::OnItemActivated, this);

    SetRootDir(rootDir);
}

void FileTreePanel::SetRootDir(const wxString& dir) {
    m_rootDir = dir;
    m_tree->DeleteAllItems();
    auto root = m_tree->AddRoot("root");
    PopulateChildren(root, dir);
}

void FileTreePanel::PopulateChildren(const wxTreeItemId& parentItem, const wxString& path) {
    wxDir dir(path);
    if (!dir.IsOpened()) return;

    wxString name;
    std::vector<wxString> dirs, files;

    // Gather directories
    if (dir.GetFirst(&name, wxEmptyString, wxDIR_DIRS | wxDIR_HIDDEN)) {
        do {
            if (!IsHiddenDir(name))
                dirs.push_back(name);
        } while (dir.GetNext(&name));
    }

    // Gather files
    if (dir.GetFirst(&name, wxEmptyString, wxDIR_FILES | wxDIR_HIDDEN)) {
        do { files.push_back(name); }
        while (dir.GetNext(&name));
    }

    std::sort(dirs.begin(),  dirs.end());
    std::sort(files.begin(), files.end());

    // Directories first
    for (auto& d : dirs) {
        wxString full = path + wxFileName::GetPathSeparator() + d;
        auto* data = new ItemData(full, true);
        auto item = m_tree->AppendItem(parentItem, d, -1, -1, data);
        m_tree->AppendItem(item, "<loading...>");   // dummy child → shows expander
    }

    // Then files
    for (auto& f : files) {
        wxString full = path + wxFileName::GetPathSeparator() + f;
        auto* data = new ItemData(full, false);
        m_tree->AppendItem(parentItem, f, -1, -1, data);
    }
}

void FileTreePanel::OnItemExpanding(wxTreeEvent& evt) {
    auto item = evt.GetItem();
    auto* data = dynamic_cast<ItemData*>(m_tree->GetItemData(item));
    if (!data || !data->isDir || data->populated) return;

    data->populated = true;
    m_tree->DeleteChildren(item);
    PopulateChildren(item, data->fullPath);
}

void FileTreePanel::OnItemActivated(wxTreeEvent& evt) {
    auto item = evt.GetItem();
    auto* data = dynamic_cast<ItemData*>(m_tree->GetItemData(item));
    if (!data || data->isDir) return;

    // Post event to the top-level frame
    wxCommandEvent fileEvt(EVT_FILE_SELECTED);
    fileEvt.SetString(data->fullPath);
    wxPostEvent(wxGetTopLevelParent(this), fileEvt);
}
