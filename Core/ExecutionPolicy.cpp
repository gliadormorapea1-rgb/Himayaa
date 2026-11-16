#include "ExecutionPolicy.h"
#include "ConfigStore.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <iterator>
#include <filesystem>
#include <string>
#include <vector>

namespace himaya {
namespace {
constexpr wchar_t kBase[] = L"SOFTWARE\\Policies\\Microsoft\\Windows\\Safer\\CodeIdentifiers";
constexpr wchar_t kPaths[] = L"SOFTWARE\\Policies\\Microsoft\\Windows\\Safer\\CodeIdentifiers\\0\\Paths";
struct Rule { std::wstring path; std::wstring description; };
std::wstring lower(std::wstring s) {
    std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
    return s;
}
bool isDirectory(const std::wstring& p) {
    const DWORD a=GetFileAttributesW(p.c_str());
    return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_DIRECTORY);
}
bool exists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES;
}
std::wstring fullPath(const std::wstring& value) {
    if(value.empty() || value.find_first_of(L"*?")!=std::wstring::npos) return {};
    DWORD n=GetFullPathNameW(value.c_str(),0,nullptr,nullptr);
    if(!n || n>32767) return {};
    std::wstring out(n,L'\0');
    DWORD got=GetFullPathNameW(value.c_str(),n,out.data(),nullptr);
    if(!got || got>=n) return {};
    out.resize(got);
    while(out.size()>3 && (out.back()==L'\\' || out.back()==L'/')) out.pop_back();
    return out;
}
bool within(const std::wstring& path,const std::wstring& root) {
    const auto p=lower(path), r=lower(root);
    return p==r || (p.size()>r.size() && p.compare(0,r.size(),r)==0 &&
        (r.back()==L'\\' || p[r.size()]==L'\\'));
}
bool protectedFolder(const std::wstring& path) {
    wchar_t windows[MAX_PATH]{},system[MAX_PATH]{},programData[MAX_PATH]{},programFiles[MAX_PATH]{},programFilesX86[MAX_PATH]{};
    GetWindowsDirectoryW(windows,MAX_PATH); GetSystemDirectoryW(system,MAX_PATH);
    GetEnvironmentVariableW(L"ProgramData",programData,MAX_PATH);
    GetEnvironmentVariableW(L"ProgramFiles",programFiles,MAX_PATH);
    GetEnvironmentVariableW(L"ProgramFiles(x86)",programFilesX86,MAX_PATH);
    const std::wstring roots[]={windows,system,programData,programFiles,programFilesX86};
    for(const auto& r:roots) if(!r.empty() && within(path,r)) return true;
    if(path.size()==3 && path[1]==L':' && (path[2]==L'\\'||path[2]==L'/')) return true;
    return false;
}
std::wstring ruleId(const std::wstring& path) {
    unsigned long long h=1469598103934665603ull;
    for(wchar_t c:lower(path)){h^=static_cast<unsigned long long>(c);h*=1099511628211ull;}
    wchar_t b[32]{}; swprintf_s(b,L"%016llX",h); return b;
}
bool setString(HKEY key,const wchar_t* name,const std::wstring& value) {
    return RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS;
}
bool setDword(HKEY key,const wchar_t* name,DWORD value) {
    return RegSetValueExW(key,name,0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value))==ERROR_SUCCESS;
}
std::wstring fingerprint(const std::vector<Rule>& rules) {
    std::wstring s;
    for(const auto& r:rules){s+=r.path;s+=L'\n';s+=r.description;s+=L'\n';}
    return s;
}
bool configureSrp() {
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,kBase,0,nullptr,0,KEY_SET_VALUE|KEY_WOW64_64KEY,nullptr,&key,nullptr)!=ERROR_SUCCESS) return false;
    const bool ok=setDword(key,L"DefaultLevel",0x00040000) &&
                  setDword(key,L"TransparentEnabled",1) &&
                  setDword(key,L"PolicyScope",0) &&
                  setDword(key,L"AuthenticodeEnabled",0);
    RegCloseKey(key); return ok;
}
bool removeManagedRules() {
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,kPaths,0,KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,&key)!=ERROR_SUCCESS) return true;
    for(DWORD index=0;;) {
        wchar_t name[256]{}; DWORD n=static_cast<DWORD>(std::size(name));
        const LONG rc=RegEnumKeyExW(key,index,name,&n,nullptr,nullptr,nullptr,nullptr);
        if(rc==ERROR_NO_MORE_ITEMS) break;
        if(rc!=ERROR_SUCCESS){RegCloseKey(key);return false;}
        HKEY child=nullptr; bool managed=false;
        if(RegOpenKeyExW(key,name,0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&child)==ERROR_SUCCESS){
            wchar_t desc[256]{};DWORD bytes=sizeof(desc),type=0;
            if(RegQueryValueExW(child,L"Description",nullptr,&type,reinterpret_cast<BYTE*>(desc),&bytes)==ERROR_SUCCESS &&
               type==REG_SZ && wcsncmp(desc,L"Himaya Security:",16)==0) managed=true;
            RegCloseKey(child);
        }
        if(managed){if(RegDeleteTreeW(key,name)!=ERROR_SUCCESS){RegCloseKey(key);return false;}}
        else ++index;
    }
    RegCloseKey(key); return true;
}
bool addRule(const Rule& rule) {
    HKEY parent=nullptr;
    if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,kPaths,0,nullptr,0,KEY_CREATE_SUB_KEY|KEY_WOW64_64KEY,nullptr,&parent,nullptr)!=ERROR_SUCCESS) return false;
    const auto id=ruleId(rule.path);
    HKEY child=nullptr;
    const LONG rc=RegCreateKeyExW(parent,id.c_str(),0,nullptr,0,KEY_SET_VALUE|KEY_WOW64_64KEY,nullptr,&child,nullptr);
    RegCloseKey(parent);
    if(rc!=ERROR_SUCCESS) return false;
    const bool ok=setString(child,L"ItemData",rule.path) &&
                  setString(child,L"Description",L"Himaya Security:"+rule.description) &&
                  setDword(child,L"SaferFlags",0);
    RegCloseKey(child); return ok;
}
}
PolicySyncResult ExecutionPolicy::synchronize(ConfigStore& store) {
    if(!store.isReady()) return {false,0,L"قاعدة بيانات Himaya غير متاحة للخدمة."};
    std::vector<Rule> rules;
    for(const auto& e:store.load(L"applications")) {
        auto p=fullPath(e.value);
        if(!p.empty() && exists(p) && !isDirectory(p))
            rules.push_back({p,L"application"});
    }
    for(const auto& e:store.load(L"paths")) {
        auto p=fullPath(e.value);
        if(!p.empty() && isDirectory(p) && !protectedFolder(p))
            rules.push_back({p+L"\\*",L"folder-execution"});
    }
    std::sort(rules.begin(),rules.end(),[](const Rule&a,const Rule&b){return a.path<b.path;});
    rules.erase(std::unique(rules.begin(),rules.end(),[](const Rule&a,const Rule&b){return lower(a.path)==lower(b.path);}),rules.end());
    static std::wstring last;
    const auto current=fingerprint(rules);
    if(current==last) return {true,rules.size(),L"سياسات التشغيل محدثة."};
    if(!configureSrp()) return {false,0,L"تعذر تهيئة سياسات Windows Software Restriction Policies."};
    if(!removeManagedRules()) return {false,0,L"تعذر إزالة قواعد Himaya القديمة بأمان."};
    for(const auto& rule:rules) if(!addRule(rule)) return {false,0,L"تعذر تسجيل إحدى قواعد منع التشغيل في Windows."};
    last=current;
    DWORD_PTR ignored=0;
    SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,reinterpret_cast<LPARAM>(L"Software\\Policies\\Microsoft\\Windows\\Safer\\CodeIdentifiers"),SMTO_ABORTIFHUNG,2000,&ignored);
    store.logEvent(L"execution_policy_synced",std::to_wstring(rules.size()));
    return {true,rules.size(),L"تم تحديث قواعد منع تشغيل التطبيقات والملفات داخل المجلدات المحددة."};
}
}
