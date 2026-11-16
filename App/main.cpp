
#include "MainWindow.h"
#include <commctrl.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    INITCOMMONCONTROLSEX controls{
        sizeof(INITCOMMONCONTROLSEX),
        ICC_STANDARD_CLASSES
    };

    InitCommonControlsEx(&controls);

    MainWindow window(instance);

    if (!window.create(showCommand)) {
        MessageBoxW(
            nullptr,
            L"تعذر تشغيل Himaya.",
            L"Himaya",
            MB_OK | MB_ICONERROR
        );
        return 1;
    }

    return window.run();
}
