#include "DnsProtection.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

namespace himaya {
namespace {
constexpr wchar_t kScript[] = LR"PS(
param([ValidateSet('Enable','Sync','Restore')][string]$Mode)
$ErrorActionPreference='Stop'
$root=Join-Path $env:ProgramData 'Himaya'
New-Item -ItemType Directory -Path $root -Force | Out-Null
$backupFile=Join-Path $root 'dns-protection-backup.json'
$dns4=@('185.228.168.168','185.228.169.168')
$dns6=@('2a0d:2a00:1::','2a0d:2a00:2::')
$firewallNames=@('Himaya Adult DNS - Block DNS over TLS','Himaya Adult DNS - Block alternate DNS UDP','Himaya Adult DNS - Block alternate DNS TCP')
function Backup-Settings {
  $b=$null
  if(Test-Path $backupFile){$b=Get-Content $backupFile -Raw|ConvertFrom-Json}
  if($null -eq $b){$b=[pscustomobject]@{Dns=@();PolicyBackups=@();PolicyBackupComplete=$false}}
  $dns=@($b.Dns);$known=@{}
  foreach($e in $dns){$known["$($e.InterfaceIndex)|$($e.AddressFamily)"]=$true}
  foreach($a in (Get-DnsClientServerAddress)){
    if($a.InterfaceAlias -match 'Loopback'){continue}
    $key="$($a.InterfaceIndex)|$($a.AddressFamily)"
    if($known.ContainsKey($key)){continue}
    $ip=Get-NetIPInterface -InterfaceIndex $a.InterfaceIndex -AddressFamily $a.AddressFamily -ErrorAction SilentlyContinue
    if($null -eq $ip){continue}
    $dns += [pscustomobject]@{InterfaceIndex=[int]$a.InterfaceIndex;AddressFamily=[string]$a.AddressFamily;Dhcp=([string]$ip.Dhcp -eq 'Enabled');Servers=@($a.ServerAddresses)}
    $known[$key]=$true
  }
  $b.Dns=@($dns)
  if(-not $b.PolicyBackupComplete){
    $items=@();$keys=@('HKLM\SOFTWARE\Policies\Microsoft\Edge','HKLM\SOFTWARE\Policies\Google\Chrome','HKLM\SOFTWARE\Policies\Mozilla\Firefox')
    for($i=0;$i -lt $keys.Count;$i++){
      $file="policy-$i.reg";$full=Join-Path $root $file
      & reg.exe query $keys[$i] *> $null
      if($LASTEXITCODE -eq 0){& reg.exe export $keys[$i] $full /y *> $null;if($LASTEXITCODE -eq 0){$items += [pscustomobject]@{File=$file}}}
    }
    $b.PolicyBackups=@($items);$b.PolicyBackupComplete=$true
  }
  $tmp="$backupFile.tmp";$b|ConvertTo-Json -Depth 10|Set-Content $tmp -Encoding UTF8;Move-Item $tmp $backupFile -Force
}
function Apply-Protection {
  Backup-Settings
  $configured=0
  foreach($a in (Get-DnsClientServerAddress)){
    if($a.InterfaceAlias -match 'Loopback'){continue}
    try{
      if([string]$a.AddressFamily -eq 'IPv4'){
        if((@($a.ServerAddresses) -join ',') -ne ($dns4 -join ',')){Set-DnsClientServerAddress -InterfaceIndex $a.InterfaceIndex -ServerAddresses $dns4 -ErrorAction Stop}
        $configured++
      } elseif([string]$a.AddressFamily -eq 'IPv6'){
        if((@($a.ServerAddresses) -join ',') -ne ($dns6 -join ',')){Set-DnsClientServerAddress -InterfaceIndex $a.InterfaceIndex -ServerAddresses $dns6 -ErrorAction Stop}
      }
    }catch{}
  }
  if($configured -eq 0){throw 'No IPv4 network adapter accepted the CleanBrowsing DNS servers.'}
  $edge='HKLM:\SOFTWARE\Policies\Microsoft\Edge';$chrome='HKLM:\SOFTWARE\Policies\Google\Chrome'
  New-Item $edge -Force|Out-Null;New-Item $chrome -Force|Out-Null
  New-ItemProperty $edge -Name DnsOverHttpsMode -PropertyType String -Value off -Force|Out-Null
  New-ItemProperty $chrome -Name DnsOverHttpsMode -PropertyType String -Value off -Force|Out-Null
  $ff='HKLM:\SOFTWARE\Policies\Mozilla\Firefox\DNSOverHTTPS';New-Item $ff -Force|Out-Null
  New-ItemProperty $ff -Name Enabled -PropertyType DWord -Value 0 -Force|Out-Null
  New-ItemProperty $ff -Name Locked -PropertyType DWord -Value 1 -Force|Out-Null
  if(-not (Get-NetFirewallRule -DisplayName $firewallNames[0] -ErrorAction SilentlyContinue)){
    New-NetFirewallRule -DisplayName $firewallNames[0] -Direction Outbound -Action Block -Protocol TCP -RemotePort 853 -Profile Any|Out-Null
  }
  $known=@('1.1.1.1','1.0.0.1','8.8.8.8','8.8.4.4','9.9.9.9','149.112.112.112','208.67.222.222','208.67.220.220','94.140.14.14','94.140.15.15','2606:4700:4700::1111','2001:4860:4860::8888','2620:fe::fe')
  if(-not (Get-NetFirewallRule -DisplayName $firewallNames[1] -ErrorAction SilentlyContinue)){New-NetFirewallRule -DisplayName $firewallNames[1] -Direction Outbound -Action Block -Protocol UDP -RemotePort 53 -RemoteAddress $known -Profile Any|Out-Null}
  if(-not (Get-NetFirewallRule -DisplayName $firewallNames[2] -ErrorAction SilentlyContinue)){New-NetFirewallRule -DisplayName $firewallNames[2] -Direction Outbound -Action Block -Protocol TCP -RemotePort 53 -RemoteAddress $known -Profile Any|Out-Null}
}
function Restore-Protection {
  if(Test-Path $backupFile){
    $b=Get-Content $backupFile -Raw|ConvertFrom-Json
    foreach($e in @($b.Dns)){
      try{
        if($e.Dhcp){Set-DnsClientServerAddress -InterfaceIndex ([int]$e.InterfaceIndex) -ResetServerAddresses -ErrorAction Stop}
        elseif(@($e.Servers).Count -gt 0){Set-DnsClientServerAddress -InterfaceIndex ([int]$e.InterfaceIndex) -ServerAddresses @($e.Servers) -ErrorAction Stop}
      }catch{}
    }
    Remove-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Edge' -Name DnsOverHttpsMode -ErrorAction SilentlyContinue
    Remove-ItemProperty 'HKLM:\SOFTWARE\Policies\Google\Chrome' -Name DnsOverHttpsMode -ErrorAction SilentlyContinue
    Remove-ItemProperty 'HKLM:\SOFTWARE\Policies\Mozilla\Firefox\DNSOverHTTPS' -Name Enabled,Locked -ErrorAction SilentlyContinue
    foreach($p in @($b.PolicyBackups)){$full=Join-Path $root $p.File;if(Test-Path $full){& reg.exe import $full *> $null}}
    Remove-Item $backupFile -Force -ErrorAction SilentlyContinue
    Get-ChildItem $root -Filter 'policy-*.reg' -ErrorAction SilentlyContinue|Remove-Item -Force -ErrorAction SilentlyContinue
  }
  foreach($n in $firewallNames){Get-NetFirewallRule -DisplayName $n -ErrorAction SilentlyContinue|Remove-NetFirewallRule -ErrorAction SilentlyContinue}
}
if($Mode -eq 'Restore'){Restore-Protection;exit 0}
Apply-Protection
exit 0
)PS";
std::wstring rootPath(){
  PWSTR raw=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData,KF_FLAG_CREATE,nullptr,&raw))||!raw)return {};
  std::filesystem::path p(raw);CoTaskMemFree(raw);p/=L"Himaya";std::error_code ec;std::filesystem::create_directories(p,ec);return ec?L"":p.wstring();
}
bool writeScript(const std::wstring& path){
  const int n=WideCharToMultiByte(CP_UTF8,0,kScript,-1,nullptr,0,nullptr,nullptr);if(n<=1)return false;
  std::string data(static_cast<size_t>(n),'\0');if(!WideCharToMultiByte(CP_UTF8,0,kScript,-1,data.data(),n,nullptr,nullptr))return false;data.resize(n-1);
  const auto tmp=path+L".tmp";HANDLE h=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return false;
  const BYTE bom[]={0xEF,0xBB,0xBF};DWORD written=0;bool ok=WriteFile(h,bom,3,&written,nullptr)&&written==3;size_t pos=0;
  while(ok&&pos<data.size()){DWORD chunk=static_cast<DWORD>(std::min<size_t>(data.size()-pos,1024*1024));written=0;ok=WriteFile(h,data.data()+pos,chunk,&written,nullptr)&&written>0;pos+=written;}
  if(ok)ok=FlushFileBuffers(h)!=FALSE;CloseHandle(h);if(!ok){DeleteFileW(tmp.c_str());return false;}
  return MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
}
DnsProtectionResult run(const wchar_t* mode){
  const auto root=rootPath();if(root.empty())return {false,L"Could not access Himaya ProgramData."};
  const auto script=(std::filesystem::path(root)/L"DnsProtection.ps1").wstring();if(!writeScript(script))return {false,L"Could not write DNS protection script."};
  wchar_t sys[MAX_PATH]{};const UINT n=GetSystemDirectoryW(sys,MAX_PATH);if(!n||n>=MAX_PATH)return {false,L"Could not locate Windows PowerShell."};
  std::wstring cmd=L"\\""+std::wstring(sys)+L"\\WindowsPowerShell\\v1.0\\powershell.exe\\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \\""+script+L"\\" -Mode "+mode;
  STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};std::vector<wchar_t> buf(cmd.begin(),cmd.end());buf.push_back(L'\0');
  if(!CreateProcessW(nullptr,buf.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return {false,L"Could not start PowerShell."};
  const DWORD wait=WaitForSingleObject(pi.hProcess,90000);DWORD code=STILL_ACTIVE;const bool ok=wait==WAIT_OBJECT_0&&GetExitCodeProcess(pi.hProcess,&code);
  if(wait==WAIT_TIMEOUT)TerminateProcess(pi.hProcess,ERROR_TIMEOUT);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
  if(!ok||code!=0)return {false,L"DNS protection failed (PowerShell exit code "+std::to_wstring(ok?code:ERROR_GEN_FAILURE)+L"). Check network and firewall services."};
  return {true,std::wstring(mode)==L"Restore"?L"Original DNS and browser policy settings restored; Himaya DNS firewall rules removed.":L"CleanBrowsing Family DNS, browser DNS policies and DNS firewall protections applied."};
}
}
DnsProtectionResult DnsProtection::enable(){return run(L"Enable");}
DnsProtectionResult DnsProtection::synchronize(){return run(L"Sync");}
DnsProtectionResult DnsProtection::restore(){return run(L"Restore");}
}
