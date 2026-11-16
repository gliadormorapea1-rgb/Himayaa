#include "MainWindow.h"
#include "Core/WebsiteBlocker.h"
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <iterator>

namespace {
constexpr wchar_t kClassName[] = L"HimayaMainWindow";
enum : int { ID_CATEGORY=1001, ID_LIST=1002, ID_INPUT=1003, ID_ADD=1004, ID_REMOVE=1005,
             ID_REFRESH=1006, ID_PROFILE=1007, ID_DURATION=1008, ID_APPLY_SITES=1009, ID_RESTORE_SITES=1010 };
HWND makeControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
                 int x,int y,int w,int h,int id,HINSTANCE inst) {
    return CreateWindowExW(0, cls, text, WS_CHILD|WS_VISIBLE|style, x,y,w,h,parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),inst,nullptr);
}
void setFont(HWND control) {
    static HFONT font = CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}
}

MainWindow::MainWindow(HINSTANCE instance) : instance_(instance) {}

bool MainWindow::create(int showCommand) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc; wc.hInstance = instance_; wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    hwnd_ = CreateWindowExW(0, kClassName, L"Himaya Security | حماية رقمية", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1060, 820, nullptr, nullptr, instance_, this);
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

void MainWindow::setStatus(const std::wstring& value) { if (status_) SetWindowTextW(status_, value.c_str()); }

LRESULT MainWindow::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        auto title = makeControl(hwnd_, L"STATIC", L"HIMAYA  |  الحماية من الإدمان الرقمي", SS_LEFT, 28,18,960,42,0,instance_);
        setFont(title);
        auto subtitle = makeControl(hwnd_, L"STATIC", L"إدارة القوائم والسياسات محليًا — الإصدار 0.4.0", SS_LEFT, 30,60,950,28,0,instance_);
        setFont(subtitle);
        settings_ = makeControl(hwnd_, L"STATIC", L"السياسات المعتمدة: حظر الإباحية شامل  |  فك القيود بموافقة + مهلة  |  إدارة مشتركة", SS_LEFT, 30,98,960,28,0,instance_);
        setFont(settings_);

        makeControl(hwnd_, L"STATIC", L"نوع القائمة", SS_LEFT, 30,145,120,24,0,instance_);
        category_ = makeControl(hwnd_, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST|WS_VSCROLL, 30,172,220,220,ID_CATEGORY,instance_);
        SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"التطبيقات"));
        SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"المواقع"));
        SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"المسارات"));
        SendMessageW(category_, CB_SETCURSEL, 0, 0);

        makeControl(hwnd_, L"STATIC", L"وضع التعافي", SS_LEFT, 275,145,150,24,0,instance_);
        profile_ = makeControl(hwnd_, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST|WS_VSCROLL, 275,172,230,220,ID_PROFILE,instance_);
        SendMessageW(profile_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"بسيط"));
        SendMessageW(profile_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"متوسط"));
        SendMessageW(profile_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"حاد"));
        SendMessageW(profile_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"تعافٍ مكثف"));
        const auto active = store_.getSetting(L"active_profile", L"simple");
        int profileIndex = active == L"moderate" ? 1 : active == L"severe" ? 2 : active == L"recovery" ? 3 : 0;
        SendMessageW(profile_, CB_SETCURSEL, profileIndex, 0);

        makeControl(hwnd_, L"STATIC", L"مدة قفل التطبيق", SS_LEFT, 530,145,170,24,0,instance_);
        duration_ = makeControl(hwnd_, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST|WS_VSCROLL, 530,172,230,220,ID_DURATION,instance_);
        SendMessageW(duration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"طويل الأجل — دون انتهاء تلقائي"));
        SendMessageW(duration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"ساعة واحدة"));
        SendMessageW(duration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"24 ساعة"));
        SendMessageW(duration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"7 أيام"));
        SendMessageW(duration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"30 يومًا"));
        SendMessageW(duration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"90 يومًا"));
        SendMessageW(duration_, CB_SETCURSEL, 0, 0);

        input_ = makeControl(hwnd_, L"EDIT", L"", WS_BORDER|ES_AUTOHSCROLL, 30,220,720,32,ID_INPUT,instance_);
        makeControl(hwnd_, L"BUTTON", L"إضافة إلى القائمة", BS_PUSHBUTTON, 770,220,220,32,ID_ADD,instance_);
        list_ = makeControl(hwnd_, WC_LISTBOXW, L"", WS_BORDER|LBS_NOTIFY|WS_VSCROLL, 30,275,960,300,ID_LIST,instance_);
        makeControl(hwnd_, L"BUTTON", L"حذف المحدد من القائمة", BS_PUSHBUTTON, 30,590,220,36,ID_REMOVE,instance_);
        makeControl(hwnd_, L"BUTTON", L"تحديث القائمة", BS_PUSHBUTTON, 265,590,140,36,ID_REFRESH,instance_);
        applySites_ = makeControl(hwnd_, L"BUTTON", L"تطبيق حظر المواقع الإباحية والمخصصة", BS_PUSHBUTTON, 30,640,360,38,ID_APPLY_SITES,instance_);
        restoreSites_ = makeControl(hwnd_, L"BUTTON", L"استعادة إعدادات المواقع", BS_PUSHBUTTON, 405,640,260,38,ID_RESTORE_SITES,instance_);
        summary_ = makeControl(hwnd_, L"STATIC", L"قاعدة البيانات: جاري التحميل...", SS_LEFT, 30,690,960,26,0,instance_);
        status_ = makeControl(hwnd_, L"STATIC", L"حظر المواقع يستخدم ملف hosts وقائمة مجتمعية؛ قد تتجاوزه بعض إعدادات DNS المشفرة. حظر التطبيقات لم يُفعّل بعد.", SS_LEFT, 30,725,960,40,0,instance_);
        if (!store_.isReady()) {
            MessageBoxW(hwnd_, store_.lastError().c_str(), L"تعذر فتح قاعدة البيانات", MB_OK|MB_ICONERROR);
        }
        SetWindowTextW(summary_, (L"قاعدة البيانات المحلية: " + store_.databasePath()).c_str());
        refreshList();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_CATEGORY: if (HIWORD(wp)==CBN_SELCHANGE) refreshList(); return 0;
        case ID_PROFILE: if (HIWORD(wp)==CBN_SELCHANGE) saveProfile(); return 0;
        case ID_ADD: addEntry(); return 0;
        case ID_REMOVE: removeEntry(); return 0;
        case ID_REFRESH: refreshList(); return 0;
        case ID_APPLY_SITES: applyWebsites(); return 0;
        case ID_RESTORE_SITES: restoreWebsites(); return 0;
        }
        break;
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
        setStatus(L"تم حفظ وضع التعافي. هذا يحدد ملف السياسة فقط ولا يفعّل الحظر تلقائيًا.");
    }
}

