#include "MainWindow.h"
#include "Core/WebsiteBlocker.h"
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shlwapi.h>
#include <string>
#include <iterator>

namespace {
constexpr wchar_t kClassName[] = L"HimayaMainWindow";
enum : int { ID_CATEGORY=1001, ID_LIST=1002, ID_INPUT=1003, ID_ADD=1004, ID_REMOVE=1005,
             ID_REFRESH=1006, ID_PROFILE=1007, ID_DURATION=1008, ID_APPLY_SITES=1009, ID_RESTORE_SITES=1010, ID_BROWSE=1011, ID_REPORT=1012, ID_RESTORE_ORIGINAL_HOSTS=1013 };
HWND makeControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
                 int x,int y,int w,int h,int id,HINSTANCE inst) {
    return CreateWindowExW(0, cls, text, WS_CHILD|WS_VISIBLE|style, x,y,w,h,parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),inst,nullptr);
}
HBRUSH backgroundBrush() { static HBRUSH brush=CreateSolidBrush(RGB(18,23,32)); return brush; }
HBRUSH inputBrush() { static HBRUSH brush=CreateSolidBrush(RGB(31,39,52)); return brush; }
HFONT uiFont() {
    static HFONT font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    return font;
}
void setFont(HWND control) { SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE); }
}

MainWindow::MainWindow(HINSTANCE instance) : instance_(instance) {}

bool MainWindow::create(int showCommand) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc; wc.hInstance = instance_; wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = backgroundBrush();
    wc.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    hwnd_ = CreateWindowExW(0, kClassName, L"Himaya Security", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1060, 820, nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    const BOOL darkTitleBar=TRUE;
    DwmSetWindowAttribute(hwnd_,20,&darkTitleBar,sizeof(darkTitleBar));
    ShowWindow(hwnd_, showCommand); UpdateWindow(hwnd_); return true;
}

int MainWindow::run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    return self ? self->handleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

void MainWindow::setStatus(const std::wstring& value) { if (status_) SetWindowTextW(status_, value.c_str()); }

