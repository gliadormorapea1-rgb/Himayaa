#include "../Core/ConfigStore.h"
#include "../Core/ExecutionPolicy.h"
#include "../Core/DnsProtection.h"
#include <windows.h>
#include <string>

namespace {
SERVICE_STATUS_HANDLE g_statusHandle=nullptr;
SERVICE_STATUS g_status{};
HANDLE g_stopEvent=nullptr;
void report(DWORD state,DWORD win32Exit=NO_ERROR,DWORD waitHint=0) {
    g_status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState=state;
    g_status.dwWin32ExitCode=win32Exit;
    g_status.dwWaitHint=waitHint;
    g_status.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN:0;
    if(g_statusHandle) SetServiceStatus(g_statusHandle,&g_status);
}
DWORD WINAPI control(DWORD code,DWORD,void*,void*) {
    if(code==SERVICE_CONTROL_STOP||code==SERVICE_CONTROL_SHUTDOWN){
        report(SERVICE_STOP_PENDING,NO_ERROR,5000);
        if(g_stopEvent) SetEvent(g_stopEvent);
    }
    return NO_ERROR;
}
void WINAPI serviceMain(DWORD,LPWSTR*) {
    g_statusHandle=RegisterServiceCtrlHandlerExW(L"HimayaGuard",control,nullptr);
    if(!g_statusHandle) return;
    report(SERVICE_START_PENDING,NO_ERROR,5000);
    g_stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!g_stopEvent){report(SERVICE_STOPPED,GetLastError());return;}
    himaya::ConfigStore store;
    if(!store.isReady()){
        report(SERVICE_STOPPED,ERROR_DATABASE_FAILURE);
        CloseHandle(g_stopEvent);g_stopEvent=nullptr;return;
    }
    report(SERVICE_RUNNING);
    store.logEvent(L"guard_service_started",L"Windows service started");
    for(;;){
        const auto result=himaya::ExecutionPolicy::synchronize(store);
        if(!result.success) store.logEvent(L"execution_policy_error",result.message);
        if(store.getSetting(L"adult_dns_enabled",L"0")==L"1") {
            const auto dns=himaya::DnsProtection::synchronize();
            if(!dns.success) store.logEvent(L"adult_dns_sync_error",dns.message);
        }
        const DWORD wait=WaitForSingleObject(g_stopEvent,30000);
        if(wait==WAIT_OBJECT_0) break;
        if(wait==WAIT_FAILED) break;
    }
    const auto restored=himaya::ExecutionPolicy::restoreAll();
    store.logEvent(L"guard_service_stopped",restored.message);
    report(SERVICE_STOPPED,restored.success?NO_ERROR:ERROR_GEN_FAILURE);
    CloseHandle(g_stopEvent);g_stopEvent=nullptr;
}
}
int wmain() {
    SERVICE_TABLE_ENTRYW table[]={{const_cast<LPWSTR>(L"HimayaGuard"),serviceMain},{nullptr,nullptr}};
    if(!StartServiceCtrlDispatcherW(table)) return static_cast<int>(GetLastError());
    return 0;
}
