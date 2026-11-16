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
    void removeEntry();
    std::wstring selectedCategory() const;

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND category_{}, list_{}, input_{}, status_{};
    himaya::ConfigStore store_;
};
