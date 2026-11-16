# Himaya Security

Himaya Security is a Windows desktop application for managing locally configured website, application, and folder restrictions.

> **Development status:** pre-release software. Features and compatibility are still being validated. Do not rely on Himaya as your only security, parental-control, or content-filtering measure.

## Current capabilities

- Arabic Windows desktop interface.
- Local SQLite configuration and event history.
- User-managed website/domain blocking through the Windows hosts file.
- Optional domain-list import from a community-maintained external source.
- Application execution restrictions using Windows policy facilities.
- Folder access restrictions using Windows ACLs, with restoration data maintained by the application.
- A background Windows service named `HimayaGuard`.
- CSV export of recent application events.

## Important limitations

- Website blocking through the hosts file can be bypassed by VPNs, proxies, encrypted DNS, browsers with their own DNS settings, or other network configurations.
- Application and folder enforcement depends on Windows policy and permissions. A local administrator can change system settings, stop services, or remove the application.
- Some profile choices currently store a selected preference but do not yet implement distinct enforcement schedules or policy sets. Do not interpret a profile label as proof of a separate protection level.
- The community domain list is maintained by a third party and may change or become unavailable.
- Compatibility and enforcement behavior can vary by Windows edition and device configuration.
- Himaya is not an antivirus and does not provide absolute protection.

## Requirements

- Windows 11 x64 (the current intended target).
- Visual Studio 2022 with the Desktop development with C++ workload.
- CMake 3.24 or newer.
- Inno Setup 6 to build the installer.
- Administrator privileges for installation and system-level enforcement.

## Build from source

Run these commands from the repository root in a Visual Studio Developer PowerShell or a shell with the MSVC x64 build tools available:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

The CMake configuration downloads the SQLite amalgamation from the official SQLite website when it is not already present in the build directory. An internet connection is therefore required for a clean first configure.

To compile the installer after a successful build:

```powershell
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" "Installer\Himaya.iss"
```

The installer output is written to `Installer\Output\Himaya-Setup.exe`.

## Tests and release status

The repository includes a Windows GitHub Actions workflow that builds the application and service, runs policy/service smoke checks, and compiles the installer. A successful workflow run is required before publishing a release. Automated checks do not replace testing on a clean Windows 11 machine.

No public installer release is announced by this development branch.

## Privacy

The application stores its configuration and event history locally under the Windows ProgramData area. Review the source code and any future privacy notice before using it. The current project does not claim that logs or configuration are transmitted to a Himaya-operated server.

## Language

- [Arabic documentation](README_AR.md)

## License

Himaya Security is licensed under the [MIT License](LICENSE).

## Disclaimer

This project is provided for personal productivity and content-restriction use. You are responsible for reviewing the effects of system-level restrictions before applying them. Always maintain a recovery path and a backup of important data.


## Shorts & Reels focus control

The development branch adds a separate Shorts & Reels page and a Chromium Manifest V3 extension for Chrome and Edge. It supports Basic sessions (early stop from Himaya) and Strict sessions (early stop disabled in the Himaya interface), with a saved timer and a selectable domain list. YouTube Shorts, Instagram Reels, and Facebook Reels use known route patterns; additional domains can be granted individually from the extension popup, which blocks common `/shorts`, `/reel`, and `/reels` routes.

The installer registers a read-only Native Messaging host. The extension must still be loaded once using the browser's **Load unpacked** option from the installed `BrowserExtension` folder. Strict mode is a browser-level focus aid, not a tamper-proof OS policy; a user with administrator access can disable or remove the extension. Existing app, website, DNS, service, and installer protection systems remain separate.
