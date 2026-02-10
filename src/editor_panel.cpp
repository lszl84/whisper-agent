#include "editor_panel.h"
#include <wx/filename.h>

EditorPanel::EditorPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    SetBackgroundColour(wxColour(30, 30, 30));

    m_pathLabel = new wxStaticText(this, wxID_ANY, " No file open");
    m_pathLabel->SetForegroundColour(wxColour(140, 140, 140));
    m_pathLabel->SetFont(m_pathLabel->GetFont().Bold());

    m_stc = new wxStyledTextCtrl(this, wxID_ANY);
    m_stc->SetReadOnly(true);
    SetupStyles();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_pathLabel, 0, wxEXPAND | wxALL, 4);
    sizer->Add(m_stc,       1, wxEXPAND);
    SetSizer(sizer);
}

// ---------------------------------------------------------------------------
// Dark-theme Scintilla defaults
// ---------------------------------------------------------------------------

void EditorPanel::SetupStyles() {
    wxFont mono(wxFontInfo(11).Family(wxFONTFAMILY_TELETYPE).FaceName("Monospace"));

    m_stc->StyleSetBackground(wxSTC_STYLE_DEFAULT, wxColour(30, 30, 30));
    m_stc->StyleSetForeground(wxSTC_STYLE_DEFAULT, wxColour(204, 204, 204));
    m_stc->StyleSetFont(wxSTC_STYLE_DEFAULT, mono);
    m_stc->StyleClearAll();   // propagate defaults to all styles

    // Line numbers
    m_stc->SetMarginType(0, wxSTC_MARGIN_NUMBER);
    m_stc->SetMarginWidth(0, 50);
    m_stc->StyleSetBackground(wxSTC_STYLE_LINENUMBER, wxColour(37, 37, 38));
    m_stc->StyleSetForeground(wxSTC_STYLE_LINENUMBER, wxColour(100, 100, 100));

    // Hide fold margin
    m_stc->SetMarginWidth(1, 0);

    // Caret & current line
    m_stc->SetCaretForeground(wxColour(200, 200, 200));
    m_stc->SetCaretLineVisible(true);
    m_stc->SetCaretLineBackground(wxColour(40, 44, 52));

    // Selection
    m_stc->SetSelBackground(true, wxColour(51, 90, 161));

    // Tabs
    m_stc->SetTabWidth(4);
    m_stc->SetUseTabs(false);
    m_stc->SetIndent(4);

    m_stc->SetWrapMode(wxSTC_WRAP_WORD);
    // No horizontal scrollbar needed with word wrap
    m_stc->SetUseHorizontalScrollBar(false);
}

// ---------------------------------------------------------------------------
// Load file
// ---------------------------------------------------------------------------

void EditorPanel::LoadFile(const wxString& path) {
    m_pathLabel->SetLabel(" " + path);

    m_stc->SetReadOnly(false);
    m_stc->ClearAll();
    ApplyLexer(path);
    m_stc->LoadFile(path);
    m_stc->SetReadOnly(true);
    m_stc->GotoLine(0);
}

// ---------------------------------------------------------------------------
// Syntax highlighting per extension
// ---------------------------------------------------------------------------

void EditorPanel::ApplyCppLexerStyles(const char* keywords) {
    m_stc->SetLexer(wxSTC_LEX_CPP);
    m_stc->SetKeyWords(0, keywords);
    m_stc->StyleSetForeground(wxSTC_C_COMMENT,      wxColour(106, 153,  85));
    m_stc->StyleSetForeground(wxSTC_C_COMMENTLINE,   wxColour(106, 153,  85));
    m_stc->StyleSetForeground(wxSTC_C_COMMENTDOC,    wxColour(106, 153,  85));
    m_stc->StyleSetForeground(wxSTC_C_NUMBER,        wxColour(181, 206, 168));
    m_stc->StyleSetForeground(wxSTC_C_WORD,          wxColour( 86, 156, 214));
    m_stc->StyleSetForeground(wxSTC_C_STRING,        wxColour(206, 145, 120));
    m_stc->StyleSetForeground(wxSTC_C_CHARACTER,     wxColour(206, 145, 120));
    m_stc->StyleSetForeground(wxSTC_C_PREPROCESSOR,  wxColour(155, 155, 155));
    m_stc->StyleSetForeground(wxSTC_C_OPERATOR,      wxColour(204, 204, 204));
    m_stc->StyleSetForeground(wxSTC_C_IDENTIFIER,    wxColour(204, 204, 204));
    m_stc->StyleSetBold(wxSTC_C_WORD, true);
}

