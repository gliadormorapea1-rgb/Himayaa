#include "MainWindow.h"
#include "Core/WebsiteBlocker.h"
#include "Core/DnsProtection.h"
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shlwapi.h>
#include <string>
#include <algorithm>
#include <iterator>

namespace {
constexpr wchar_t kClassName[] = L"HimayaMainWindow";
enum : int {
    ID_CATEGORY=1001, ID_LIST=1002, ID_INPUT=1003, ID_ADD=1004, ID_REMOVE=1005,
    ID_REFRESH=1006, ID_PROFILE=1007, ID_DURATION=1008, ID_APPLY_SITES=1009,
    ID_RESTORE_SITES=1010, ID_BROWSE=1011, ID_REPORT=1012,
    ID_RESTORE_ORIGINAL_HOSTS=1013, ID_ENABLE_ADULT_DNS=1014, ID_DISABLE_ADULT_DNS=1015,
    ID_NAV_DASHBOARD=1101, ID_NAV_APPLICATIONS=1102, ID_NAV_WEBSITES=1103,
    ID_NAV_PROTECTION=1104, ID_NAV_LOGS=1105, ID_NAV_STATISTICS=1106,
    ID_NAV_SETTINGS=1107, ID_THEME=1108
};
bool gDarkTheme = true;
HBRUSH gBackgroundBrush = nullptr;
HBRUSH gPanelBrush = nullptr;
HBRUSH gInputBrush = nullptr;
HBRUSH backgroundBrush() {
    if (!gBackgroundBrush) gBackgroundBrush = CreateSolidBrush(gDarkTheme ? RGB(18,24,36) : RGB(246,248,252));
    return gBackgroundBrush;
}
HBRUSH panelBrush() {
    if (!gPanelBrush) gPanelBrush = CreateSolidBrush(gDarkTheme ? RGB(27,36,51) : RGB(255,255,255));
    return gPanelBrush;
}
HBRUSH inputBrush() {
    if (!gInputBrush) gInputBrush = CreateSolidBrush(gDarkTheme ? RGB(34,45,62) : RGB(255,255,255));
    return gInputBrush;
}
void rebuildBrushes() {
    if (gBackgroundBrush) DeleteObject(gBackgroundBrush);
    if (gPanelBrush) DeleteObject(gPanelBrush);
    if (gInputBrush) DeleteObject(gInputBrush);
    gBackgroundBrush = gPanelBrush = gInputBrush = nullptr;
    backgroundBrush(); panelBrush(); inputBrush();
}
HFONT uiFont() {
    static HFONT font = CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    return font;
}
HFONT headingFont() {
    static HFONT font = CreateFontW(-29,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    return font;
}
HFONT cardFont() {
    static HFONT font = CreateFontW(-21,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    return font;
}
void setFont(HWND control, HFONT font = uiFont()) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}
HWND makeControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
                 int x,int y,int w,int h,int id,HINSTANCE inst) {
    return CreateWindowExW(0, cls, text, WS_CHILD|WS_VISIBLE|style, x,y,w,h,parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),inst,nullptr);
}
void addGroup(std::vector<HWND>& group, HWND control) {
    if (control) group.push_back(control);
}
bool readThemePreference() {
    HKEY key{};
    DWORD value=1, size=sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Himaya\\UI",0,KEY_QUERY_VALUE,&key)==ERROR_SUCCESS) {
        RegQueryValueExW(key,L"DarkTheme",nullptr,nullptr,reinterpret_cast<BYTE*>(&value),&size);
        RegCloseKey(key);
    }
    return value != 0;
}
void writeThemePreference(bool dark) {
    HKEY key{}; DWORD disposition{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Himaya\\UI",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,&disposition)==ERROR_SUCCESS) {
        DWORD value=dark?1u:0u;
        RegSetValueExW(key,L"DarkTheme",0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value));
        RegCloseKey(key);
    }
}
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
        CW_USEDEFAULT, CW_USEDEFAULT, 1320, 880, nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
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

void MainWindow::setStatus(const std::wstring& value) {
    if (status_) SetWindowTextW(status_, value.c_str());
}

void MainWindow::applyTheme() {
    gDarkTheme = darkTheme_;
    rebuildBrushes();
    if (hwnd_) {
        const BOOL darkTitle = darkTheme_ ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd_,20,&darkTitle,sizeof(darkTitle));
        if (themeToggle_) SetWindowTextW(themeToggle_,darkTheme_?L"Switch to Light Mode":L"Switch to Dark Mode");
        InvalidateRect(hwnd_,nullptr,TRUE);
        EnumChildWindows(hwnd_,[](HWND child,LPARAM)->BOOL { InvalidateRect(child,nullptr,TRUE); return TRUE; },0);
    }
}

