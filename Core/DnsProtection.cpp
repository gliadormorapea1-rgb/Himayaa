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
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Join-Path $env:ProgramData 'Himaya'
$logDir = Join-Path $root 'Logs'
New-Item -ItemType Directory -Path $root -Force | Out-Null
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$backupFile = Join-Path $root 'dns-protection-backup.json'
$logFile = Join-Path $logDir 'DnsProtection.log'
$dns4 = @('185.228.168.168','185.228.169.168')
$dns6 = @('2a0d:2a00:1::','2a0d:2a00:2::')
$firewallNames = @(
  'Himaya Adult DNS - Block DNS over TLS',
  'Himaya Adult DNS - Block alternate DNS UDP',
  'Himaya Adult DNS - Block alternate DNS TCP'
)
$policyKeys = @(
  [pscustomobject]@{ Key='HKLM\SOFTWARE\Policies\Microsoft\Edge'; File='policy-0.reg' },
  [pscustomobject]@{ Key='HKLM\SOFTWARE\Policies\Google\Chrome'; File='policy-1.reg' },
  [pscustomobject]@{ Key='HKLM\SOFTWARE\Policies\Mozilla\Firefox'; File='policy-2.reg' },
  [pscustomobject]@{ Key='HKLM\SOFTWARE\Policies\BraveSoftware\Brave'; File='policy-3.reg' }
)

function Write-Log {
  param([string]$Message)
  $line = '{0} [{1}] {2}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $Mode, $Message
  try { Add-Content -LiteralPath $logFile -Value $line -Encoding UTF8 -ErrorAction Stop } catch {}
}
function Get-AddressFamilyName {
  param($Value)
  switch -Regex ([string]$Value) {
    '^(2|IPv4|InterNetwork)$' { return 'IPv4' }
    '^(23|IPv6|InterNetworkV6)$' { return 'IPv6' }
    default { return $null }
  }
}
function Get-AdapterForIndex {
  param([int]$Index)
  return Get-NetAdapter -IncludeHidden -ErrorAction SilentlyContinue |
    Where-Object { [int]$_.InterfaceIndex -eq $Index } |
    Select-Object -First 1
}
function Backup-Settings {
  $b = $null
  if (Test-Path -LiteralPath $backupFile) {
    $b = Get-Content -LiteralPath $backupFile -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ($null -eq $b -or $null -eq $b.Dns) {
      throw 'The existing DNS backup is invalid. It was preserved; refusing to overwrite it.'
    }
  }
  if ($null -eq $b) {
    $b = [pscustomobject]@{
      BackupVersion = 2
      Dns = @()
      PolicyKeys = @()
      PolicyBackups = @()
      PolicyBackupComplete = $false
    }
  }

  $dns = @($b.Dns)
  $known = @{}
  foreach ($entry in $dns) {
    $savedFamily = Get-AddressFamilyName $entry.AddressFamily
    if ($null -ne $savedFamily) {
      $entry.AddressFamily = $savedFamily
      $known["$($entry.InterfaceIndex)|$savedFamily"] = $true
    }
  }
  foreach ($entry in (Get-DnsClientServerAddress -ErrorAction Stop)) {
    if ($entry.InterfaceAlias -match 'Loopback') { continue }
    $family = Get-AddressFamilyName $entry.AddressFamily
    if ($null -eq $family) {
      Write-Log "Backup skipped unsupported address family '$($entry.AddressFamily)' on $($entry.InterfaceAlias)."
      continue
    }
    $key = "$($entry.InterfaceIndex)|$family"
    if ($known.ContainsKey($key)) { continue }

    $ip = Get-NetIPInterface -InterfaceIndex $entry.InterfaceIndex -AddressFamily $family -ErrorAction SilentlyContinue |
      Select-Object -First 1
    if ($null -eq $ip) { continue }
    $nic = Get-AdapterForIndex ([int]$entry.InterfaceIndex)
    $manualDns = $false
    $interfaceGuid = ''
    if ($null -ne $nic -and $nic.InterfaceGuid) {
      $interfaceGuid = [string]$nic.InterfaceGuid
      $guid = ([guid]$nic.InterfaceGuid).ToString('B')
      if ($family -eq 'IPv4') {
        $registryPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters\Interfaces\' + $guid
      } else {
        $registryPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\Tcpip6\Parameters\Interfaces\' + $guid
      }
      $value = Get-ItemProperty -Path $registryPath -Name NameServer -ErrorAction SilentlyContinue
      $manualDns = ($null -ne $value -and -not [string]::IsNullOrWhiteSpace([string]$value.NameServer))
    }
    $dns += [pscustomobject]@{
      InterfaceIndex = [int]$entry.InterfaceIndex
      InterfaceGuid = $interfaceGuid
      InterfaceAlias = [string]$entry.InterfaceAlias
      AddressFamily = $family
      Dhcp = (([string]$ip.Dhcp -eq 'Enabled') -and -not $manualDns)
      Servers = @($entry.ServerAddresses)
    }
    $known[$key] = $true
  }
  $b.Dns = @($dns)

  if (-not $b.PolicyBackupComplete) {
    $items = @()
    foreach ($policy in $policyKeys) {
      $fullPath = Join-Path $root $policy.File
      & reg.exe query $policy.Key *> $null
      $queryCode = $LASTEXITCODE
      if ($queryCode -eq 0) {
        & reg.exe export $policy.Key $fullPath /y *> $null
        $exportCode = $LASTEXITCODE
        if ($exportCode -ne 0 -or -not (Test-Path -LiteralPath $fullPath)) {
          throw "Could not back up browser policy key '$($policy.Key)' (reg.exe export exit code $exportCode)."
        }
        $items += [pscustomobject]@{ Key=$policy.Key; File=$policy.File; Exists=$true }
      } elseif ($queryCode -eq 1) {
        $items += [pscustomobject]@{ Key=$policy.Key; File=$policy.File; Exists=$false }
      } else {
        throw "Could not inspect browser policy key '$($policy.Key)' (reg.exe query exit code $queryCode)."
      }
    }
    $b.PolicyKeys = @($items)
    $b.PolicyBackups = @($items | Where-Object { $_.Exists })
    $b.PolicyBackupComplete = $true
  }

  $b.BackupVersion = 2
  $temporaryFile = "$backupFile.tmp"
  $json = $b | ConvertTo-Json -Depth 12
  Set-Content -LiteralPath $temporaryFile -Value $json -Encoding UTF8 -ErrorAction Stop
  Move-Item -LiteralPath $temporaryFile -Destination $backupFile -Force -ErrorAction Stop
  Write-Log "Settings backup validated and saved. DNS entries: $(@($b.Dns).Count); browser policy backup complete: $($b.PolicyBackupComplete)."
}