void EditorPanel::ApplyLexer(const wxString& path) {
    wxFileName fn(path);
    wxString ext = fn.GetExt().Lower();

    // ---- C / C++ ----
    if (ext == "c" || ext == "cpp" || ext == "cc" || ext == "cxx" ||
        ext == "h" || ext == "hpp" || ext == "hxx")
    {
        ApplyCppLexerStyles(
            "auto break case char const continue default do double else enum extern "
            "float for goto if inline int long register restrict return short signed "
            "sizeof static struct switch typedef union unsigned void volatile while "
            "class namespace template typename this virtual override final public "
            "private protected using new delete throw try catch constexpr decltype "
            "noexcept nullptr static_assert thread_local alignas alignof "
            "bool true false include define ifdef ifndef endif pragma");
        return;
    }

    // ---- C# ----
    if (ext == "cs") {
        ApplyCppLexerStyles(
            "abstract as async await base bool break byte case catch char checked "
            "class const continue decimal default delegate do double else enum event "
            "explicit extern false finally fixed float for foreach get goto if "
            "implicit in int interface internal is lock long namespace new null "
            "object operator out override params partial private protected public "
            "readonly ref return sbyte sealed set short sizeof stackalloc static "
            "string struct switch this throw true try typeof uint ulong unchecked "
            "unsafe ushort using var virtual void volatile where while yield");
        return;
    }

    // ---- Java ----
    if (ext == "java") {
        ApplyCppLexerStyles(
            "abstract assert boolean break byte case catch char class const continue "
            "default do double else enum extends final finally float for goto if "
            "implements import instanceof int interface long native new null package "
            "private protected public return short static strictfp super switch "
            "synchronized this throw throws transient try void volatile while "
            "true false var record sealed permits");
        return;
    }

    // ---- JavaScript / TypeScript ----
    if (ext == "js" || ext == "jsx" || ext == "ts" || ext == "tsx" || ext == "mjs") {
        ApplyCppLexerStyles(
            "async await break case catch class const continue debugger default "
            "delete do else enum export extends false finally for from function get "
            "if import in instanceof let new null of return set static super switch "
            "this throw true try typeof undefined var void while with yield "
            "type interface declare module namespace abstract as implements "
            "private protected public readonly");
        return;
    }

    // ---- Go ----
    if (ext == "go") {
        ApplyCppLexerStyles(
            "break case chan const continue default defer else fallthrough for func "
            "go goto if import interface map package range return select struct "
            "switch type var bool byte complex64 complex128 error float32 float64 "
            "int int8 int16 int32 int64 rune string uint uint8 uint16 uint32 "
            "uint64 uintptr true false nil iota append cap close copy delete len "
            "make new panic print println recover any");
        return;
    }

    // ---- Rust ----
    if (ext == "rs") {
        ApplyCppLexerStyles(
            "as async await break const continue crate dyn else enum extern false "
            "fn for if impl in let loop match mod move mut pub ref return self "
            "Self static struct super trait true type unsafe use where while yield "
            "bool char i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize "
            "f32 f64 str String Vec Box Option Result Some None Ok Err");
        return;
    }

    // ---- Swift ----
    if (ext == "swift") {
        ApplyCppLexerStyles(
            "actor associatedtype async await break case catch class continue "
            "default defer deinit do else enum extension fallthrough false final "
            "for func get guard if import in init inout is let nil operator override "
            "private protocol public repeat return self set some static struct "
            "subscript super switch throw throws true try typealias var where while");
        return;
    }

    // ---- Python ----
    if (ext == "py" || ext == "pyw") {
        m_stc->SetLexer(wxSTC_LEX_PYTHON);
        m_stc->SetKeyWords(0,
            "and as assert async await break class continue def del elif else except "
            "finally for from global if import in is lambda nonlocal not or pass "
            "raise return try while with yield True False None");

        m_stc->StyleSetForeground(wxSTC_P_COMMENTLINE, wxColour(106, 153,  85));
        m_stc->StyleSetForeground(wxSTC_P_NUMBER,      wxColour(181, 206, 168));
        m_stc->StyleSetForeground(wxSTC_P_STRING,      wxColour(206, 145, 120));
        m_stc->StyleSetForeground(wxSTC_P_WORD,        wxColour( 86, 156, 214));
        m_stc->StyleSetForeground(wxSTC_P_DEFNAME,     wxColour(220, 220, 170));
        m_stc->StyleSetForeground(wxSTC_P_CLASSNAME,   wxColour( 78, 201, 176));
        m_stc->StyleSetBold(wxSTC_P_WORD, true);
        return;
    }

    // ---- HTML / XML / Razor ----
    if (ext == "html" || ext == "htm" || ext == "xhtml" ||
        ext == "xml"  || ext == "svg" || ext == "xaml" ||
        ext == "cshtml" || ext == "razor" || ext == "aspx" ||
        ext == "vue" || ext == "svelte" || ext == "php" ||
        ext == "csproj" || ext == "fsproj" || ext == "vbproj" ||
        ext == "props"  || ext == "targets" || ext == "resx" || ext == "config")
        { m_stc->SetLexer(wxSTC_LEX_HTML); return; }

    // ---- CSS ----
    if (ext == "css" || ext == "scss" || ext == "less")
        { m_stc->SetLexer(wxSTC_LEX_CSS); return; }

    // ---- Shell ----
    if (ext == "sh" || ext == "bash" || ext == "zsh")
        { m_stc->SetLexer(wxSTC_LEX_BASH); return; }

    // ---- Markdown ----
    if (ext == "md" || ext == "markdown")
        { m_stc->SetLexer(wxSTC_LEX_MARKDOWN); return; }

    // ---- CMake ----
    if (ext == "cmake" || fn.GetFullName() == "CMakeLists.txt")
        { m_stc->SetLexer(wxSTC_LEX_CMAKE); return; }

    // ---- JSON ----
    if (ext == "json")
        { m_stc->SetLexer(wxSTC_LEX_JSON); return; }

    // ---- YAML ----
    if (ext == "yaml" || ext == "yml")
        { m_stc->SetLexer(wxSTC_LEX_YAML); return; }

    // ---- SQL ----
    if (ext == "sql")
        { m_stc->SetLexer(wxSTC_LEX_SQL); return; }

    // ---- Fallback: plain text ----
    m_stc->SetLexer(wxSTC_LEX_NULL);
}
