#include "AppLockerPolicy.h"
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace himaya {
namespace {
std::wstring quote(const std::wstring& value) {
    std::wstring out(1,L'"');
    out+=value;
    out.push_back(L'"');
    return out;
}
bool writeUtf16(const std::wstring& path,const std::wstring& value) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
                            FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    const wchar_t bom=0xFEFF;
    DWORD written=0;
    bool ok=WriteFile(file,&bom,sizeof(bom),&written,nullptr)&&written==sizeof(bom);
    size_t offset=0;
    while(ok && offset<value.size()) {
        const DWORD bytes=static_cast<DWORD>(std::min<size_t>((value.size()-offset)*sizeof(wchar_t),1024*1024));
        ok=WriteFile(file,value.data()+offset,bytes,&written,nullptr)&&written==bytes;
        offset+=written/sizeof(wchar_t);
    }
    if(!FlushFileBuffers(file)) ok=false;
    CloseHandle(file);
    return ok;
}
std::wstring readUtf8(const std::wstring& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
                            OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size)||size.QuadPart<=0||size.QuadPart>65536) {CloseHandle(file);return {};}
    std::string bytes(static_cast<size_t>(size.QuadPart),'\0');
    DWORD got=0;
    const bool ok=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size();
    CloseHandle(file);
    if(!ok) return {};
    if(bytes.size()>=3 && static_cast<unsigned char>(bytes[0])==0xEF &&
       static_cast<unsigned char>(bytes[1])==0xBB && static_cast<unsigned char>(bytes[2])==0xBF) bytes.erase(0,3);
    const int count=MultiByteToWideChar(CP_UTF8,0,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
    if(count<=0) return {};
    std::wstring result(static_cast<size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,0,bytes.data(),static_cast<int>(bytes.size()),result.data(),count);
    return result;
}
constexpr wchar_t kAppIdBackupKey[]=L"SOFTWARE\\Himaya\\Security\\AppIDSvcBackup";
bool readRegistryDword(HKEY key,const wchar_t* name,DWORD& value) {
    DWORD type=0,bytes=sizeof(value);
    return RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(&value),&bytes)==ERROR_SUCCESS &&
           type==REG_DWORD && bytes==sizeof(value);
}
bool saveAppIdConfiguration(SC_HANDLE service,const SERVICE_STATUS_PROCESS& status,std::wstring& error) {
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,kAppIdBackupKey,0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key)==ERROR_SUCCESS) {
        DWORD oldStart=0,wasRunning=0;
        const bool valid=readRegistryDword(key,L"StartType",oldStart)&&readRegistryDword(key,L"WasRunning",wasRunning);
        RegCloseKey(key);
        if(valid) return true;
        error=L"The saved Application Identity service configuration is incomplete; refusing to overwrite it.";
        return false;
    }
    DWORD bytes=0;
    QueryServiceConfigW(service,nullptr,0,&bytes);
    if(!bytes){error=L"Could not read the original Application Identity startup configuration.";return false;}
    std::vector<BYTE> buffer(bytes);
    auto* config=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
    if(!QueryServiceConfigW(service,config,bytes,&bytes)) {
        error=L"Could not read the original Application Identity startup configuration.";
        return false;
    }
    if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,kAppIdBackupKey,0,nullptr,0,KEY_SET_VALUE|KEY_WOW64_64KEY,
                       nullptr,&key,nullptr)!=ERROR_SUCCESS) {
        error=L"Could not save the original Application Identity service configuration.";
        return false;
    }
    const DWORD wasRunning=status.dwCurrentState==SERVICE_RUNNING?1u:0u;
    const LONG first=RegSetValueExW(key,L"StartType",0,REG_DWORD,reinterpret_cast<const BYTE*>(&config->dwStartType),sizeof(DWORD));
    const LONG second=RegSetValueExW(key,L"WasRunning",0,REG_DWORD,reinterpret_cast<const BYTE*>(&wasRunning),sizeof(DWORD));
    RegCloseKey(key);
    if(first!=ERROR_SUCCESS||second!=ERROR_SUCCESS) {
        error=L"Could not save the original Application Identity service configuration.";
        return false;
    }
    return true;
}
bool restoreAppIdConfiguration(std::wstring& error) {
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,kAppIdBackupKey,0,KEY_READ|KEY_WOW64_64KEY,&key)!=ERROR_SUCCESS) return true;
    DWORD startType=SERVICE_DEMAND_START,wasRunning=0;
    const bool valid=readRegistryDword(key,L"StartType",startType)&&readRegistryDword(key,L"WasRunning",wasRunning);
    RegCloseKey(key);
    if(!valid){error=L"The saved Application Identity service configuration is invalid.";return false;}
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if(!manager){error=L"Could not open the Service Control Manager to restore Application Identity.";return false;}
    SC_HANDLE service=OpenServiceW(manager,L"AppIDSvc",SERVICE_QUERY_STATUS|SERVICE_CHANGE_CONFIG|SERVICE_STOP);
    if(!service){error=L"Could not open AppIDSvc to restore its startup configuration.";CloseServiceHandle(manager);return false;}
    SERVICE_STATUS_PROCESS status{};DWORD needed=0;
    bool ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&needed)!=FALSE;
    if(ok && wasRunning==0 && status.dwCurrentState!=SERVICE_STOPPED) {
        SERVICE_STATUS stopped{};
        if(!ControlService(service,SERVICE_CONTROL_STOP,&stopped) && GetLastError()!=ERROR_SERVICE_NOT_ACTIVE) ok=false;
        for(int i=0;ok && i<30;++i) {
            Sleep(250);
            if(!QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&needed)){ok=false;break;}
            if(status.dwCurrentState==SERVICE_STOPPED) break;
        }
        if(ok && status.dwCurrentState!=SERVICE_STOPPED) ok=false;
    }
    if(ok && !ChangeServiceConfigW(service,SERVICE_NO_CHANGE,startType,SERVICE_NO_CHANGE,
                                   nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)) ok=false;
    const DWORD winError=GetLastError();
    CloseServiceHandle(service);CloseServiceHandle(manager);
    if(!ok) {
        error=L"Could not restore the original Application Identity service configuration. Windows error: "+std::to_wstring(winError);
        return false;
    }
    RegDeleteTreeW(HKEY_LOCAL_MACHINE,kAppIdBackupKey);
    return true;
}
bool ensureAppIdRunning(std::wstring& error) {
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if(!manager){error=L"Could not open the Windows Service Control Manager.";return false;}
    SC_HANDLE service=OpenServiceW(manager,L"AppIDSvc",SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG|SERVICE_CHANGE_CONFIG|SERVICE_START);
    if(!service){error=L"Application Identity (AppIDSvc) is unavailable. Windows error: "+std::to_wstring(GetLastError());CloseServiceHandle(manager);return false;}
    SERVICE_STATUS_PROCESS status{};DWORD needed=0;
    bool ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&needed)!=FALSE;
    if(ok) ok=saveAppIdConfiguration(service,status,error);
    if(ok && !ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_AUTO_START,SERVICE_NO_CHANGE,
                                   nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)) {
        error=L"Could not configure Application Identity to start automatically. Windows error: "+std::to_wstring(GetLastError());
        ok=false;
    }
    if(ok && status.dwCurrentState!=SERVICE_RUNNING) {
        if(status.dwCurrentState==SERVICE_STOPPED) {
            if(!StartServiceW(service,0,nullptr) && GetLastError()!=ERROR_SERVICE_ALREADY_RUNNING) ok=false;
        }
        for(int i=0;ok && i<30;++i) {
            Sleep(500);
            if(!QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&needed)){ok=false;break;}
            if(status.dwCurrentState==SERVICE_RUNNING) break;
        }
        if(ok && status.dwCurrentState!=SERVICE_RUNNING) ok=false;
    }
    if(!ok && error.empty()) error=L"Could not start Application Identity (AppIDSvc). Windows error: "+std::to_wstring(GetLastError());
    CloseServiceHandle(service);CloseServiceHandle(manager);
    return ok;
}
const wchar_t* scriptText=LR"PS(
param([string]$RulesFile,[string]$PolicyFile,[string]$ErrorFile,[switch]$RestoreOnly)
$ErrorActionPreference = 'Stop'
try {
    Import-Module AppLocker -ErrorAction Stop
    $current = Get-AppLockerPolicy -Local -Xml
    if ([string]::IsNullOrWhiteSpace([string]$current)) {
        [xml]$doc = '<AppLockerPolicy Version="1"></AppLockerPolicy>'
    } else {
        [xml]$doc = [string]$current
    }
    $root = $doc.DocumentElement
    $exe = $root.SelectSingleNode("RuleCollection[@Type='Exe']")
    if ($null -eq $exe -and -not $RestoreOnly) {
        $exe = $doc.CreateElement('RuleCollection')
        $exe.SetAttribute('Type','Exe')
        $exe.SetAttribute('EnforcementMode','Enabled')
        [void]$root.AppendChild($exe)
    }
    if ($null -ne $exe) {
        foreach ($node in @($exe.ChildNodes)) {
            if ($node.NodeType -eq [System.Xml.XmlNodeType]::Element -and
                $node.GetAttribute('Description').StartsWith('Himaya Security:',[System.StringComparison]::Ordinal)) {
                [void]$exe.RemoveChild($node)
            }
        }
        $external = @($exe.ChildNodes | Where-Object {
            $_.NodeType -eq [System.Xml.XmlNodeType]::Element -and
            -not $_.GetAttribute('Description').StartsWith('Himaya Security:',[System.StringComparison]::Ordinal)
        })
        $effectiveText = Get-AppLockerPolicy -Effective -Xml
        $effectiveExternal = @()
        $effectiveExe = $null
        if (-not [string]::IsNullOrWhiteSpace([string]$effectiveText)) {
            [xml]$effectiveDoc = [string]$effectiveText
            $effectiveExe = $effectiveDoc.DocumentElement.SelectSingleNode("RuleCollection[@Type='Exe']")
            if ($null -ne $effectiveExe) {
                $effectiveExternal = @($effectiveExe.ChildNodes | Where-Object {
                    $_.NodeType -eq [System.Xml.XmlNodeType]::Element -and
                    -not $_.GetAttribute('Description').StartsWith('Himaya Security:',[System.StringComparison]::Ordinal)
                })
            }
        }
        $hasExternalPolicy = ($external.Count -gt 0 -or $effectiveExternal.Count -gt 0)
        $requested = @()
        if (-not $RestoreOnly -and (Test-Path -LiteralPath $RulesFile)) {
            foreach ($line in [System.IO.File]::ReadAllLines($RulesFile,[System.Text.Encoding]::Unicode)) {
                if ([string]::IsNullOrWhiteSpace($line)) { continue }
                $split = $line.IndexOf('|')
                if ($split -le 0 -or $split -ge ($line.Length-1)) { throw 'Invalid Himaya AppLocker rule input.' }
                $requested += ,@($line.Substring(0,$split),$line.Substring($split+1))
            }
        }
        if ($requested.Count -gt 0 -and $hasExternalPolicy -and
            ($exe.GetAttribute('EnforcementMode') -eq 'AuditOnly' -or
             ($null -ne $effectiveExe -and $effectiveExe.GetAttribute('EnforcementMode') -eq 'AuditOnly'))) {
            throw 'An existing effective AppLocker EXE policy is set to Audit only. Himaya will not change another policy owner''s enforcement mode.'
        }
        if ($requested.Count -gt 0 -and -not $hasExternalPolicy) {
            $allow = $doc.CreateElement('FilePathRule')
            $allow.SetAttribute('Id',([guid]::NewGuid()).ToString('D'))
            $allow.SetAttribute('Name','Himaya baseline allow')
            $allow.SetAttribute('Description','Himaya Security: baseline allow')
            $allow.SetAttribute('UserOrGroupSid','S-1-1-0')
            $allow.SetAttribute('Action','Allow')
            $conditions = $doc.CreateElement('Conditions')
            $condition = $doc.CreateElement('FilePathCondition')
            $condition.SetAttribute('Path','*')
            [void]$conditions.AppendChild($condition)
            [void]$allow.AppendChild($conditions)
            [void]$exe.AppendChild($allow)
            $exe.SetAttribute('EnforcementMode','Enabled')
        }
        foreach ($item in $requested) {
            $rule = $doc.CreateElement('FilePathRule')
            $rule.SetAttribute('Id',([guid]::NewGuid()).ToString('D'))
            $rule.SetAttribute('Name','Himaya blocked application')
            $rule.SetAttribute('Description','Himaya Security: '+$item[1])
            $rule.SetAttribute('UserOrGroupSid',$item[0])
            $rule.SetAttribute('Action','Deny')
            $conditions = $doc.CreateElement('Conditions')
            $condition = $doc.CreateElement('FilePathCondition')
            $condition.SetAttribute('Path',$item[1])
            [void]$conditions.AppendChild($condition)
            [void]$rule.AppendChild($conditions)
            [void]$exe.AppendChild($rule)
        }
        if ($external.Count -eq 0 -and $requested.Count -eq 0) {
            [void]$root.RemoveChild($exe)
        }
    }
    $doc.Save($PolicyFile)
    Set-AppLockerPolicy -XmlPolicy $PolicyFile -ErrorAction Stop
    $converterPath = '\Microsoft\Windows\AppID\'
    $converterName = 'PolicyConverter'
    $converter = Get-ScheduledTask -TaskPath $converterPath -TaskName $converterName -ErrorAction Stop
    $wasDisabled = $converter.State -eq 'Disabled'
    if ($wasDisabled) { Enable-ScheduledTask -TaskPath $converterPath -TaskName $converterName -ErrorAction Stop | Out-Null }
    try {
        $beforeRun = (Get-ScheduledTaskInfo -TaskPath $converterPath -TaskName $converterName).LastRunTime
        Start-ScheduledTask -TaskPath $converterPath -TaskName $converterName -ErrorAction Stop
        $deadline = (Get-Date).AddSeconds(20)
        do {
            Start-Sleep -Milliseconds 250
            $converter = Get-ScheduledTask -TaskPath $converterPath -TaskName $converterName -ErrorAction Stop
            $converterInfo = Get-ScheduledTaskInfo -TaskPath $converterPath -TaskName $converterName
        } while (($converter.State -eq 'Running' -or $converterInfo.LastRunTime -le $beforeRun) -and (Get-Date) -lt $deadline)
        if ($converter.State -eq 'Running' -or $converterInfo.LastRunTime -le $beforeRun) { throw 'The AppLocker policy converter task did not complete.' }
        if ($converterInfo.LastTaskResult -ne 0) { throw ('The AppLocker policy converter task failed with result '+$converterInfo.LastTaskResult) }
    } finally {
        if ($wasDisabled) { Disable-ScheduledTask -TaskPath $converterPath -TaskName $converterName -ErrorAction Stop | Out-Null }
    }
    if (-not $RestoreOnly) {
        $effectivePolicy = Get-AppLockerPolicy -Effective -ErrorAction Stop
        foreach ($item in $requested) {
            if ($item[1].EndsWith('\*')) { continue }
            $decision = @(Test-AppLockerPolicy -PolicyObject $effectivePolicy -Path $item[1] -User $item[0] -Filter Denied -ErrorAction Stop)
            if ($decision.Count -eq 0) { throw ('The effective AppLocker policy does not deny the requested executable: '+$item[1]) }
        }
    }
    exit 0
} catch {
    ($_ | Out-String) | Set-Content -LiteralPath $ErrorFile -Encoding UTF8
    exit 1
}
)PS";
bool runPolicyScript(const std::vector<AppLockerRule>& rules,
                     const std::vector<std::wstring>& sids,
                     const std::wstring& directory,bool restoreOnly,
                     std::wstring& error) {
    if(!ensureAppIdRunning(error)) return false;
    const auto suffix=std::to_wstring(GetCurrentProcessId());
    const auto script=directory+L"\\Himaya-AppLocker-"+suffix+L".ps1";
    const auto input=directory+L"\\Himaya-AppLocker-"+suffix+L".txt";
    const auto policy=directory+L"\\Himaya-AppLocker-"+suffix+L".xml";
    const auto errorFile=directory+L"\\Himaya-AppLocker-"+suffix+L".err";
    std::wstring lines;
    if(!restoreOnly) {
        for(const auto& rule:rules) for(const auto& sid:sids) {
            if(sid.empty()||rule.path.empty()) continue;
            lines+=sid+L"|"+rule.path+L"\r\n";
        }
    }
    if(!writeUtf16(script,scriptText)||!writeUtf16(input,lines)) {
        error=L"Could not write the temporary AppLocker policy files.";
        return false;
    }
    DeleteFileW(errorFile.c_str());
    wchar_t system[MAX_PATH]{};
    const UINT n=GetSystemDirectoryW(system,MAX_PATH);
    if(!n||n>=MAX_PATH){error=L"Could not locate the Windows system directory.";return false;}
    const std::wstring powershell=std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring command=quote(powershell)+L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "+
        quote(script)+L" -RulesFile "+quote(input)+L" -PolicyFile "+quote(policy)+L" -ErrorFile "+quote(errorFile);
    if(restoreOnly) command+=L" -RestoreOnly";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(),command.end());mutableCommand.push_back(L'\0');
    const BOOL created=CreateProcessW(powershell.c_str(),mutableCommand.data(),nullptr,nullptr,FALSE,
        CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,nullptr,directory.c_str(),&startup,&process);
    if(!created) {
        error=L"Could not start the Windows AppLocker policy manager. Windows error: "+std::to_wstring(GetLastError());
        return false;
    }
    const DWORD wait=WaitForSingleObject(process.hProcess,45000);
    DWORD exitCode=ERROR_GEN_FAILURE;
    if(wait==WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess,&exitCode);
    else {TerminateProcess(process.hProcess,ERROR_TIMEOUT);error=L"AppLocker policy update timed out.";}
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    if(wait!=WAIT_OBJECT_0) return false;
    if(exitCode!=0) {
        error=readUtf8(errorFile);
        if(error.empty()) error=L"Windows rejected the AppLocker policy update. Exit code: "+std::to_wstring(exitCode);
        return false;
    }
    if(restoreOnly && !restoreAppIdConfiguration(error)) return false;
    DeleteFileW(script.c_str());DeleteFileW(input.c_str());DeleteFileW(policy.c_str());DeleteFileW(errorFile.c_str());
    return true;
}
}
bool AppLockerPolicy::synchronize(const std::vector<AppLockerRule>& rules,
                                  const std::vector<std::wstring>& userSids,
                                  const std::wstring& workingDirectory,
                                  std::wstring& error) {
    if(userSids.empty()) {
        error=L"No interactive user account is available yet; existing AppLocker rules were left unchanged.";
        return true;
    }
    return runPolicyScript(rules,userSids,workingDirectory,false,error);
}
bool AppLockerPolicy::restore(const std::wstring& workingDirectory,std::wstring& error) {
    return runPolicyScript({}, {},workingDirectory,true,error);
}
}