function Apply-Protection {
  Backup-Settings
  $configuredIPv4 = 0
  $configuredIPv6 = 0
  $activeIPv4Failures = @()
  foreach ($entry in (Get-DnsClientServerAddress -ErrorAction Stop)) {
    if ($entry.InterfaceAlias -match 'Loopback') { continue }
    $family = Get-AddressFamilyName $entry.AddressFamily
    if ($null -eq $family) { continue }

    $nic = Get-AdapterForIndex ([int]$entry.InterfaceIndex)
    if ($null -eq $nic -or [string]$nic.Status -ne 'Up') {
      Write-Log "Skipping inactive interface $($entry.InterfaceAlias) (index $($entry.InterfaceIndex), $family)."
      continue
    }

    $componentId = if ($family -eq 'IPv4') { 'ms_tcpip' } else { 'ms_tcpip6' }
    $binding = Get-NetAdapterBinding -Name $nic.Name -ComponentID $componentId -ErrorAction SilentlyContinue |
      Select-Object -First 1
    if ($null -eq $binding -or -not $binding.Enabled) {
      Write-Log "Skipping $family on $($nic.Name): protocol binding '$componentId' is not enabled."
      continue
    }

    $servers = if ($family -eq 'IPv4') { $dns4 } else { $dns6 }
    try {
      if ((@($entry.ServerAddresses) -join ',') -ne ($servers -join ',')) {
        Set-DnsClientServerAddress -InterfaceIndex ([int]$entry.InterfaceIndex) -ServerAddresses $servers -ErrorAction Stop
      }
      $verified = Get-DnsClientServerAddress -InterfaceIndex ([int]$entry.InterfaceIndex) -AddressFamily $family -ErrorAction Stop
      if ((@($verified.ServerAddresses) -join ',') -ne ($servers -join ',')) {
        throw "DNS verification did not match the requested $family servers."
      }
      if ($family -eq 'IPv4') { $configuredIPv4++ } else { $configuredIPv6++ }
      Write-Log "Configured and verified $family DNS on $($nic.Name) (index $($entry.InterfaceIndex))."
    } catch {
      $detail = "Interface $($nic.Name) (index $($entry.InterfaceIndex), $family): $($_.Exception.Message)"
      Write-Log "DNS configuration error: $detail"
      if ($family -eq 'IPv4') { $activeIPv4Failures += $detail } else { throw $detail }
    }
  }

  if ($configuredIPv4 -eq 0) {
    throw "No connected adapter accepted CleanBrowsing IPv4 DNS. Check active adapters and permissions. IPv4 errors: $($activeIPv4Failures -join '; ')"
  }
  if ($activeIPv4Failures.Count -gt 0) {
    throw "DNS protection is incomplete because one or more active IPv4 adapters failed: $($activeIPv4Failures -join '; ')"
  }

  $edge = 'HKLM:\SOFTWARE\Policies\Microsoft\Edge'
  $chrome = 'HKLM:\SOFTWARE\Policies\Google\Chrome'
  $brave = 'HKLM:\SOFTWARE\Policies\BraveSoftware\Brave'
  $firefox = 'HKLM:\SOFTWARE\Policies\Mozilla\Firefox\DNSOverHTTPS'
  New-Item $edge -Force -ErrorAction Stop | Out-Null
  New-Item $chrome -Force -ErrorAction Stop | Out-Null
  New-Item $brave -Force -ErrorAction Stop | Out-Null
  New-Item $firefox -Force -ErrorAction Stop | Out-Null
  New-ItemProperty $edge -Name DnsOverHttpsMode -PropertyType String -Value off -Force -ErrorAction Stop | Out-Null
  New-ItemProperty $chrome -Name DnsOverHttpsMode -PropertyType String -Value off -Force -ErrorAction Stop | Out-Null
  New-ItemProperty $brave -Name DnsOverHttpsMode -PropertyType String -Value off -Force -ErrorAction Stop | Out-Null
  New-ItemProperty $firefox -Name Enabled -PropertyType DWord -Value 0 -Force -ErrorAction Stop | Out-Null
  New-ItemProperty $firefox -Name Locked -PropertyType DWord -Value 1 -Force -ErrorAction Stop | Out-Null

  if (-not (Get-NetFirewallRule -DisplayName $firewallNames[0] -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName $firewallNames[0] -Direction Outbound -Action Block -Protocol TCP -RemotePort 853 -Profile Any -ErrorAction Stop | Out-Null
  }
  $known = @('1.1.1.1','1.0.0.1','8.8.8.8','8.8.4.4','9.9.9.9','149.112.112.112','208.67.222.222','208.67.220.220','94.140.14.14','94.140.15.15','2606:4700:4700::1111','2001:4860:4860::8888','2620:fe::fe')
  if (-not (Get-NetFirewallRule -DisplayName $firewallNames[1] -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName $firewallNames[1] -Direction Outbound -Action Block -Protocol UDP -RemotePort 53 -RemoteAddress $known -Profile Any -ErrorAction Stop | Out-Null
  }
  if (-not (Get-NetFirewallRule -DisplayName $firewallNames[2] -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName $firewallNames[2] -Direction Outbound -Action Block -Protocol TCP -RemotePort 53 -RemoteAddress $known -Profile Any -ErrorAction Stop | Out-Null
  }
  Write-Log "DNS protection applied. Verified IPv4 adapters: $configuredIPv4; verified IPv6 adapters: $configuredIPv6."
}

function Restore-Protection {
  if (-not (Test-Path -LiteralPath $backupFile)) {
    Write-Log 'No DNS backup exists; removing only Himaya-owned firewall rules.'
  } else {
    $b = Get-Content -LiteralPath $backupFile -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ($null -eq $b -or $null -eq $b.Dns) {
      throw 'The DNS backup is invalid. It was preserved; restore was not attempted.'
    }
    $restoreErrors = @()
    $resolvedDns = @()
    foreach ($entry in @($b.Dns)) {
      try {
        $targetIndex = [int]$entry.InterfaceIndex
        if ($entry.InterfaceGuid) {
          $matchingAdapter = Get-NetAdapter -IncludeHidden -ErrorAction SilentlyContinue |
            Where-Object { [string]$_.InterfaceGuid -eq [string]$entry.InterfaceGuid } |
            Select-Object -First 1
          if ($null -ne $matchingAdapter) { $targetIndex = [int]$matchingAdapter.InterfaceIndex }
        }
        $existingAdapter = Get-AdapterForIndex $targetIndex
        if ($null -eq $existingAdapter) {
          Write-Log "Restore skipped removed interface index $targetIndex ($($entry.InterfaceAlias))."
          continue
        }
        $family = Get-AddressFamilyName $entry.AddressFamily
        if ($null -eq $family) { throw "Unknown saved address family '$($entry.AddressFamily)'." }
        $resolvedDns += [pscustomobject]@{
          Entry = $entry
          Index = $targetIndex
          Adapter = $existingAdapter
          Family = $family
          NeedsReset = ([bool]$entry.Dhcp -or @($entry.Servers).Count -eq 0)
        }
      } catch {
        $restoreErrors += "DNS interface $($entry.InterfaceAlias) ($($entry.AddressFamily)): $($_.Exception.Message)"
      }
    }
    # Reset DHCP-managed families first: ResetServerAddresses may reset both
    # protocol families on an interface. Static families are reapplied second.
    $resetIndexes = @($resolvedDns | Where-Object { $_.NeedsReset } | Select-Object -ExpandProperty Index -Unique)
    foreach ($index in $resetIndexes) {
      try {
        Set-DnsClientServerAddress -InterfaceIndex ([int]$index) -ResetServerAddresses -ErrorAction Stop
        Write-Log "Reset DNS to automatic assignment on interface index $index."
      } catch {
        $restoreErrors += "DNS reset on interface index $index: $($_.Exception.Message)"
      }
    }
    foreach ($item in @($resolvedDns | Where-Object { -not $_.NeedsReset })) {
      try {
        Set-DnsClientServerAddress -InterfaceIndex ([int]$item.Index) -ServerAddresses @($item.Entry.Servers) -ErrorAction Stop
        Write-Log "Restored static $($item.Family) DNS on $($item.Adapter.Name) (index $($item.Index))."
      } catch {
        $restoreErrors += "DNS interface $($item.Adapter.Name) ($($item.Family)): $($_.Exception.Message)"
      }
    }

    Remove-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Edge' -Name DnsOverHttpsMode -ErrorAction SilentlyContinue
    Remove-ItemProperty 'HKLM:\SOFTWARE\Policies\Google\Chrome' -Name DnsOverHttpsMode -ErrorAction SilentlyContinue
    Remove-ItemProperty 'HKLM:\SOFTWARE\Policies\BraveSoftware\Brave' -Name DnsOverHttpsMode -ErrorAction SilentlyContinue
    Remove-ItemProperty 'HKLM:\SOFTWARE\Policies\Mozilla\Firefox\DNSOverHTTPS' -Name Enabled,Locked -ErrorAction SilentlyContinue

    if ($null -ne $b.PolicyKeys -and @($b.PolicyKeys).Count -gt 0) {
      foreach ($policy in @($b.PolicyKeys)) {
        try {
          if ($policy.Exists) {
            $fullPath = Join-Path $root $policy.File
            if (-not (Test-Path -LiteralPath $fullPath)) { throw "Missing browser policy backup file '$fullPath'." }
            & reg.exe import $fullPath *> $null
            $importCode = $LASTEXITCODE
            if ($importCode -ne 0) { throw "reg.exe import failed with exit code $importCode." }
          } else {
            & reg.exe delete $policy.Key /f *> $null
            $deleteCode = $LASTEXITCODE
            if ($deleteCode -gt 1) { throw "reg.exe delete failed with exit code $deleteCode." }
          }
        } catch { $restoreErrors += "Browser policy $($policy.Key): $($_.Exception.Message)" }
      }
    } else {
      foreach ($policy in @($b.PolicyBackups)) {
        $fullPath = Join-Path $root $policy.File
        if (Test-Path -LiteralPath $fullPath) {
          & reg.exe import $fullPath *> $null
          $importCode = $LASTEXITCODE
          if ($importCode -ne 0) { $restoreErrors += "Browser policy import '$($policy.File)' failed with exit code $importCode." }
        }
      }
    }

    foreach ($name in $firewallNames) {
      try {
        Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue |
          Remove-NetFirewallRule -ErrorAction Stop
      } catch { $restoreErrors += "Firewall rule '$name': $($_.Exception.Message)" }
    }

    if ($restoreErrors.Count -gt 0) {
      throw "Restore was incomplete; the original backup was retained. $($restoreErrors -join ' | ')"
    }
    Remove-Item -LiteralPath $backupFile -Force -ErrorAction Stop
    Get-ChildItem $root -Filter 'policy-*.reg' -ErrorAction SilentlyContinue |
      Remove-Item -Force -ErrorAction SilentlyContinue
  }
  foreach ($name in $firewallNames) {
    Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue |
      Remove-NetFirewallRule -ErrorAction SilentlyContinue
  }
  Write-Log 'Restore completed.'
}

Write-Log "Starting DNS protection mode '$Mode'."
try {
  if ($Mode -eq 'Restore') {
    Restore-Protection
  } else {
    Apply-Protection
  }
  Write-Log "Mode '$Mode' completed successfully."
  exit 0
} catch {
  $failure = $_
  Write-Log "FAILED: $($failure.Exception.Message)"
  try {
    $details = $failure | Format-List * -Force | Out-String
    Add-Content -LiteralPath $logFile -Value $details -Encoding UTF8 -ErrorAction Stop
  } catch {}
  if ($Mode -ne 'Restore' -and (Test-Path -LiteralPath $backupFile)) {
    try {
      Write-Log 'Attempting automatic rollback after failed enable/sync.'
      Restore-Protection
      Write-Log 'Automatic rollback completed.'
    } catch {
      Write-Log "AUTOMATIC ROLLBACK FAILED; backup retained: $($_.Exception.Message)"
      try { Add-Content -LiteralPath $logFile -Value ($_ | Format-List * -Force | Out-String) -Encoding UTF8 } catch {}
    }
  }
  exit 1
}
)PS";
std::wstring rootPath(){
  PWSTR raw=nullptr;
  if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData,KF_FLAG_CREATE,nullptr,&raw))||!raw)return {};
  std::filesystem::path p(raw);CoTaskMemFree(raw);p/=L"Himaya";
  std::error_code ec;std::filesystem::create_directories(p,ec);return ec?L"":p.wstring();
}
bool writeScript(const std::wstring& path){
  const int n=WideCharToMultiByte(CP_UTF8,0,kScript,-1,nullptr,0,nullptr,nullptr);if(n<=1)return false;
  std::string data(static_cast<size_t>(n),'\0');
  if(!WideCharToMultiByte(CP_UTF8,0,kScript,-1,data.data(),n,nullptr,nullptr))return false;
  data.resize(n-1);
  const auto tmp=path+L".tmp";
  HANDLE h=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(h==INVALID_HANDLE_VALUE)return false;
  const BYTE bom[]={0xEF,0xBB,0xBF};DWORD written=0;
  bool ok=WriteFile(h,bom,3,&written,nullptr)&&written==3;size_t pos=0;
  while(ok&&pos<data.size()){
    DWORD chunk=static_cast<DWORD>(std::min<size_t>(data.size()-pos,1024*1024));written=0;
    ok=WriteFile(h,data.data()+pos,chunk,&written,nullptr)&&written>0;pos+=written;
  }
  if(ok)ok=FlushFileBuffers(h)!=FALSE;
  CloseHandle(h);
  if(!ok){DeleteFileW(tmp.c_str());return false;}
  return MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
}
DnsProtectionResult run(const wchar_t* mode){
  const auto root=rootPath();if(root.empty())return {false,L"Could not access Himaya ProgramData."};
  const auto script=(std::filesystem::path(root)/L"DnsProtection.ps1").wstring();
  if(!writeScript(script))return {false,L"Could not write DNS protection script."};
  wchar_t sys[MAX_PATH]{};const UINT n=GetSystemDirectoryW(sys,MAX_PATH);
  if(!n||n>=MAX_PATH)return {false,L"Could not locate Windows PowerShell."};
  std::wstring cmd=L"\""+std::wstring(sys)+L"\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""+script+L"\" -Mode "+mode;
  STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buf(cmd.begin(),cmd.end());buf.push_back(L'\0');
  if(!CreateProcessW(nullptr,buf.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))
    return {false,L"Could not start PowerShell."};
  const DWORD wait=WaitForSingleObject(pi.hProcess,90000);DWORD code=STILL_ACTIVE;
  const bool ok=wait==WAIT_OBJECT_0&&GetExitCodeProcess(pi.hProcess,&code);
  if(wait==WAIT_TIMEOUT)TerminateProcess(pi.hProcess,ERROR_TIMEOUT);
  CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
  if(!ok||code!=0){
    const auto log=(std::filesystem::path(root)/L"Logs"/L"DnsProtection.log").wstring();
    return {false,L"DNS protection failed (PowerShell exit code "+std::to_wstring(ok?code:ERROR_GEN_FAILURE)+L"). Diagnostic log: "+log};
  }
  return {true,std::wstring(mode)==L"Restore"
    ?L"Original DNS and browser policy settings restored; Himaya DNS firewall rules removed."
    :L"CleanBrowsing Family DNS, browser DNS policies and DNS firewall protections applied."};
}
}
DnsProtectionResult DnsProtection::enable(){return run(L"Enable");}
DnsProtectionResult DnsProtection::synchronize(){return run(L"Sync");}
DnsProtectionResult DnsProtection::restore(){return run(L"Restore");}
}
