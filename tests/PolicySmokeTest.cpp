#include "../Core/ConfigStore.h"
#include "../Core/ExecutionPolicy.h"
#include <windows.h>
#include <string>

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
            if(RegQueryValueExW(child,L"ItemData",nullptr,&type,reinterpret_cast<BYTE*>(value),&bytes)==ERROR_SUCCESS &&
               type==REG_SZ && _wcsicmp(value,expected.c_str())==0) found=true;
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
    return removed.success && !stillPresent ? 0 : 17;
}
