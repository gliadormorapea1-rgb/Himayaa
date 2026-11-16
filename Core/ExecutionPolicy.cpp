#include "ExecutionPolicy.h"
#include "ConfigStore.h"
#include "AppLockerPolicy.h"
#include <windows.h>
#include <WtsApi32.h>
#include <aclapi.h>
#include <sddl.h>
#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <iterator>
#include <filesystem>
#include <string>
#include <vector>
#include <set>

namespace himaya {
namespace {
constexpr wchar_t kBase[] = L"SOFTWARE\\Policies\\Microsoft\\Windows\\Safer\\CodeIdentifiers";
constexpr wchar_t kPaths[] = L"SOFTWARE\\Policies\\Microsoft\\Windows\\Safer\\CodeIdentifiers\\0\\Paths";
constexpr wchar_t kFolderBackupRoot[] = L"SOFTWARE\\Himaya\\Security\\FolderAccessBackups";
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
    const auto userMarker=lower(path).find(L"\\users\\");
    if(userMarker!=std::wstring::npos) {
        const auto remainder=path.substr(userMarker+7);
        if(!remainder.empty() && remainder.find_first_of(L"\\/")==std::wstring::npos) return true;
    }
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

std::vector<std::wstring> targetUserSids() {
    std::set<std::wstring> unique;
    HANDLE processToken=nullptr;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&processToken)) {
        DWORD bytes=0; GetTokenInformation(processToken,TokenUser,nullptr,0,&bytes);
        std::vector<BYTE> buffer(bytes);
        if(bytes && GetTokenInformation(processToken,TokenUser,buffer.data(),bytes,&bytes)) {
            auto* user=reinterpret_cast<TOKEN_USER*>(buffer.data());
            BYTE systemBuffer[SECURITY_MAX_SID_SIZE]{}; DWORD systemSize=sizeof(systemBuffer);
            const bool isSystem=CreateWellKnownSid(WinLocalSystemSid,nullptr,systemBuffer,&systemSize) &&
                                EqualSid(user->User.Sid,systemBuffer);
            if(!isSystem) {
                LPWSTR raw=nullptr;
                if(ConvertSidToStringSidW(user->User.Sid,&raw) && raw) { unique.insert(raw);LocalFree(raw); }
            }
        }
        CloseHandle(processToken);
    }
    if(unique.empty()) {
        PWTS_SESSION_INFOW sessions=nullptr; DWORD count=0;
        if(WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE,0,1,&sessions,&count)) {
            for(DWORD i=0;i<count;++i) {
                if(sessions[i].State!=WTSActive) continue;
                HANDLE token=nullptr;
                if(!WTSQueryUserToken(sessions[i].SessionId,&token)) continue;
                DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
                std::vector<BYTE> buffer(bytes);
                if(bytes && GetTokenInformation(token,TokenUser,buffer.data(),bytes,&bytes)) {
                    auto* user=reinterpret_cast<TOKEN_USER*>(buffer.data());
                    LPWSTR raw=nullptr;
                    if(ConvertSidToStringSidW(user->User.Sid,&raw) && raw) {unique.insert(raw);LocalFree(raw);}
                }
                CloseHandle(token);
            }
            WTSFreeMemory(sessions);
        }
    }
    return std::vector<std::wstring>(unique.begin(),unique.end());
}