void MainWindow::refreshList() {
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    visibleEntries_ = store_.load(selectedCategory());
    for (const auto& entry : visibleEntries_) {
        std::wstring label = entry.value;
        if (selectedCategory() == L"applications") {
            label += entry.lockMode == L"long" ? L"   |   قفل طويل الأجل (تطبقه خدمة الحماية)" : L"   |   قفل مؤقت (تطبقه خدمة الحماية)";
        }
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (applySites_) EnableWindow(applySites_, selectedCategory() == L"websites");
    if (restoreSites_) EnableWindow(restoreSites_, selectedCategory() == L"websites");
}

void MainWindow::addEntry() {
    wchar_t value[2048]{};
    GetWindowTextW(input_, value, static_cast<int>(std::size(value)));
    if (!value[0]) { MessageBoxW(hwnd_, L"اكتب اسم التطبيق أو الموقع أو المسار أولاً.", L"Himaya", MB_OK|MB_ICONINFORMATION); return; }
    const auto category = selectedCategory();
    const auto duration = category == L"applications" ? selectedDuration() : L"long";
    if (store_.addEntry(category, value, duration, selectedProfile())) {
        SetWindowTextW(input_, L""); refreshList();
        setStatus(category == L"websites" ? L"تم حفظ الموقع. اضغط تطبيق حظر المواقع لتفعيل القواعد بصلاحيات المسؤول." : L"تم حفظ القاعدة في SQLite. ستتولى خدمة HimayaGuard تطبيق منع التشغيل تلقائيًا بعد تثبيت الخدمة.");
    } else {
        MessageBoxW(hwnd_, store_.lastError().empty() ? L"تعذر حفظ العنصر." : store_.lastError().c_str(), L"Himaya", MB_OK|MB_ICONERROR);
    }
}

void MainWindow::removeEntry() {
    const LRESULT index = SendMessageW(list_, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR || static_cast<size_t>(index) >= visibleEntries_.size()) return;
    if (store_.removeEntry(selectedCategory(), visibleEntries_[static_cast<size_t>(index)].value)) {
        refreshList(); setStatus(L"تم حذف القاعدة؛ ستزيل خدمة HimayaGuard سياسة المنع في المزامنة التالية.");
    }
}

void MainWindow::applyWebsites() {
    if (selectedCategory() != L"websites") return;
    const int answer = MessageBoxW(hwnd_,
        L"سيتم طلب صلاحيات مسؤول Windows لتحديث ملف hosts، وتطبيق قائمة نطاقات إباحية مجتمعية بالإضافة إلى المواقع التي أضفتها. قد لا تشمل القائمة كل موقع موجود. هل تريد المتابعة؟",
        L"تأكيد تطبيق حظر المواقع", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer != IDYES) return;
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) {
        setStatus(L"تعذر تحديد مسار تشغيل Himaya."); return;
    }
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_, L"runas", executable, L"--apply-sites", nullptr, SW_SHOWNORMAL));
    if (result <= 32) setStatus(L"لم يتم تشغيل إجراء الحظر بصلاحيات المسؤول. لم تتغير إعدادات المواقع.");
    else setStatus(L"تم طلب صلاحيات المسؤول. ستظهر نتيجة تطبيق الحظر في نافذة منفصلة.");
}

void MainWindow::restoreWebsites() {
    if (selectedCategory() != L"websites") return;
    const int answer = MessageBoxW(hwnd_,
        L"سيتم إزالة الجزء الذي أضافه Himaya فقط من ملف hosts، مع الحفاظ على بقية الأسطر. هل تريد المتابعة؟",
        L"تأكيد استعادة إعدادات المواقع", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer != IDYES) return;
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) {
        setStatus(L"تعذر تحديد مسار تشغيل Himaya."); return;
    }
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_, L"runas", executable, L"--restore-sites", nullptr, SW_SHOWNORMAL));
    if (result <= 32) setStatus(L"لم يتم تشغيل إجراء الاستعادة بصلاحيات المسؤول.");
    else setStatus(L"تم طلب صلاحيات المسؤول لاستعادة إعدادات المواقع.");
}