LRESULT MainWindow::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        auto title = makeControl(hwnd_, L"STATIC", L"HIMAYA  |  DIGITAL WELLNESS PROTECTION", SS_LEFT, 28,18,960,42,0,instance_);
        setFont(title);
        auto subtitle = makeControl(hwnd_, L"STATIC", L"Manage application, website, and folder protection — Version 0.5.0", SS_LEFT, 30,60,950,28,0,instance_);
        setFont(subtitle);
        settings_ = makeControl(hwnd_, L"STATIC", L"Protect applications, websites, and folders with locally managed, reversible rules", SS_LEFT, 30,98,960,28,0,instance_);
        setFont(settings_);

        makeControl(hwnd_, L"STATIC", L"List type", SS_LEFT, 30,145,120,24,0,instance_);
        category_ = makeControl(hwnd_, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST|WS_VSCROLL, 30,172,220,220,ID_CATEGORY,instance_);
        SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Applications"));
        SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Websites"));
        SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Folders"));
        SendMessageW(category_, CB_SETCURSEL, 0, 0);

        makeControl(hwnd_, L"STATIC", L"Protection profile", SS_LEFT, 275,145,150,24,0,instance_);
        profile_ = makeControl(hwnd_, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST|WS_VSCROLL, 275,172,230,220,ID_PROFILE,instance_);
        SendMessageW(profile_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Basic"));
        SendMessageW(profile_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Moderate"));
        SendMessageW(profile_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Strict"));
        SendMessageW(profile_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Intensive recovery"));
        const auto active = store_.getSetting(L"active_profile", L"simple");
        int profileIndex = active == L"moderate" ? 1 : active == L"severe" ? 2 : active == L"recovery" ? 3 : 0;
        SendMessageW(profile_, CB_SETCURSEL, profileIndex, 0);

        makeControl(hwnd_, L"STATIC", L"Application lock duration", SS_LEFT, 530,145,170,24,0,instance_);
        duration_ = makeControl(hwnd_, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST|WS_VSCROLL, 530,172,230,220,ID_DURATION,instance_);
        SendMessageW(duration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Long-term — no automatic expiry"));
        SendMessageW(duration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1 hour"));
        SendMessageW(duration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"24 hours"));
        SendMessageW(duration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"7 days"));
        SendMessageW(duration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"30 days"));
        SendMessageW(duration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"90 days"));
        SendMessageW(duration_, CB_SETCURSEL, 0, 0);

        input_ = makeControl(hwnd_, L"EDIT", L"", WS_BORDER|ES_AUTOHSCROLL, 30,220,610,36,ID_INPUT,instance_);
        browse_ = makeControl(hwnd_, L"BUTTON", L"Browse...", BS_PUSHBUTTON, 650,220,130,36,ID_BROWSE,instance_);
        makeControl(hwnd_, L"BUTTON", L"Add to list", BS_PUSHBUTTON, 790,220,200,36,ID_ADD,instance_);
        list_ = makeControl(hwnd_, WC_LISTBOXW, L"", WS_BORDER|LBS_NOTIFY|WS_VSCROLL, 30,275,960,300,ID_LIST,instance_);
        makeControl(hwnd_, L"BUTTON", L"Remove selected", BS_PUSHBUTTON, 30,590,220,36,ID_REMOVE,instance_);
        makeControl(hwnd_, L"BUTTON", L"Refresh list", BS_PUSHBUTTON, 265,590,140,36,ID_REFRESH,instance_);
        makeControl(hwnd_, L"BUTTON", L"Export activity log (CSV)", BS_PUSHBUTTON, 420,590,220,36,ID_REPORT,instance_);
        applySites_ = makeControl(hwnd_, L"BUTTON", L"Apply adult and custom website blocking", BS_PUSHBUTTON, 30,640,360,38,ID_APPLY_SITES,instance_);
        restoreSites_ = makeControl(hwnd_, L"BUTTON", L"Restore website settings", BS_PUSHBUTTON, 405,640,260,38,ID_RESTORE_SITES,instance_);
        restoreOriginalHosts_ = makeControl(hwnd_, L"BUTTON", L"Restore original hosts file", BS_PUSHBUTTON, 680,640,310,38,ID_RESTORE_ORIGINAL_HOSTS,instance_);
        summary_ = makeControl(hwnd_, L"STATIC", L"Database: Loading...", SS_LEFT, 30,690,960,26,0,instance_);
        status_ = makeControl(hwnd_, L"STATIC", L"Website blocking uses the Windows hosts file. HimayaGuard enforces application and folder execution rules.", SS_LEFT, 30,725,960,40,0,instance_);
        EnumChildWindows(hwnd_, [](HWND child, LPARAM font) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
            SetWindowTheme(child,L"DarkMode_Explorer",nullptr);
            return TRUE;
        }, reinterpret_cast<LPARAM>(uiFont()));
        if (!store_.isReady()) {
            MessageBoxW(hwnd_, store_.lastError().c_str(), L"Database error", MB_OK|MB_ICONERROR);
        }
        SetWindowTextW(summary_, (L"Local database: " + store_.databasePath()).c_str());
        refreshList();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_CATEGORY: if (HIWORD(wp)==CBN_SELCHANGE) refreshList(); return 0;
        case ID_PROFILE: if (HIWORD(wp)==CBN_SELCHANGE) saveProfile(); return 0;
        case ID_ADD: addEntry(); return 0;
        case ID_BROWSE: browseEntry(); return 0;
        case ID_REMOVE: removeEntry(); return 0;
        case ID_REFRESH: refreshList(); return 0;
        case ID_REPORT: exportReport(); return 0;
        case ID_APPLY_SITES: applyWebsites(); return 0;
        case ID_RESTORE_SITES: restoreWebsites(); return 0;
        case ID_RESTORE_ORIGINAL_HOSTS: restoreOriginalHosts(); return 0;
        }
        break;
    case WM_ERASEBKGND: {
        RECT rc{}; GetClientRect(hwnd_,&rc);
        FillRect(reinterpret_cast<HDC>(wp),&rc,backgroundBrush());
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc=reinterpret_cast<HDC>(wp);
        SetTextColor(dc,RGB(231,236,244)); SetBkMode(dc,TRANSPARENT);
        return reinterpret_cast<LRESULT>(backgroundBrush());
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc=reinterpret_cast<HDC>(wp);
        SetTextColor(dc,RGB(238,242,248)); SetBkColor(dc,RGB(31,39,52));
        return reinterpret_cast<LRESULT>(inputBrush());
    }
    case WM_CTLCOLORBTN: {
        HDC dc=reinterpret_cast<HDC>(wp);
        SetTextColor(dc,RGB(238,242,248)); SetBkMode(dc,TRANSPARENT);
        return reinterpret_cast<LRESULT>(backgroundBrush());
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

std::wstring MainWindow::selectedCategory() const {
    const LRESULT index = SendMessageW(category_, CB_GETCURSEL, 0, 0);
    if (index == 1) return L"websites";
    if (index == 2) return L"paths";
    return L"applications";
}

std::wstring MainWindow::selectedDuration() const {
    const LRESULT index = SendMessageW(duration_, CB_GETCURSEL, 0, 0);
    switch (index) { case 1: return L"1h"; case 2: return L"24h"; case 3: return L"7d"; case 4: return L"30d"; case 5: return L"90d"; default: return L"long"; }
}

std::wstring MainWindow::selectedProfile() const {
    const LRESULT index = SendMessageW(profile_, CB_GETCURSEL, 0, 0);
    switch (index) { case 1: return L"moderate"; case 2: return L"severe"; case 3: return L"recovery"; default: return L"simple"; }
}

void MainWindow::saveProfile() {
    if (store_.setSetting(L"active_profile", selectedProfile())) {
        store_.logEvent(L"profile_changed", selectedProfile());
        setStatus(L"Profile saved. This currently selects a profile only; it does not automatically enable different protection rules.");
    }
}

void MainWindow::refreshList() {
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    visibleEntries_ = store_.load(selectedCategory());
    for (const auto& entry : visibleEntries_) {
        std::wstring label = entry.value;
        if (selectedCategory() == L"applications") {
            label += entry.lockMode == L"long" ? L"   |   Long-term lock (enforced by the protection service)" : L"   |   Temporary lock (enforced by the protection service)";
        }
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (applySites_) EnableWindow(applySites_, selectedCategory() == L"websites");
    if (restoreSites_) EnableWindow(restoreSites_, selectedCategory() == L"websites");
    if (restoreOriginalHosts_) EnableWindow(restoreOriginalHosts_, selectedCategory() == L"websites");
    if (browse_) EnableWindow(browse_, selectedCategory() != L"websites");
}

void MainWindow::browseEntry() {
    const auto category=selectedCategory();
    if(category==L"applications") {
        wchar_t path[32768]{};
        OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=hwnd_;
        dialog.lpstrFilter=L"Executable files (*.exe)\0*.exe\0All files (*.*)\0*.*\0\0";
        dialog.lpstrFile=path; dialog.nMaxFile=static_cast<DWORD>(std::size(path));
        dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_HIDEREADONLY;
        dialog.lpstrTitle=L"Select an application to block";
        if(GetOpenFileNameW(&dialog)) SetWindowTextW(input_,path);
        return;
    }
    if(category==L"paths") {
        BROWSEINFOW dialog{}; dialog.hwndOwner=hwnd_; dialog.lpszTitle=L"Select a folder in which to block file execution";
        dialog.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE|BIF_EDITBOX;
        PIDLIST_ABSOLUTE item=SHBrowseForFolderW(&dialog);
        if(item) {
            wchar_t path[MAX_PATH]{};
            if(SHGetPathFromIDListW(item,path)) SetWindowTextW(input_,path);
            CoTaskMemFree(item);
        }
    }
}

void MainWindow::addEntry() {
    wchar_t value[2048]{};
    GetWindowTextW(input_, value, static_cast<int>(std::size(value)));
    if (!value[0]) { MessageBoxW(hwnd_, L"Enter an application path, website, or folder path first.", L"Himaya", MB_OK|MB_ICONINFORMATION); return; }
    const auto category = selectedCategory();
    const DWORD attributes=GetFileAttributesW(value);
    if(category==L"applications" &&
       (attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY))) {
        MessageBoxW(hwnd_,L"Select an existing EXE file using the Browse button.",L"Invalid application path",MB_OK|MB_ICONWARNING);
        return;
    }
    if(category==L"paths" &&
       (attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY))) {
        MessageBoxW(hwnd_,L"Select an existing folder using the Browse button.",L"Invalid folder path",MB_OK|MB_ICONWARNING);
        return;
    }
    if(category==L"paths") {
        wchar_t executable[MAX_PATH]{};
        if(GetModuleFileNameW(nullptr,executable,MAX_PATH) && PathIsPrefixW(value,executable)) {
            MessageBoxW(hwnd_,L"You cannot block a folder that contains Himaya itself.",L"Folder not allowed",MB_OK|MB_ICONWARNING);
            return;
        }
        const int answer=MessageBoxW(hwnd_,
            L"Himaya will deny Windows user accounts access to this folder and its contents, not just prevent files from running. The original folder permissions will be saved and restored when the rule is removed or the protection service stops. Do not select Windows system folders, a drive root, or your main user folder. Continue?",
            L"Confirm folder access restriction",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2);
        if(answer!=IDYES) return;
    }
    const auto duration = category == L"applications" ? selectedDuration() : L"long";
    if (store_.addEntry(category, value, duration, selectedProfile())) {
        SetWindowTextW(input_, L""); refreshList();
        setStatus(category == L"websites" ? L"Website saved. Click Apply website blocking to activate the rules with administrator privileges." :
            category == L"paths" ? L"Folder saved. The service will restrict access and file execution, while preserving the original permissions for restoration." :
            L"Application saved. HimayaGuard will automatically enforce the execution block.");
    } else {
        MessageBoxW(hwnd_, store_.lastError().empty() ? L"Could not save the item." : store_.lastError().c_str(), L"Himaya", MB_OK|MB_ICONERROR);
    }
}