void MainWindow::toggleTheme() {
    darkTheme_ = !darkTheme_;
    writeThemePreference(darkTheme_);
    applyTheme();
    setStatus(darkTheme_?L"Dark appearance enabled.":L"Light appearance enabled.");
}

void MainWindow::layoutControls() {
    if (!hwnd_) return;
    RECT rc{}; GetClientRect(hwnd_,&rc);
    const int width=rc.right-rc.left, height=rc.bottom-rc.top;
    const int side=226, x=side+34, contentW=std::max(600,width-x-34);
    auto move=[&](HWND h,int px,int py,int w,int he) { if(h) MoveWindow(h,px,py,w,he,TRUE); };
    move(pageTitle_,x,28,contentW,42);
    move(pageSubtitle_,x,76,contentW,28);
    const int gap=18, cardW=(contentW-gap)/2, cardY=142, cardH=126;
    move(dashboardApps_,x+16,cardY+16,cardW-32,cardH-32);
    move(dashboardWebsites_,x+cardW+gap+16,cardY+16,cardW-32,cardH-32);
    move(dashboardFolders_,x+16,cardY+cardH+gap+16,cardW-32,cardH-32);
    move(dashboardDns_,x+cardW+gap+16,cardY+cardH+gap+16,cardW-32,cardH-32);

    const int col=(contentW-36)/3, rowY=145;
    move(categoryLabel_,x,rowY, col,22); move(category_,x,rowY+26,col,34);
    move(profileLabel_,x+col+18,rowY,col,22); move(profile_,x+col+18,rowY+26,col,34);
    move(durationLabel_,x+2*(col+18),rowY,col,22); move(duration_,x+2*(col+18),rowY+26,col,34);
    move(inputLabel_,x,226,contentW,22);
    const int browseW=132, addW=150;
    move(input_,x,254,contentW-browseW-addW-20,38);
    move(browse_,x+contentW-browseW-addW-14,254,browseW,38);
    move(addButton_,x+contentW-addW,254,addW,38);
    move(list_,x,310,contentW,std::max(190,height-430));
    const int actionY=height-104;
    move(removeButton_,x,actionY,180,38);
    move(refreshButton_,x+194,actionY,140,38);
    move(reportButton_,x+348,actionY,230,38);
    move(applySites_,x,actionY+48,245,38);
    move(restoreSites_,x+258,actionY+48,245,38);
    move(restoreOriginalHosts_,x+516,actionY+48,245,38);

    move(protectionDescription_,x,150,contentW,82);
    move(enableAdultDns_,x,250,260,42);
    move(disableAdultDns_,x+278,250,260,42);
    move(summary_,x,320,contentW,54);
    move(statsSummary_,x,150,contentW,390);
    move(settingsDescription_,x,150,contentW,90);
    move(themeToggle_,x,260,230,42);
    move(status_,x,height-42,contentW,30);
}