std::wstring fingerprint(const std::vector<Rule>& rules,const std::vector<std::wstring>& sids) {
    std::wstring s;
    for(const auto& r:rules){s+=r.path;s+=L'\n';s+=r.description;s+=L'\n';}
    for(const auto& sid:sids){s+=sid;s+=L'\n';}
    return s;
}
bool configureSrp() {
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,kBase,0,nullptr,0,KEY_SET_VALUE|KEY_WOW64_64KEY,nullptr,&key,nullptr)!=ERROR_SUCCESS) return false;
    const bool ok=setDword(key,L"DefaultLevel",0x00040000) &&
                  setDword(key,L"TransparentEnabled",1) &&
                  setDword(key,L"PolicyScope",1) &&
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
                  setDword(child,L"SaferFlags",0) &&
                  setDword(child,L"Level",0x00000000);
    RegCloseKey(child); return ok;
}
bool getOriginalSddl(const std::wstring& path,std::wstring& sddl) {
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    const DWORD rc=GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        nullptr,nullptr,nullptr,nullptr,&descriptor);
    if(rc!=ERROR_SUCCESS) return false;
    LPWSTR raw=nullptr;
    const BOOL converted=ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor,
        SDDL_REVISION_1,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        &raw,nullptr);
    LocalFree(descriptor);
    if(!converted) return false;
    sddl=raw; LocalFree(raw); return true;
}
bool readBackup(const std::wstring& id,std::wstring& path,std::wstring& sddl) {
    HKEY root=nullptr,child=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,kFolderBackupRoot,0,KEY_READ|KEY_WOW64_64KEY,&root)!=ERROR_SUCCESS) return false;
    if(RegOpenKeyExW(root,id.c_str(),0,KEY_READ|KEY_WOW64_64KEY,&child)!=ERROR_SUCCESS){RegCloseKey(root);return false;}
    auto read=[&](const wchar_t* name,std::wstring& out){
        DWORD type=0,bytes=0;
        if(RegQueryValueExW(child,name,nullptr,&type,nullptr,&bytes)!=ERROR_SUCCESS || type!=REG_SZ || bytes<sizeof(wchar_t)) return false;
        out.resize(bytes/sizeof(wchar_t));
        if(RegQueryValueExW(child,name,nullptr,&type,reinterpret_cast<BYTE*>(out.data()),&bytes)!=ERROR_SUCCESS) return false;
        if(!out.empty() && out.back()==L'\0') out.pop_back();
        return true;
    };
    const bool ok=read(L"Path",path)&&read(L"OriginalSddl",sddl);
    RegCloseKey(child);RegCloseKey(root);return ok;
}
bool saveBackup(const std::wstring& id,const std::wstring& path) {
    HKEY root=nullptr,child=nullptr;
    if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,kFolderBackupRoot,0,nullptr,0,
       KEY_CREATE_SUB_KEY|KEY_WOW64_64KEY,nullptr,&root,nullptr)!=ERROR_SUCCESS) return false;
    if(RegCreateKeyExW(root,id.c_str(),0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE|KEY_WOW64_64KEY,
       nullptr,&child,nullptr)!=ERROR_SUCCESS){RegCloseKey(root);return false;}
    DWORD type=0,bytes=0;
    if(RegQueryValueExW(child,L"OriginalSddl",nullptr,&type,nullptr,&bytes)==ERROR_SUCCESS) {
        std::wstring oldPath,oldSddl;
        const bool ok=readBackup(id,oldPath,oldSddl) && _wcsicmp(oldPath.c_str(),path.c_str())==0;
        RegCloseKey(child);RegCloseKey(root);return ok;
    }
    std::wstring sddl;
    const bool got=getOriginalSddl(path,sddl);
    const bool ok=got && setString(child,L"Path",path) && setString(child,L"OriginalSddl",sddl);
    RegCloseKey(child);RegCloseKey(root);return ok;
}
bool usersDenyAcePresent(PACL dacl,PSID usersSid) {
    if(!dacl) return false;
    ACL_SIZE_INFORMATION info{};
    if(!GetAclInformation(dacl,&info,sizeof(info),AclSizeInformation)) return false;
    for(DWORD i=0;i<info.AceCount;++i) {
        LPVOID raw=nullptr;
        if(!GetAce(dacl,i,&raw)) continue;
        auto* header=static_cast<ACE_HEADER*>(raw);
        if(header->AceType!=ACCESS_DENIED_ACE_TYPE) continue;
        auto* ace=static_cast<ACCESS_DENIED_ACE*>(raw);
        if((ace->Mask&FILE_ALL_ACCESS)==FILE_ALL_ACCESS && EqualSid(&ace->SidStart,usersSid)) return true;
    }
    return false;
}
bool applyFolderAccessDeny(const std::wstring& path) {
    const auto id=ruleId(path);
    if(!saveBackup(id,path)) return false;
    PACL oldDacl=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
    const DWORD query=GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,nullptr,nullptr,&oldDacl,nullptr,&descriptor);
    if(query!=ERROR_SUCCESS) return false;
    BYTE usersBuffer[SECURITY_MAX_SID_SIZE]{};DWORD usersSize=sizeof(usersBuffer);
    if(!CreateWellKnownSid(WinBuiltinUsersSid,nullptr,usersBuffer,&usersSize)){LocalFree(descriptor);return false;}
    if(usersDenyAcePresent(oldDacl,usersBuffer)){LocalFree(descriptor);return true;}
    EXPLICIT_ACCESSW deny{};deny.grfAccessPermissions=FILE_ALL_ACCESS;deny.grfAccessMode=DENY_ACCESS;
    deny.grfInheritance=SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    BuildTrusteeWithSidW(&deny.Trustee,usersBuffer);
    PACL newDacl=nullptr;
    const DWORD build=SetEntriesInAclW(1,&deny,oldDacl,&newDacl);
    LocalFree(descriptor);
    if(build!=ERROR_SUCCESS) return false;
    const DWORD set=SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,nullptr,nullptr,newDacl,nullptr);
    LocalFree(newDacl);
    return set==ERROR_SUCCESS;
}
bool restoreFolderAccess(const std::wstring& id,const std::wstring& path,const std::wstring& sddl) {
    if(exists(path)) {
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)) return false;
        const BOOL ok=SetFileSecurityW(path.c_str(),DACL_SECURITY_INFORMATION,descriptor);
        LocalFree(descriptor);
        if(!ok) return false;
    }
    HKEY root=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,kFolderBackupRoot,0,KEY_WRITE|KEY_WOW64_64KEY,&root)==ERROR_SUCCESS) {
        RegDeleteTreeW(root,id.c_str());RegCloseKey(root);
    }
    return true;
}
bool synchronizeFolderAccess(const std::vector<std::wstring>& folders) {
    std::vector<std::wstring> activeIds;
    for(const auto& path:folders) {
        const auto id=ruleId(path);activeIds.push_back(id);
        if(!applyFolderAccessDeny(path)) return false;
    }
    HKEY root=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,kFolderBackupRoot,0,KEY_ENUMERATE_SUB_KEYS|KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,&root)!=ERROR_SUCCESS) return true;
    for(DWORD index=0;;) {
        wchar_t idBuffer[256]{};DWORD n=static_cast<DWORD>(std::size(idBuffer));
        const LONG rc=RegEnumKeyExW(root,index,idBuffer,&n,nullptr,nullptr,nullptr,nullptr);
        if(rc==ERROR_NO_MORE_ITEMS) break;
        if(rc!=ERROR_SUCCESS){RegCloseKey(root);return false;}
        const std::wstring id(idBuffer,n);
        if(std::find(activeIds.begin(),activeIds.end(),id)!=activeIds.end()){++index;continue;}
        std::wstring path,sddl;
        if(!readBackup(id,path,sddl)){RegCloseKey(root);return false;}
        if(!restoreFolderAccess(id,path,sddl)){RegCloseKey(root);return false;}
    }
    RegCloseKey(root);return true;
}
}
PolicySyncResult ExecutionPolicy::synchronize(ConfigStore& store) {
    if(!store.isReady()) return {false,0,L"The Himaya database is unavailable to the service."};
    std::vector<Rule> rules;
    std::vector<std::wstring> folders;
    for(const auto& e:store.load(L"applications")) {
        auto p=fullPath(e.value);
        if(!p.empty() && exists(p) && !isDirectory(p))
            rules.push_back({p,L"application"});
    }
    for(const auto& e:store.load(L"paths")) {
        auto p=fullPath(e.value);
        if(!p.empty() && isDirectory(p) && !protectedFolder(p)) {
            folders.push_back(p);
            rules.push_back({p+L"\\*",L"folder-execution"});
        }
    }
    std::sort(rules.begin(),rules.end(),[](const Rule&a,const Rule&b){return a.path<b.path;});
    rules.erase(std::unique(rules.begin(),rules.end(),[](const Rule&a,const Rule&b){return lower(a.path)==lower(b.path);}),rules.end());
    const auto sids=targetUserSids();
    static std::wstring last;
    const auto current=fingerprint(rules,sids);
    if(current==last) {
        if(!synchronizeFolderAccess(folders)) return {false,0,L"Could not apply or restore folder access permissions."};
        return {true,rules.size(),L"Execution and access policies are up to date."};
    }
    std::vector<AppLockerRule> appRules;
    appRules.reserve(rules.size());
    for(const auto& rule:rules) appRules.push_back({rule.path,rule.description});
    std::wstring policyError;
    if(!AppLockerPolicy::synchronize(appRules,sids,store.directory(),policyError)) {
        return {false,0,L"Could not apply Windows AppLocker rules. "+policyError};
    }
    // Keep legacy SRP rules only until AppLocker has been successfully written.
    if(!removeManagedRules()) return {false,0,L"AppLocker was updated, but legacy Himaya SRP rules could not be removed."};
    if(!synchronizeFolderAccess(folders)) return {false,0,L"Could not apply or restore folder access permissions."};
    last=current;
    store.logEvent(L"execution_policy_synced",std::to_wstring(rules.size()));
    return {true,rules.size(),L"AppLocker execution rules and folder access restrictions were updated."};
}
PolicySyncResult ExecutionPolicy::restoreAll() {
    wchar_t programData[MAX_PATH]{};
    const DWORD length=GetEnvironmentVariableW(L"ProgramData",programData,MAX_PATH);
    if(!length || length>=MAX_PATH) return {false,0,L"Could not locate the ProgramData directory to restore AppLocker policy."};
    const std::wstring workingDirectory=std::wstring(programData)+L"\\Himaya";
    std::wstring error;
    if(!AppLockerPolicy::restore(workingDirectory,error))
        return {false,0,L"Could not remove Himaya AppLocker rules: "+error};
    if(!removeManagedRules()) return {false,0,L"Could not remove legacy Himaya SRP rules."};
    if(!synchronizeFolderAccess({})) return {false,0,L"Could not restore permissions for some folders."};
    return {true,0,L"Himaya AppLocker and legacy SRP rules were removed; folder permissions were restored."};
}
}