void MainWindow::removeEntry() {
    const LRESULT index = SendMessageW(list_, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR || static_cast<size_t>(index) >= visibleEntries_.size()) return;
    if (store_.removeEntry(selectedCategory(), visibleEntries_[static_cast<size_t>(index)].value)) {
        refreshList(); setStatus(L"Rule removed. The service will remove the execution block and restore folder permissions during its next synchronization.");
    }
}

void MainWindow::applyWebsites() {
    if (selectedCategory() != L"websites") return;
    const int answer = MessageBoxW(hwnd_,
        L"The Windows hosts file will be updated with the community adult-domain list and your custom websites. The list may not include every website, and DNS or VPN tools may bypass it. Continue?",
        L"Confirm website blocking", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer != IDYES) return;
    std::vector<std::wstring> domains;
    for(const auto& entry:store_.load(L"websites")) domains.push_back(entry.value);
    const auto result=himaya::WebsiteBlocker::apply(domains,store_.getSetting(L"adult_block_mode",L"all")==L"all");
    if(result.success) store_.logEvent(L"websites_applied",std::to_wstring(result.blockedDomains));
    std::wstring message=result.message;
    if(result.success && result.blockedDomains) message+=L"\nDomains blocked: "+std::to_wstring(result.blockedDomains);
    setStatus(message);
    MessageBoxW(hwnd_,message.c_str(),L"Himaya Security",MB_OK|(result.success?MB_ICONINFORMATION:MB_ICONERROR));
}