void MainWindow::setPage(int page) {
    currentPage_=std::clamp(page,0,6);
    if(profileLabel_) ShowWindow(profileLabel_,SW_HIDE);
    if(profile_) ShowWindow(profile_,SW_HIDE);
    auto show=[&](const std::vector<HWND>& group,bool visible) {
        for(HWND control:group) ShowWindow(control,visible?SW_SHOW:SW_HIDE);
    };
    show(managementControls_,false);
    show(dashboardControls_,currentPage_==0);
    show(protectionControls_,currentPage_==3);
    show(statisticsControls_,currentPage_==5);
    show(settingsControls_,currentPage_==6);
    if (currentPage_==1 || currentPage_==2) {
        show(managementControls_,true);
        if(profileLabel_) ShowWindow(profileLabel_,SW_SHOW);
        if(profile_) ShowWindow(profile_,SW_SHOW);
        SendMessageW(category_,CB_SETCURSEL,currentPage_==2?1:0,0);
        const bool websites=currentPage_==2;
        if(applySites_) ShowWindow(applySites_,websites?SW_SHOW:SW_HIDE);
        if(restoreSites_) ShowWindow(restoreSites_,websites?SW_SHOW:SW_HIDE);
        if(restoreOriginalHosts_) ShowWindow(restoreOriginalHosts_,websites?SW_SHOW:SW_HIDE);
    } else if(currentPage_==3) {
        if(profileLabel_) ShowWindow(profileLabel_,SW_SHOW);
        if(profile_) ShowWindow(profile_,SW_SHOW);
    } else if(currentPage_==4) {
        if(list_) ShowWindow(list_,SW_SHOW);
        if(refreshButton_) ShowWindow(refreshButton_,SW_SHOW);
        if(reportButton_) ShowWindow(reportButton_,SW_SHOW);
    }
    const wchar_t* titles[]={L"Dashboard",L"Applications",L"Websites",L"Protection",L"Activity Logs",L"Statistics",L"Settings"};
    const wchar_t* subtitles[]={
        L"Your device protection at a glance.",
        L"Manage blocked applications and protected folders.",
        L"Manage your custom website blocking rules.",
        L"Review the protection profile and DNS filtering controls.",
        L"Recent events recorded by Himaya Security.",
        L"An overview calculated from the data currently stored by Himaya.",
        L"Customize the appearance of the Himaya interface."
    };
    if(pageTitle_) {
        ShowWindow(pageTitle_,SW_HIDE);
        SetWindowTextW(pageTitle_,titles[currentPage_]);
        if(!AnimateWindow(pageTitle_,110,AW_SLIDE|AW_VER_POSITIVE)) ShowWindow(pageTitle_,SW_SHOW);
    }
    if(pageSubtitle_) {
        ShowWindow(pageSubtitle_,SW_HIDE);
        SetWindowTextW(pageSubtitle_,subtitles[currentPage_]);
        if(!AnimateWindow(pageSubtitle_,130,AW_SLIDE|AW_VER_POSITIVE)) ShowWindow(pageSubtitle_,SW_SHOW);
    }
    if(navDashboard_) InvalidateRect(navDashboard_,nullptr,TRUE);
    if(navApplications_) InvalidateRect(navApplications_,nullptr,TRUE);
    if(navWebsites_) InvalidateRect(navWebsites_,nullptr,TRUE);
    if(navProtection_) InvalidateRect(navProtection_,nullptr,TRUE);
    if(navLogs_) InvalidateRect(navLogs_,nullptr,TRUE);
    if(navStatistics_) InvalidateRect(navStatistics_,nullptr,TRUE);
    if(navSettings_) InvalidateRect(navSettings_,nullptr,TRUE);
    layoutControls();
    if(currentPage_==1 || currentPage_==2 || currentPage_==4) refreshList();
    if(currentPage_==0) refreshDashboard();
    if(currentPage_==5) refreshStatistics();
}

void MainWindow::refreshDashboard() {
    if(!store_.isReady()) return;
    const auto apps=store_.load(L"applications");
    const auto sites=store_.load(L"websites");
    const auto folders=store_.load(L"paths");
    const bool dns=store_.getSetting(L"adult_dns_enabled",L"0")==L"1";
    if(dashboardApps_) SetWindowTextW(dashboardApps_,(L"APPLICATIONS\n"+std::to_wstring(apps.size())+L" blocked").c_str());
    if(dashboardWebsites_) SetWindowTextW(dashboardWebsites_,(L"WEBSITES\n"+std::to_wstring(sites.size())+L" blocked").c_str());
    if(dashboardFolders_) SetWindowTextW(dashboardFolders_,(L"PROTECTED FOLDERS\n"+std::to_wstring(folders.size())+L" rules").c_str());
    if(dashboardDns_) SetWindowTextW(dashboardDns_,dns?L"ADULT DNS FILTERING\nEnabled":L"ADULT DNS FILTERING\nDisabled");
}

void MainWindow::refreshStatistics() {
    if(!statsSummary_ || !store_.isReady()) return;
    const auto apps=store_.load(L"applications");
    const auto sites=store_.load(L"websites");
    const auto folders=store_.load(L"paths");
    const auto events=store_.loadRecentEvents(500);
    const auto profile=store_.getSetting(L"active_profile",L"simple");
    const auto dns=store_.getSetting(L"adult_dns_enabled",L"0")==L"1";
    std::wstring value=L"APPLICATIONS     "+std::to_wstring(apps.size())+
        L"\n\nWEBSITES     "+std::to_wstring(sites.size())+
        L"\n\nPROTECTED FOLDERS     "+std::to_wstring(folders.size())+
        L"\n\nACTIVE PROFILE     "+(profile==L"severe"?L"Strict":L"Basic")+
        L"\n\nADULT DNS FILTERING     "+(dns?L"Enabled":L"Disabled")+
        L"\n\nRECENT EVENTS LOADED     "+std::to_wstring(events.size())+
        L"\n\nNote: event count is the latest records returned by the existing event reader, not a lifetime total.";
    SetWindowTextW(statsSummary_,value.c_str());
}

