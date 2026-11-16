#include "MainWindow.h"
#include <commctrl.h>
#include <shlobj.h>
#include <string>

namespace {
constexpr wchar_t kClassName[] = L"HimayaMainWindow";
enum : int { ID_CATEGORY=1001, ID_LIST=1002, ID_INPUT=1003, ID_ADD=1004, ID_REMOVE=1005, ID_REFRESH=1006 };
HWND makeControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x,int y,int w,int h,int id,HINSTANCE inst) {
    return CreateWindowExW(0, cls, text, WS_CHILD|WS_VISIBLE|style, x,y,w,h,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),inst,nullptr);
}
}

MainWindow::MainWindow(HINSTANCE instance) : instance_(instance) {}

bool MainWindow::create(int showCommand) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    hwnd_ = CreateWindowExW(0, kClassName, L"Himaya Security | حماية", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 560, nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
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

LRESULT MainWindow::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        makeControl(hwnd_, L"STATIC", L"لوحة تحكم Himaya", SS_LEFT, 24,18,700,36,0,instance_);
        makeControl(hwnd_, L"STATIC", L"هذه نسخة تأسيسية لإدارة القوائم فقط. لا يتم تطبيق أي حظر تلقائيًا.", SS_LEFT, 24,58,750,28,0,instance_);
        category_ = makeControl(hwnd_, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST|WS_VSCROLL, 24,105,260,250,ID_CATEGORY,instance_);
        SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"التطبيقات (قائمة فقط)"));
        SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"المواقع (قائمة فقط)"));
        SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"المسارات (قائمة فقط)"));
        SendMessageW(category_, CB_SETCURSEL, 0, 0);
        input_ = makeControl(hwnd_, L"EDIT", L"", WS_BORDER|ES_AUTOHSCROLL, 300,105,330,28,ID_INPUT,instance_);
        makeControl(hwnd_, L"BUTTON", L"إضافة", BS_PUSHBUTTON, 644,105,120,28,ID_ADD,instance_);
        list_ = makeControl(hwnd_, WC_LISTBOXW, L"", WS_BORDER|LBS_NOTIFY|WS_VSCROLL, 24,150,740,290,ID_LIST,instance_);
        makeControl(hwnd_, L"BUTTON", L"حذف المحدد", BS_PUSHBUTTON, 24,455,130,32,ID_REMOVE,instance_);
        makeControl(hwnd_, L"BUTTON", L"تحديث", BS_PUSHBUTTON, 166,455,100,32,ID_REFRESH,instance_);
        status_ = makeControl(hwnd_, L"STATIC", L"وضع آمن: لا توجد تغييرات على إعدادات Windows.", SS_LEFT, 24,500,740,24,0,instance_);
        refreshList();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_CATEGORY: if (HIWORD(wp)==CBN_SELCHANGE) refreshList(); return 0;
        case ID_ADD: addEntry(); return 0;
        case ID_REMOVE: removeEntry(); return 0;
        case ID_REFRESH: refreshList(); return 0;
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

void MainWindow::refreshList() {
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    for (const auto& value : store_.load(selectedCategory()))
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
}

void MainWindow::addEntry() {
    wchar_t value[2048]{};
    GetWindowTextW(input_, value, static_cast<int>(std::size(value)));
    if (!value[0]) { MessageBoxW(hwnd_, L"اكتب قيمة أولاً.", L"Himaya", MB_OK|MB_ICONINFORMATION); return; }
    if (store_.appendUnique(selectedCategory(), value)) {
        SetWindowTextW(input_, L"");
        refreshList();
        SetWindowTextW(status_, L"تم حفظ العنصر في القائمة. لم يتم تفعيل الحظر بعد.");
    } else {
        MessageBoxW(hwnd_, L"تعذر حفظ العنصر.", L"Himaya", MB_OK|MB_ICONERROR);
    }
}

void MainWindow::removeEntry() {
    const LRESULT index = SendMessageW(list_, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR) return;
    wchar_t value[2048]{};
    SendMessageW(list_, LB_GETTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(value));
    if (store_.remove(selectedCategory(), value)) {
        refreshList();
        SetWindowTextW(status_, L"تم حذف العنصر من القائمة.");
    }
}
