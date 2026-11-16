#include "../Core/ConfigStore.h"
#include "../Core/ExecutionPolicy.h"
#include "../Core/WebsiteBlocker.h"
#include <windows.h>
#include <string>
#include <vector>

namespace {
bool hostsContains(const char* needle) {
    wchar_t system[MAX_PATH]{};
    const UINT length=GetSystemDirectoryW(system,MAX_PATH);
    if(!length || length>=MAX_PATH) return false;
    const std::wstring path=std::wstring(system)+L"\\drivers\\etc\\hosts";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
                            OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size) || size.QuadPart<0 || size.QuadPart>32*1024*1024) {
        CloseHandle(file); return false;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart),'\0');
    DWORD read=0;
    const bool ok=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) &&
                  read==bytes.size();
    CloseHandle(file);
    return ok && bytes.find(needle)!=std::string::npos;
}
void printAppLockerDiagnostics() {
    wchar_t userName[256]{};DWORD userNameLength=static_cast<DWORD>(std::size(userName));
    if(GetUserNameW(userName,&userNameLength)) wprintf(L"Smoke test account: %ls\\n",userName);
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if(!manager) return;
    const wchar_t* names[]={L"AppIDSvc",L"appid",L"AppLockerFltr"};
    for(const auto* name:names) {
        SC_HANDLE service=OpenServiceW(manager,name,SERVICE_QUERY_STATUS);
        if(!service) {wprintf(L"Service %ls: unavailable (%lu)\n",name,GetLastError());continue;}
        SERVICE_STATUS_PROCESS status{};DWORD needed=0;
        if(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&needed))
            wprintf(L"Service %ls state: %lu\n",name,status.dwCurrentState);
        CloseServiceHandle(service);
    }
    CloseServiceHandle(manager);
}
bool launchProbe(const std::wstring& path,bool expectBlocked) {
    for(int attempt=0;attempt<10;++attempt) {
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        PROCESS_INFORMATION process{};
        std::wstring command=L"\""+path+L"\" --probe";
        std::vector<wchar_t> mutableCommand(command.begin(),command.end());mutableCommand.push_back(L'\0');
        const BOOL started=CreateProcessW(path.c_str(),mutableCommand.data(),nullptr,nullptr,FALSE,
            CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);
        if(!started) {
            const DWORD error=GetLastError();
            if(expectBlocked && error==ERROR_ACCESS_DISABLED_BY_POLICY) return true;
            if(!expectBlocked && error==ERROR_ACCESS_DISABLED_BY_POLICY) {
                wprintf(L"Probe still blocked after policy removal (attempt %d), Windows error %lu.\\n",attempt+1,error);
                Sleep(500);continue;
            }
            wprintf(L"Probe CreateProcessW failed (expectBlocked=%d), Windows error %lu.\\n",
                    expectBlocked?1:0,error);
            return false;
        }
        if(expectBlocked) {
            wprintf(L"Probe process unexpectedly started (PID %lu); AppLocker did not block it.\\n",process.dwProcessId);
            TerminateProcess(process.hProcess,1);
            CloseHandle(process.hThread);CloseHandle(process.hProcess);
            Sleep(500);
            continue;
        }
        const DWORD waitResult=WaitForSingleObject(process.hProcess,5000);
        DWORD exitCode=0;GetExitCodeProcess(process.hProcess,&exitCode);
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
        if(waitResult!=WAIT_OBJECT_0 || exitCode!=42) {
            wprintf(L"Probe launched after policy removal but did not finish as expected (wait=%lu, exit=%lu).\\n",
                    waitResult,exitCode);
            return false;
        }
        return true;
    }
    return false;
}

}
int wmain(int argc,wchar_t** argv) {
    if(argc==2 && _wcsicmp(argv[1],L"--probe")==0) return 42;
    wchar_t temp[MAX_PATH]{};
    if(!GetTempPathW(MAX_PATH,temp)) return 10;
    std::wstring testPath=std::wstring(temp)+L"HimayaPolicySmokeProbe.exe";
    wchar_t self[MAX_PATH]{};
    if(!GetModuleFileNameW(nullptr,self,MAX_PATH) || !CopyFileW(self,testPath.c_str(),FALSE)) return 11;
    himaya::ConfigStore store;
    auto websiteApplied=himaya::WebsiteBlocker::apply({L"himaya-user-test.invalid"},false);
    const bool websiteRulesPresent=websiteApplied.success &&
        hostsContains("0.0.0.0 himaya-user-test.invalid") &&
        hostsContains("0.0.0.0 www.himaya-user-test.invalid") &&
        !hostsContains("xnxx-arabic.com");
    const auto websiteRestored=himaya::WebsiteBlocker::restoreOriginal();
    if(!websiteRulesPresent || !websiteRestored.success) return 28;


    if(!store.isReady()) {DeleteFileW(testPath.c_str());return 13;}
    store.removeEntry(L"applications",testPath);
    if(!store.addEntry(L"applications",testPath,L"long",L"simple")) {DeleteFileW(testPath.c_str());return 14;}
    auto applied=himaya::ExecutionPolicy::synchronize(store);
    if(!applied.success) {
        wprintf(L"AppLocker apply failed: %ls\\n",applied.message.c_str());
        store.removeEntry(L"applications",testPath);
        himaya::ExecutionPolicy::synchronize(store);
        DeleteFileW(testPath.c_str());return 15;
    }
    if(!launchProbe(testPath,true)) {
        wprintf(L"AppLocker rule was written, but Windows did not block the probe process.\\n");
        printAppLockerDiagnostics();
        // Preserve the failing rule and probe executable on the disposable CI
        // runner so the workflow can inspect the actual local/effective policy
        // and registry state before any cleanup removes the evidence.
        return 29;
    }
    if(!store.removeEntry(L"applications",testPath)) {DeleteFileW(testPath.c_str());return 16;}
    auto removed=himaya::ExecutionPolicy::synchronize(store);
    wprintf(L"AppLocker removal result: success=%d, message=%ls\\n",removed.success?1:0,removed.message.c_str());
    const bool allowedAgain=removed.success && launchProbe(testPath,false);
    DeleteFileW(testPath.c_str());
    if(!removed.success || !allowedAgain) return 17;

    const std::wstring folder=std::wstring(temp)+L"HimayaPolicySmokeFolder";
    const std::wstring child=folder+L"\\probe.txt";
    RemoveDirectoryW(folder.c_str());
    if(!CreateDirectoryW(folder.c_str(),nullptr)) return 18;
    HANDLE childFile=CreateFileW(child.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(childFile==INVALID_HANDLE_VALUE){RemoveDirectoryW(folder.c_str());return 19;}
    const char sample[]="Himaya ACL test";DWORD childWritten=0;
    WriteFile(childFile,sample,sizeof(sample),&childWritten,nullptr);CloseHandle(childFile);
    if(!store.addEntry(L"paths",folder,L"long",L"simple")){DeleteFileW(child.c_str());RemoveDirectoryW(folder.c_str());return 20;}
    auto folderApplied=himaya::ExecutionPolicy::synchronize(store);
    HANDLE denied=CreateFileW(folder.c_str(),FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    const DWORD deniedError=GetLastError();
    if(denied!=INVALID_HANDLE_VALUE) CloseHandle(denied);
    if(!folderApplied.success || denied!=INVALID_HANDLE_VALUE || deniedError!=ERROR_ACCESS_DENIED) {
        store.removeEntry(L"paths",folder);himaya::ExecutionPolicy::synchronize(store);
        DeleteFileW(child.c_str());RemoveDirectoryW(folder.c_str());return 21;
    }
    if(!store.removeEntry(L"paths",folder)){himaya::ExecutionPolicy::restoreAll();return 22;}
    auto folderRestored=himaya::ExecutionPolicy::synchronize(store);
    HANDLE accessible=CreateFileW(folder.c_str(),FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    const bool accessRestored=accessible!=INVALID_HANDLE_VALUE;
    if(accessible!=INVALID_HANDLE_VALUE) CloseHandle(accessible);
    DeleteFileW(child.c_str());RemoveDirectoryW(folder.c_str());
    if (!folderRestored.success || !accessRestored) return 23;

    const std::wstring reportPath=std::wstring(temp)+L"Himaya-Activity-Smoke.csv";
    DeleteFileW(reportPath.c_str());
    if (!store.logEvent(L"policy_smoke_csv",L"CSV export verification")) return 24;
    if (!store.exportEventsCsv(reportPath)) return 25;
    HANDLE report=CreateFileW(reportPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,nullptr);
    if(report==INVALID_HANDLE_VALUE){DeleteFileW(reportPath.c_str());return 26;}
    LARGE_INTEGER reportSize{};
    const bool sizeOk=GetFileSizeEx(report,&reportSize) && reportSize.QuadPart>16 && reportSize.QuadPart<4*1024*1024;
    std::string reportBytes(sizeOk?static_cast<size_t>(reportSize.QuadPart):0,'\0');
    DWORD reportRead=0;
    const bool readOk=sizeOk && ReadFile(report,reportBytes.data(),static_cast<DWORD>(reportBytes.size()),&reportRead,nullptr) &&
                      reportRead==reportBytes.size();
    CloseHandle(report);
    DeleteFileW(reportPath.c_str());
    return readOk && reportBytes.find("policy_smoke_csv")!=std::string::npos ? 0 : 27;
}
