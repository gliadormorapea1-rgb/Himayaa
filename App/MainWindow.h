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
    void setAdultDnsProtection(bool enabled);
    void restoreWebsites();
    void restoreOriginalHosts();
    void exportReport();
    std::wstring selectedCategory() const;
    std::wstring selectedDuration() const;
    std::wstring selectedReelsDuration() const;
    std::wstring selectedProfile() const;
    void setStatus(const std::wstring& value);
    void setPage(int page);
    void layoutControls();
    void applyTheme();
    void toggleTheme();
    void refreshDashboard();
    void refreshStatistics();
    void refreshHealth();
    void startReelsSession();
    void stopReelsSession();
    void refreshReelsStatus();
    void syncReelsBridge();

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND category_{}, profile_{}, duration_{}, list_{}, input_{}, browse_{}, status_{};
    HWND settings_{}, summary_{}, applySites_{}, restoreSites_{}, restoreOriginalHosts_{}, enableAdultDns_{}, disableAdultDns_{};
    HWND pageTitle_{}, pageSubtitle_{}, categoryLabel_{}, profileLabel_{}, durationLabel_{}, inputLabel_{};
    HWND refreshButton_{}, reportButton_{}, addButton_{}, removeButton_{};
    HWND protectionDescription_{}, statsSummary_{}, settingsDescription_{}, themeToggle_{};
    HWND healthSummary_{}, healthRefresh_{};
    HWND reelsModeLabel_{}, reelsMode_{}, reelsDurationLabel_{}, reelsDuration_{}, reelsDateLabel_{}, reelsDate_{}, reelsSitesLabel_{}, reelsSites_{}, reelsEnable_{}, reelsStop_{}, reelsStatus_{}, navReels_{}, navSocial_{};
    HWND durationDateLabel_{}, durationDate_{};
    HWND dashboardApps_{}, dashboardWebsites_{}, dashboardFolders_{}, dashboardDns_{};
    HWND navDashboard_{}, navApplications_{}, navWebsites_{}, navProtection_{}, navLogs_{}, navStatistics_{}, navSettings_{};
    std::vector<HWND> managementControls_, dashboardControls_, protectionControls_, statisticsControls_, settingsControls_, healthControls_, reelsControls_;
    int currentPage_{0};
    bool darkTheme_{true};
    std::vector<himaya::Entry> visibleEntries_;
    himaya::ConfigStore store_;
};
