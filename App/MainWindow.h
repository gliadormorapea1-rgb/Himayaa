#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "Core/ConfigStore.h"

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);
    bool create(int showCommand);
    int run();
private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT, WPARAM, LPARAM);
    void refreshList();
    void addEntry();
    void browseEntry();
    void removeEntry();
    void saveProfile();
    void applyWebsites();
    void restoreWebsites();
    std::wstring selectedCategory() const;
    std::wstring selectedDuration() const;
    std::wstring selectedProfile() const;
    void setStatus(const std::wstring& value);

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND category_{}, profile_{}, duration_{}, list_{}, input_{}, browse_{}, status_{};
    HWND settings_{}, summary_{}, applySites_{}, restoreSites_{};
    std::vector<himaya::Entry> visibleEntries_;
    himaya::ConfigStore store_;
};
