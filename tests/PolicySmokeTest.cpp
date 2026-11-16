#include "../Core/ConfigStore.h"
#include "../Core/ExecutionPolicy.h"
#include <windows.h>
#include <string>
#include <vector>

namespace {
bool registryContains(const std::wstring& expected) {
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,
       L"SOFTWARE\\Policies\\Microsoft\\Windows\\Safer\\CodeIdentifiers\\0\\Paths",
       0,KEY_READ|KEY_WOW64_64KEY,&key)!=ERROR_SUCCESS) return false;
    bool found=false;
    for(DWORD i=0;;++i) {
        wchar_t name[256]{};DWORD n=static_cast<DWORD>(std::size(name));
        if(RegEnumKeyExW(key,i,name,&n,nullptr,nullptr,nullptr,nullptr)!=ERROR_SUCCESS) break;
        HKEY child=nullptr;
        if(RegOpenKeyExW(key,name,0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&child)==ERROR_SUCCESS) {
            wchar_t value[32768]{};DWORD bytes=sizeof(value),type=0;
            DWORD level=0,levelBytes=sizeof(level),levelType=0;
            const bool disallowed=RegQueryValueExW(child,L"Level",nullptr,&levelType,
                reinterpret_cast<BYTE*>(&level),&levelBytes)==ERROR_SUCCESS &&
                levelType==REG_DWORD && level==0x00000000;
            if(RegQueryValueExW(child,L"ItemData",nullptr,&type,reinterpret_cast<BYTE*>(value),&bytes)==ERROR_SUCCESS &&
               type==REG_SZ && _wcsicmp(value,expected.c_str())==0 && disallowed) found=true;
            RegCloseKey(child);
        }
        if(found) break;
    }
    RegCloseKey(key);return found;
}
}
int wmain() {
    wchar_t temp[MAX_PATH]{};
    if(!GetTempPathW(MAX_PATH,temp)) return 10;
    std::wstring testPath=std::wstring(temp)+L"HimayaPolicySmokeTest.exe";
    HANDLE file=CreateFileW(testPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return 11;
    const char bytes[]="MZ";
    DWORD written=0;WriteFile(file,bytes,2,&written,nullptr);CloseHandle(file);
    if(written!=2) {DeleteFileW(testPath.c_str());return 12;}
    himaya::ConfigStore store;
    if(!store.isReady()) {DeleteFileW(testPath.c_str());return 13;}
    store.removeEntry(L"applications",testPath);
    if(!store.addEntry(L"applications",testPath,L"long",L"simple")) {DeleteFileW(testPath.c_str());return 14;}
    auto applied=himaya::ExecutionPolicy::synchronize(store);
    if(!applied.success || !registryContains(testPath)) {
        store.removeEntry(L"applications",testPath);
        himaya::ExecutionPolicy::synchronize(store);
        DeleteFileW(testPath.c_str());return 15;
    }
    if(!store.removeEntry(L"applications",testPath)) {DeleteFileW(testPath.c_str());return 16;}
    auto removed=himaya::ExecutionPolicy::synchronize(store);
    const bool stillPresent=registryContains(testPath);
    DeleteFileW(testPath.c_str());
    if(!removed.success || stillPresent) return 17;

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