void MainWindow::exportReport() {
    wchar_t path[MAX_PATH] = L"Himaya-Activity-Report.csv";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrDefExt = L"csv";
    dialog.lpstrTitle = L"Export protection activity log";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) return;
    if (!store_.exportEventsCsv(path)) {
        MessageBoxW(hwnd_, L"Could not export the activity log. Check write permissions and available disk space.",
                    L"Himaya Security", MB_OK | MB_ICONERROR);
        return;
    }
    store_.logEvent(L"report_exported", path);
    setStatus(L"The latest 5,000 events were exported to a CSV file.");
    MessageBoxW(hwnd_, L"Activity log exported successfully.", L"Himaya Security",
                MB_OK | MB_ICONINFORMATION);
}

void MainWindow::restoreOriginalHosts() {
    if (selectedCategory() != L"websites") return;
    const int answer = MessageBoxW(hwnd_,
        L"This will replace the entire current Windows hosts file with the original backup created by Himaya before its first change. Any hosts-file edits made after that backup (including edits made by other programs or by you) will be lost. The backup will be kept. Continue?",
        L"Confirm original hosts file restoration", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer != IDYES) return;

    const auto result = himaya::WebsiteBlocker::restoreOriginal();
    if (result.success) store_.logEvent(L"hosts_original_restored", result.message);
    setStatus(result.message);
    MessageBoxW(hwnd_, result.message.c_str(), L"Himaya Security",
                MB_OK | (result.success ? MB_ICONINFORMATION : MB_ICONERROR));
}
void MainWindow::restoreWebsites() {
    if (selectedCategory() != L"websites") return;
    const int answer = MessageBoxW(hwnd_,
        L"Only Himaya-managed rules will be removed from the hosts file. All other entries will be preserved. Continue?",
        L"Confirm website settings restoration", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer != IDYES) return;
    const auto result=himaya::WebsiteBlocker::restore();
    if(result.success) store_.logEvent(L"websites_restored",result.message);
    setStatus(result.message);
    MessageBoxW(hwnd_,result.message.c_str(),L"Himaya Security",MB_OK|(result.success?MB_ICONINFORMATION:MB_ICONERROR));
}
