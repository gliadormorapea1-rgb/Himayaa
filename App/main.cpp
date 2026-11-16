#include "MainWindow.h"
#include "Core/WebsiteBlocker.h"
#include "Core/DnsProtection.h"
#include <commctrl.h>
#include <shellapi.h>
#include <string>

namespace {
int runWebsiteMaintenance(const std::wstring& command) {
    if (!himaya::WebsiteBlocker::isAdministrator()) {
        MessageBoxW(nullptr, L"Administrator privileges are required.", L"Himaya Security", MB_OK | MB_ICONERROR);
        return 5;
    }
    himaya::WebsiteBlockResult result;
    if (command == L"--apply-sites") {
        himaya::ConfigStore store;
        if (!store.isReady()) {
            MessageBoxW(nullptr, store.lastError().c_str(), L"Himaya Security", MB_OK | MB_ICONERROR);
            return 2;
        }
        std::vector<std::wstring> domains;
        for (const auto& entry : store.load(L"websites")) domains.push_back(entry.value);
        result = himaya::WebsiteBlocker::apply(domains, store.getSetting(L"adult_block_mode", L"all") == L"all");
        if (result.success) store.logEvent(L"websites_applied", std::to_wstring(result.blockedDomains));
    } else {
        result = himaya::WebsiteBlocker::restore();
        if (result.success) {
            himaya::ConfigStore store;
            if (store.isReady()) store.logEvent(L"websites_restored", result.message);
        }
    }
    const std::wstring message = result.message + (result.success && result.blockedDomains
        ? L"\nDomains blocked: " + std::to_wstring(result.blockedDomains) : L"");
    MessageBoxW(nullptr, message.c_str(), L"Himaya Security", MB_OK | (result.success ? MB_ICONINFORMATION : MB_ICONERROR));
    return result.success ? 0 : 1;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2) {
        const std::wstring command = argv[1];
        LocalFree(argv);
        if (command == L"--apply-sites" || command == L"--restore-sites") return runWebsiteMaintenance(command);
        if (command == L"--restore-dns") {
            const auto result = himaya::DnsProtection::restore();
            MessageBoxW(nullptr,result.message.c_str(),L"Himaya Security",MB_OK|(result.success?MB_ICONINFORMATION:MB_ICONERROR));
            return result.success?0:1;
        }
    } else if (argv) {
        LocalFree(argv);
    }

    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    MainWindow window(instance);
    if (!window.create(showCommand)) {
        MessageBoxW(nullptr, L"Could not start Himaya.", L"Himaya", MB_OK | MB_ICONERROR);
        return 1;
    }
    return window.run();
}
