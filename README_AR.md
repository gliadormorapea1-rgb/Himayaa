# Himaya Security 0.4.0 — Development

## What is implemented in this branch

- SQLite-backed lists and settings under `%ProgramData%\Himaya`.
- Adult-domain blocking via the Windows hosts file, with an original backup and managed-block restoration.
- A Windows background service (`HimayaGuard`) installed as LocalSystem.
- The service synchronizes selected executable and folder execution-deny rules into Windows Software Restriction Policies (SRP). Executable entries must be full existing file paths. Folder entries deny execution below the selected folder; they do not deny browsing or reading files.

## Important limitations

- This build does **not** implement folder read/write ACL denial.
- SRP is an OS policy, not tamper-proof against a local administrator. A local administrator can stop the service, edit policy, or uninstall the app.
- Website blocking uses the hosts file and a finite community domain list. It is not a network filter and can be bypassed by some VPN, proxy, encrypted DNS, or apps with their own resolver.
- Trusted-person approval, a 72-hour unlock delay, secure password administration, scheduled feed refresh, and complete recovery/reporting UI are not yet implemented.
- Service installation and build success do not prove end-to-end enforcement on every Windows 11 configuration. Validate on a Windows 11 Pro test machine before relying on it.

## Build

Pushes to `himaya-0.3-development` run the Windows x64 GitHub Actions workflow. Download the `Himaya-Windows-x64-v0.4.0` artifact after a successful run.