LRESULT MainWindow::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        darkTheme_=readThemePreference();
        pageTitle_=makeControl(hwnd_,L"STATIC",L"Dashboard",SS_LEFT,260,28,900,42,0,instance_); setFont(pageTitle_,headingFont());
        pageSubtitle_=makeControl(hwnd_,L"STATIC",L"Your device protection at a glance.",SS_LEFT,260,76,900,28,0,instance_);
        auto brand=makeControl(hwnd_,L"STATIC",L"HIMAYA SECURITY",SS_LEFT,22,30,185,34,0,instance_); setFont(brand,headingFont());
        auto tagline=makeControl(hwnd_,L"STATIC",L"PERSONAL PROTECTION",SS_LEFT,24,68,180,22,0,instance_);
        const int navX=16,navY=132,navW=194,navH=42,navGap=10;
        navDashboard_=makeControl(hwnd_,L"BUTTON",L"Dashboard",BS_OWNERDRAW,navX,navY,navW,navH,ID_NAV_DASHBOARD,instance_);
        navApplications_=makeControl(hwnd_,L"BUTTON",L"Applications",BS_OWNERDRAW,navX,navY+(navH+navGap),navW,navH,ID_NAV_APPLICATIONS,instance_);
        navWebsites_=makeControl(hwnd_,L"BUTTON",L"Websites",BS_OWNERDRAW,navX,navY+2*(navH+navGap),navW,navH,ID_NAV_WEBSITES,instance_);
        navProtection_=makeControl(hwnd_,L"BUTTON",L"Protection",BS_OWNERDRAW,navX,navY+3*(navH+navGap),navW,navH,ID_NAV_PROTECTION,instance_);
        navLogs_=makeControl(hwnd_,L"BUTTON",L"Activity Logs",BS_OWNERDRAW,navX,navY+4*(navH+navGap),navW,navH,ID_NAV_LOGS,instance_);
        navStatistics_=makeControl(hwnd_,L"BUTTON",L"Statistics",BS_OWNERDRAW,navX,navY+5*(navH+navGap),navW,navH,ID_NAV_STATISTICS,instance_);
        navSettings_=makeControl(hwnd_,L"BUTTON",L"Settings",BS_OWNERDRAW,navX,navY+6*(navH+navGap),navW,navH,ID_NAV_SETTINGS,instance_);
        themeToggle_=makeControl(hwnd_,L"BUTTON",L"Switch to Light Mode",BS_PUSHBUTTON,22,0,185,40,ID_THEME,instance_);

        dashboardApps_=makeControl(hwnd_,L"STATIC",L"APPLICATIONS\n0 blocked",SS_LEFT,0,0,400,90,0,instance_); setFont(dashboardApps_,cardFont());
        dashboardWebsites_=makeControl(hwnd_,L"STATIC",L"WEBSITES\n0 blocked",SS_LEFT,0,0,400,90,0,instance_); setFont(dashboardWebsites_,cardFont());
        dashboardFolders_=makeControl(hwnd_,L"STATIC",L"PROTECTED FOLDERS\n0 rules",SS_LEFT,0,0,400,90,0,instance_); setFont(dashboardFolders_,cardFont());
        dashboardDns_=makeControl(hwnd_,L"STATIC",L"ADULT DNS FILTERING\nDisabled",SS_LEFT,0,0,400,90,0,instance_); setFont(dashboardDns_,cardFont());
        addGroup(dashboardControls_,dashboardApps_); addGroup(dashboardControls_,dashboardWebsites_);
        addGroup(dashboardControls_,dashboardFolders_); addGroup(dashboardControls_,dashboardDns_);

        categoryLabel_=makeControl(hwnd_,L"STATIC",L"List type",SS_LEFT,0,0,180,22,0,instance_);
        category_=makeControl(hwnd_,WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL,0,0,200,180,ID_CATEGORY,instance_);
        SendMessageW(category_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Applications"));
        SendMessageW(category_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Websites"));
        SendMessageW(category_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Folders"));
        SendMessageW(category_,CB_SETCURSEL,0,0);
        profileLabel_=makeControl(hwnd_,L"STATIC",L"Protection profile",SS_LEFT,0,0,180,22,0,instance_);
        profile_=makeControl(hwnd_,WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL,0,0,200,180,ID_PROFILE,instance_);
        SendMessageW(profile_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Basic"));
        SendMessageW(profile_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Strict"));
        SendMessageW(profile_,CB_SETCURSEL,store_.getSetting(L"active_profile",L"simple")==L"severe"?1:0,0);
        durationLabel_=makeControl(hwnd_,L"STATIC",L"Application lock duration",SS_LEFT,0,0,220,22,0,instance_);
        duration_=makeControl(hwnd_,WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL,0,0,220,180,ID_DURATION,instance_);
        const wchar_t* durations[]={L"Long-term — no automatic expiry",L"2 minutes — test",L"1 hour",L"24 hours",L"7 days",L"30 days",L"90 days"};
        for(const auto* duration:durations) SendMessageW(duration_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(duration));
        SendMessageW(duration_,CB_SETCURSEL,0,0);
        inputLabel_=makeControl(hwnd_,L"STATIC",L"Application, website, or folder",SS_LEFT,0,0,500,22,0,instance_);
        input_=makeControl(hwnd_,L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,0,0,400,38,ID_INPUT,instance_);
        browse_=makeControl(hwnd_,L"BUTTON",L"Browse...",BS_PUSHBUTTON,0,0,130,38,ID_BROWSE,instance_);
        addButton_=makeControl(hwnd_,L"BUTTON",L"Add to list",BS_PUSHBUTTON,0,0,150,38,ID_ADD,instance_);
        list_=makeControl(hwnd_,WC_LISTBOXW,L"",WS_BORDER|LBS_NOTIFY|WS_VSCROLL,0,0,600,300,ID_LIST,instance_);
        removeButton_=makeControl(hwnd_,L"BUTTON",L"Remove selected",BS_PUSHBUTTON,0,0,180,38,ID_REMOVE,instance_);
        refreshButton_=makeControl(hwnd_,L"BUTTON",L"Refresh",BS_PUSHBUTTON,0,0,140,38,ID_REFRESH,instance_);
        reportButton_=makeControl(hwnd_,L"BUTTON",L"Export CSV",BS_PUSHBUTTON,0,0,230,38,ID_REPORT,instance_);
        applySites_=makeControl(hwnd_,L"BUTTON",L"Apply custom website rules",BS_PUSHBUTTON,0,0,245,38,ID_APPLY_SITES,instance_);
        restoreSites_=makeControl(hwnd_,L"BUTTON",L"Restore custom website rules",BS_PUSHBUTTON,0,0,245,38,ID_RESTORE_SITES,instance_);
        restoreOriginalHosts_=makeControl(hwnd_,L"BUTTON",L"Restore original Hosts file",BS_PUSHBUTTON,0,0,245,38,ID_RESTORE_ORIGINAL_HOSTS,instance_);

        protectionDescription_=makeControl(hwnd_,L"STATIC",L"Adult-content filtering uses DNS. Selected social platforms are blocked as complete websites in supported browsers. Original DNS and browser policies are backed up by the existing protection module.",SS_LEFT,0,0,700,90,0,instance_);
        enableAdultDns_=makeControl(hwnd_,L"BUTTON",L"Enable adult DNS protection",BS_PUSHBUTTON,0,0,260,42,ID_ENABLE_ADULT_DNS,instance_);
        disableAdultDns_=makeControl(hwnd_,L"BUTTON",L"Disable adult DNS protection",BS_PUSHBUTTON,0,0,260,42,ID_DISABLE_ADULT_DNS,instance_);
        summary_=makeControl(hwnd_,L"STATIC",L"",SS_LEFT,0,0,700,54,0,instance_);
        statsSummary_=makeControl(hwnd_,L"STATIC",L"",SS_LEFT,0,0,700,390,0,instance_);
        settingsDescription_=makeControl(hwnd_,L"STATIC",L"Choose the appearance that is most comfortable. This preference is stored separately in your Windows user profile and does not change protection settings.",SS_LEFT,0,0,700,90,0,instance_);
        status_=makeControl(hwnd_,L"STATIC",L"Ready.",SS_LEFT,260,0,900,30,0,instance_);

        addGroup(managementControls_,categoryLabel_); addGroup(managementControls_,category_);
        addGroup(managementControls_,durationLabel_); addGroup(managementControls_,duration_);
        addGroup(managementControls_,inputLabel_); addGroup(managementControls_,input_);
        addGroup(managementControls_,browse_); addGroup(managementControls_,addButton_);
        addGroup(managementControls_,list_); addGroup(managementControls_,removeButton_);
        addGroup(managementControls_,refreshButton_); addGroup(managementControls_,reportButton_);
        addGroup(managementControls_,applySites_); addGroup(managementControls_,restoreSites_);
        addGroup(managementControls_,restoreOriginalHosts_);
        addGroup(protectionControls_,protectionDescription_); addGroup(protectionControls_,enableAdultDns_);
        addGroup(protectionControls_,disableAdultDns_);
        addGroup(statisticsControls_,statsSummary_);
        addGroup(settingsControls_,settingsDescription_); addGroup(settingsControls_,themeToggle_); addGroup(settingsControls_,summary_);

        EnumChildWindows(hwnd_,[](HWND child,LPARAM font)->BOOL { setFont(child,reinterpret_cast<HFONT>(font)); SetWindowTheme(child,L"Explorer",nullptr); return TRUE; },reinterpret_cast<LPARAM>(uiFont()));
        setFont(pageTitle_,headingFont()); setFont(dashboardApps_,cardFont()); setFont(dashboardWebsites_,cardFont());
        setFont(dashboardFolders_,cardFont()); setFont(dashboardDns_,cardFont());
        if(!store_.isReady()) MessageBoxW(hwnd_,store_.lastError().c_str(),L"Database error",MB_OK|MB_ICONERROR);
        const bool dnsEnabled=store_.getSetting(L"adult_dns_enabled",L"0")==L"1";
        EnableWindow(enableAdultDns_,!dnsEnabled); EnableWindow(disableAdultDns_,dnsEnabled);
        SetWindowTextW(summary_,(L"Current local database: "+store_.databasePath()).c_str());
        applyTheme();
        layoutControls();
        setPage(0);
        return 0;
    }
    case WM_SIZE:
        layoutControls();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize.x=1100; info->ptMinTrackSize.y=720;
        return 0;
    }
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case ID_NAV_DASHBOARD: setPage(0); return 0;
        case ID_NAV_APPLICATIONS: setPage(1); return 0;
        case ID_NAV_WEBSITES: setPage(2); return 0;
        case ID_NAV_PROTECTION: setPage(3); return 0;
        case ID_NAV_LOGS: setPage(4); return 0;
        case ID_NAV_STATISTICS: setPage(5); return 0;
        case ID_NAV_SETTINGS: setPage(6); return 0;
        case ID_THEME: toggleTheme(); return 0;
        case ID_CATEGORY: if(HIWORD(wp)==CBN_SELCHANGE) refreshList(); return 0;
        case ID_PROFILE: if(HIWORD(wp)==CBN_SELCHANGE) saveProfile(); return 0;
        case ID_ADD: addEntry(); return 0;
        case ID_BROWSE: browseEntry(); return 0;
        case ID_REMOVE: removeEntry(); return 0;
        case ID_REFRESH: refreshList(); refreshDashboard(); refreshStatistics(); return 0;
        case ID_REPORT: exportReport(); return 0;
        case ID_APPLY_SITES: applyWebsites(); return 0;
        case ID_RESTORE_SITES: restoreWebsites(); return 0;
        case ID_RESTORE_ORIGINAL_HOSTS: restoreOriginalHosts(); return 0;
        case ID_ENABLE_ADULT_DNS: setAdultDnsProtection(true); refreshDashboard(); refreshStatistics(); return 0;
        case ID_DISABLE_ADULT_DNS: setAdultDnsProtection(false); refreshDashboard(); refreshStatistics(); return 0;
        }
        break;
    case WM_ERASEBKGND: {
        RECT rc{}; GetClientRect(hwnd_,&rc);
        FillRect(reinterpret_cast<HDC>(wp),&rc,backgroundBrush());
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd_,&ps);
        RECT side{0,0,226,ps.rcPaint.bottom};
        FillRect(dc,&side,panelBrush());
        HPEN pen=CreatePen(PS_SOLID,1,gDarkTheme?RGB(48,61,80):RGB(229,233,240));
        HGDIOBJ old=SelectObject(dc,pen);
        MoveToEx(dc,225,0,nullptr); LineTo(dc,225,ps.rcPaint.bottom);
        SelectObject(dc,old); DeleteObject(pen);
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,gDarkTheme?RGB(150,165,185):RGB(112,124,142));
        RECT brand{22,92,205,116}; DrawTextW(dc,L"FOCUS  •  CONTROL  •  PROTECT",-1,&brand,DT_LEFT|DT_SINGLELINE|DT_VCENTER);
        EndPaint(hwnd_,&ps);
        return 0;
    }
    case WM_DRAWITEM: {
        auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        const int id=static_cast<int>(item->CtlID);
        if(id>=ID_NAV_DASHBOARD && id<=ID_NAV_SETTINGS) {
            const int page=id-ID_NAV_DASHBOARD;
            const bool selected=page==currentPage_;
            const bool hot=(item->itemState&ODS_HOTLIGHT)!=0;
            HBRUSH fill=CreateSolidBrush(selected?(gDarkTheme?RGB(39,58,85):RGB(231,239,255)):
                (hot?(gDarkTheme?RGB(42,55,74):RGB(241,246,255)):(gDarkTheme?RGB(27,36,51):RGB(255,255,255))));
            HPEN edge=CreatePen(PS_SOLID,1,selected?(gDarkTheme?RGB(62,103,158):RGB(205,221,250)):(gDarkTheme?RGB(27,36,51):RGB(255,255,255)));
            HGDIOBJ oldBrush=SelectObject(item->hDC,fill),oldPen=SelectObject(item->hDC,edge);
            RoundRect(item->hDC,item->rcItem.left+1,item->rcItem.top+1,item->rcItem.right-1,item->rcItem.bottom-1,9,9);
            SelectObject(item->hDC,oldBrush); SelectObject(item->hDC,oldPen); DeleteObject(fill); DeleteObject(edge);
            SetBkMode(item->hDC,TRANSPARENT);
            SetTextColor(item->hDC,selected?RGB(48,116,220):(gDarkTheme?RGB(220,228,239):RGB(55,66,82)));
            RECT text=item->rcItem; text.left+=16;
            wchar_t caption[80]{}; GetWindowTextW(item->hwndItem,caption,static_cast<int>(std::size(caption)));
            DrawTextW(item->hDC,caption,-1,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc=reinterpret_cast<HDC>(wp);
        SetTextColor(dc,gDarkTheme?RGB(226,233,243):RGB(39,50,66));
        SetBkMode(dc,TRANSPARENT);
        return reinterpret_cast<LRESULT>(backgroundBrush());
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc=reinterpret_cast<HDC>(wp);
        SetTextColor(dc,gDarkTheme?RGB(235,241,249):RGB(39,50,66));
        SetBkColor(dc,gDarkTheme?RGB(34,45,62):RGB(255,255,255));
        return reinterpret_cast<LRESULT>(inputBrush());
    }
    case WM_CTLCOLORBTN: {
        HDC dc=reinterpret_cast<HDC>(wp);
        SetTextColor(dc,gDarkTheme?RGB(235,241,249):RGB(39,50,66));
        SetBkMode(dc,TRANSPARENT);
        return reinterpret_cast<LRESULT>(panelBrush());
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd_,msg,wp,lp);
}

std::wstring MainWindow::selectedCategory() const {
    const LRESULT index = SendMessageW(category_, CB_GETCURSEL, 0, 0);
    if (index == 1) return L"websites";
    if (index == 2) return L"paths";
    return L"applications";
}

std::wstring MainWindow::selectedDuration() const {
    const LRESULT index = SendMessageW(duration_, CB_GETCURSEL, 0, 0);
    switch (index) { case 1: return L"2m"; case 2: return L"1h"; case 3: return L"24h"; case 4: return L"7d"; case 5: return L"30d"; case 6: return L"90d"; default: return L"long"; }
}

std::wstring MainWindow::selectedProfile() const {
    const LRESULT index = SendMessageW(profile_, CB_GETCURSEL, 0, 0);
    switch (index) { case 1: return L"severe"; default: return L"simple"; }
}

void MainWindow::saveProfile() {
    if (store_.setSetting(L"active_profile", selectedProfile())) {
        store_.logEvent(L"profile_changed", selectedProfile());
        setStatus(L"Profile saved. This currently selects a profile only; it does not automatically enable different protection rules.");
    }
}

void MainWindow::refreshList() {
    if (!list_) return;
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    if (currentPage_ == 4) {
        const auto events = store_.loadRecentEvents(500);
        for (const auto& event : events) {
            std::wstring line = event.timestamp + L"   |   " + event.type + L"   |   " + event.details;
            SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        }
        return;
    }
    visibleEntries_ = store_.load(selectedCategory());
    for (const auto& entry : visibleEntries_) {
        std::wstring label = entry.value;
        if (selectedCategory() == L"applications" || selectedCategory() == L"websites") {
            label += entry.profile == L"severe" ? L"   |   STRICT — locked until expiry" : L"   |   Basic";
            label += entry.lockMode == L"long" ? L"   |   Long-term" : (entry.lockMode == L"2m" ? L"   |   2-minute test" : L"   |   Timed lock");
        }
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    const bool websites = currentPage_ == 2 && selectedCategory() == L"websites";
    if (applySites_) EnableWindow(applySites_, websites);
    const bool strictWebsiteLock = websites &&
        std::any_of(visibleEntries_.begin(), visibleEntries_.end(), [](const himaya::Entry& entry) {
            return entry.profile == L"severe";
        });
    if (restoreSites_) EnableWindow(restoreSites_, websites && !strictWebsiteLock);
    if (restoreOriginalHosts_) EnableWindow(restoreOriginalHosts_, websites && !strictWebsiteLock);
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
    if(category==L"applications") {
        wchar_t selected[32768]{},self[32768]{};
        const DWORD selectedLength=GetFullPathNameW(value,static_cast<DWORD>(std::size(selected)),selected,nullptr);
        const DWORD selfLength=GetModuleFileNameW(nullptr,self,static_cast<DWORD>(std::size(self)));
        if(selectedLength && selectedLength<std::size(selected) && selfLength && selfLength<std::size(self)) {
            std::wstring guard(self);
            const auto slash=guard.find_last_of(L"\\/");
            if(slash!=std::wstring::npos) guard.resize(slash+1);
            guard+=L"HimayaGuard.exe";
            if(_wcsicmp(selected,self)==0 || _wcsicmp(selected,guard.c_str())==0) {
                MessageBoxW(hwnd_,L"Himaya cannot block its own interface or protection service. This prevents accidental loss of access to the recovery controls.",L"Protected application",MB_OK|MB_ICONWARNING);
                return;
            }
        }
    }
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
    const auto duration = (category == L"applications" || category == L"websites") ? selectedDuration() : L"long";
    if (store_.addEntry(category, value, duration, selectedProfile())) {
        SetWindowTextW(input_, L""); refreshList(); refreshDashboard(); refreshStatistics();
        setStatus(category == L"websites" ? L"Website saved. HimayaGuard will synchronize the website block automatically; you can also apply it now." :
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
        refreshList(); refreshDashboard(); refreshStatistics(); setStatus(L"Rule removed. The protection service will synchronize the change.");
    } else {
        const auto error = store_.lastError();
        MessageBoxW(hwnd_, error.empty() ? L"Could not remove this rule." : error.c_str(),
                    L"Strict-mode protection", MB_OK | MB_ICONWARNING);
    }
}

void MainWindow::applyWebsites() {
    if (selectedCategory() != L"websites") return;
    const int answer = MessageBoxW(hwnd_,
        L"Only websites you added to Himaya will be written to the managed Hosts section. Adult-content filtering is handled separately. Continue?",
        L"Confirm custom website blocking", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer != IDYES) return;
    std::vector<std::wstring> domains;
    for(const auto& entry:store_.load(L"websites")) domains.push_back(entry.value);
    const auto result=himaya::WebsiteBlocker::apply(domains,false);
    if(result.success) store_.logEvent(L"websites_applied",std::to_wstring(result.blockedDomains));
    std::wstring message=result.message;
    if(result.success && result.blockedDomains) message+=L"\nDomains blocked: "+std::to_wstring(result.blockedDomains);
    setStatus(message);
    MessageBoxW(hwnd_,message.c_str(),L"Himaya Security",MB_OK|(result.success?MB_ICONINFORMATION:MB_ICONERROR));
}


void MainWindow::setAdultDnsProtection(bool enabled) {
    const int answer = MessageBoxW(hwnd_,
        enabled
          ? L"Himaya will enable adult-content DNS filtering and block the entire YouTube, Instagram, Facebook, Snapchat, TikTok, Likee, Kwai, X, Reddit, Threads, Pinterest and Tumblr websites. Some of these sites use single-page navigation, so blocking only a Reels/Shorts URL can be bypassed when content changes inside the page. Full-site blocking is the reliable option. Rules apply to Edge, Chrome, Brave and Firefox and are re-synchronized by HimayaGuard. Original DNS and browser policies will be backed up. Continue?"
          : L"Himaya will restore the DNS and browser policy settings saved before protection was enabled, including the social-platform blocks, and remove Himaya DNS firewall rules. Continue?",
        enabled ? L"Enable adult-content DNS protection" : L"Restore DNS settings",
        MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2);
    if(answer!=IDYES) return;
    const auto result = enabled ? himaya::DnsProtection::enable() : himaya::DnsProtection::restore();
    if(result.success) {
        store_.setSetting(L"adult_dns_enabled",enabled?L"1":L"0");
        store_.logEvent(enabled?L"adult_dns_enabled":L"adult_dns_disabled",result.message);
        EnableWindow(enableAdultDns_,!enabled);
        EnableWindow(disableAdultDns_,enabled);
    }
    setStatus(result.message);
    MessageBoxW(hwnd_,result.message.c_str(),L"Himaya Security",MB_OK|(result.success?MB_ICONINFORMATION:MB_ICONERROR));
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
