#include "WebsiteBlocker.h"

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "dnsapi.lib")

namespace himaya {
namespace {
constexpr wchar_t kBegin[] = L"# BEGIN HIMAYA MANAGED BLOCK";
constexpr wchar_t kEnd[] = L"# END HIMAYA MANAGED BLOCK";
constexpr wchar_t kAdultFeed[] = L"https://raw.githubusercontent.com/StevenBlack/hosts/master/alternates/porn/hosts";

std::wstring trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool validDomain(std::wstring_view domain) {
    if (domain.empty() || domain.size() > 253 || domain.front() == L'.' || domain.back() == L'.') return false;
    size_t labelStart = 0;
    while (labelStart < domain.size()) {
        const size_t dot = domain.find(L'.', labelStart);
        const size_t end = dot == std::wstring_view::npos ? domain.size() : dot;
        const size_t length = end - labelStart;
        if (length == 0 || length > 63 || domain[labelStart] == L'-' || domain[end - 1] == L'-') return false;
        for (size_t i = labelStart; i < end; ++i) {
            const wchar_t c = domain[i];
            if (!((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'-')) return false;
        }
        if (dot == std::wstring_view::npos) break;
        labelStart = dot + 1;
    }
    return domain.find(L'.') != std::wstring_view::npos;
}

std::wstring normalizeDomain(std::wstring input) {
    input = trim(std::move(input));
    if (input.empty()) return {};
    std::transform(input.begin(), input.end(), input.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    const auto scheme = input.find(L"://");
    if (scheme != std::wstring::npos) input.erase(0, scheme + 3);
    const auto slash = input.find_first_of(L"/?#");
    if (slash != std::wstring::npos) input.resize(slash);
    const auto colon = input.find(L':'); // discard an optional port
    if (colon != std::wstring::npos) input.resize(colon);
    // Preserve the exact hostname. A leading "www." is a real DNS label, not a safe alias to strip.
    while (!input.empty() && input.back() == L'.') input.pop_back();
    // Convert internationalized domain names to ASCII/Punycode before writing hosts.
    wchar_t ascii[256]{};
    const int converted = IdnToAscii(IDN_USE_STD3_ASCII_RULES, input.c_str(), -1, ascii, static_cast<int>(std::size(ascii)));
    if (converted > 1) input.assign(ascii, static_cast<size_t>(converted - 1));
    return validDomain(input) ? input : L"";
}

bool downloadAdultFeed(std::set<std::wstring>& domains, std::wstring& error) {
    wchar_t hostBuffer[256]{};
    wchar_t pathBuffer[2048]{};
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = hostBuffer;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(hostBuffer));
    parts.lpszUrlPath = pathBuffer;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(pathBuffer));
    if (!WinHttpCrackUrl(kAdultFeed, 0, 0, &parts)) {
        error = L"Could not parse the website list URL.";
        return false;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    HINTERNET session = WinHttpOpen(L"Himaya Security/0.3", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { error = L"Could not start the website list update connection."; return false; }
    WinHttpSetTimeouts(session, 10000, 10000, 20000, 30000);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    if (!connection) { WinHttpCloseHandle(session); error = L"Could not connect to the website list source."; return false; }
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!request || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) {
        if (request) WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
        error = L"Failed to download the adult website list. Check your internet connection and try again.";
        return false;
    }
    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        WinHttpCloseHandle(request); WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
        error = L"The website list source returned an unsuccessful response.";
        return false;
    }

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            WinHttpCloseHandle(request); WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
            error = L"The connection was interrupted while reading the website list.";
            return false;
        }
        if (available == 0) break;
        if (body.size() + available > 20u * 1024u * 1024u) {
            WinHttpCloseHandle(request); WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
            error = L"The website list exceeds the safe size limit.";
            return false;
        }
        const size_t offset = body.size();
        body.resize(offset + available);
        DWORD totalRead = 0;
        while (totalRead < available) {
            DWORD read = 0;
            if (!WinHttpReadData(request, body.data() + offset + totalRead,
                                 available - totalRead, &read) || read == 0) {
                WinHttpCloseHandle(request); WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
                error = L"An error occurred while reading the website list.";
                return false;
            }
            totalRead += read;
        }
    }
    WinHttpCloseHandle(request); WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
    if (body.empty()) { error = L"The downloaded website list is empty."; return false; }

    std::istringstream stream(body);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string address, domain;
        row >> address >> domain;
        if (domain.empty()) continue;
        std::wstring wide(domain.begin(), domain.end());
        auto normalized = normalizeDomain(std::move(wide));
        // Community feeds already enumerate the exact hostnames they intend to block.
        // Do not invent www.<host> entries for subdomains such as ww.example.com.
        if (!normalized.empty()) domains.insert(std::move(normalized));
    }
    if (domains.empty()) { error = L"No valid domains were found in the downloaded list."; return false; }
    return true;
}

std::wstring hostsPath() {
    wchar_t system[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    if (!length || length >= MAX_PATH) return {};
    return (std::filesystem::path(system) / L"drivers" / L"etc" / L"hosts").wstring();
}

bool readFile(const std::wstring& path, std::wstring& content) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 32 * 1024 * 1024) {
        CloseHandle(file); return false;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    size_t totalRead = 0;
    bool ok = true;
    while (totalRead < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - totalRead, 1024u * 1024u));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + totalRead, chunk, &read, nullptr) || read == 0) {
            ok = false;
            break;
        }
        totalRead += read;
    }
    CloseHandle(file);
    if (!ok || totalRead != bytes.size()) return false;
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) bytes.erase(0, 3);
    if (bytes.empty()) { content.clear(); return true; }
    const int count = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (count <= 0) {
        // Windows hosts files are commonly ASCII/ANSI. Preserve those files as a fallback.
        const int ansiCount = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (ansiCount <= 0) return false;
        content.resize(static_cast<size_t>(ansiCount));
        MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), content.data(), ansiCount);
        return true;
    }
    content.resize(static_cast<size_t>(count));
    return MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), content.data(), count) > 0;
}

bool writeAtomically(const std::wstring& path, const std::wstring& content) {
    const int needed = WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);
    if (needed < 0) return false;
    std::string bytes(static_cast<size_t>(needed), '\0');
    if (needed && !WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.size()), bytes.data(), needed, nullptr, nullptr)) return false;
    const std::wstring temp = path + L".himaya.tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    size_t totalWritten = 0;
    bool ok = true;
    while (totalWritten < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - totalWritten, 1024u * 1024u));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + totalWritten, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        totalWritten += written;
    }
    if (ok) ok = totalWritten == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) { DeleteFileW(temp.c_str()); return false; }
    if (!ReplaceFileW(path.c_str(), temp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
        // If the hosts file did not exist, a same-volume move is sufficient.
        if (GetLastError() == ERROR_FILE_NOT_FOUND && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
        DeleteFileW(temp.c_str()); return false;
    }
    return true;
}

bool removeManagedBlock(const std::wstring& source, std::wstring& cleaned, bool& foundBlock) {
    std::wstringstream input(source), output;
    std::wstring line;
    bool inside = false;
    foundBlock = false;
    while (std::getline(input, line)) {
        const auto value = trim(line);
        if (value == kBegin) {
            if (inside || foundBlock) return false;
            inside = true;
            foundBlock = true;
            continue;
        }
        if (value == kEnd) {
            if (!inside) return false;
            inside = false;
            continue;
        }
        if (!inside) output << line << L"\r\n";
    }
    if (inside) return false; // Never risk deleting the rest of a manually edited hosts file.
    cleaned = output.str();
    return true;
}

bool flushDns() {
    wchar_t system[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    std::wstring command = L"\"" + std::wstring(system) + L"\\ipconfig.exe\" /flushdns";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
    const DWORD waitResult = WaitForSingleObject(process.hProcess, 15000);
    DWORD exitCode = STILL_ACTIVE;
    const bool gotExitCode = waitResult == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return gotExitCode && exitCode == 0;
}
} // namespace

bool WebsiteBlocker::isAdministrator() {
    BOOL isAdmin = FALSE;
    PSID administrators = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administrators)) {
        CheckTokenMembership(nullptr, administrators, &isAdmin);
        FreeSid(administrators);
    }
    return isAdmin == TRUE;
}

WebsiteBlockResult WebsiteBlocker::apply(const std::vector<std::wstring>& userDomains, bool includeAdultFeed) {
    if (!isAdministrator()) return {false, 0, L"Administrator privileges are required to apply website blocking."};
    std::set<std::wstring> domains;
    for (const auto& value : userDomains) {
        auto domain = normalizeDomain(value);
        if (domain.empty()) continue;
        // For manually entered sites, block both the supplied hostname and its common www alias.
        // If the user supplied www.<host>, also include the base host without stripping other labels.
        if (domain.rfind(L"www.", 0) == 0) {
            domains.insert(domain.substr(4));
            domains.insert(std::move(domain));
        } else {
            domains.insert(domain);
            domains.insert(L"www." + domain);
        }
    }
    if (includeAdultFeed) {
        std::wstring error;
        if (!downloadAdultFeed(domains, error)) return {false, 0, error};

        // Supplemental adult domains that may be missing from the community feed.
        domains.insert(L"xnxx-arabic.com");
        domains.insert(L"www.xnxx-arabic.com");
        domains.insert(L"ww.xnxx-arabic.com");
        domains.insert(L"www.ww.xnxx-arabic.com");
    }
    if (domains.empty()) return {false, 0, L"There are no valid websites to block."};

    const auto path = hostsPath();
    std::wstring current;
    if (path.empty() || !readFile(path, current)) return {false, 0, L"Could not read the Windows hosts file."};
    // Keep one untouched copy from before Himaya first modifies this file.
    const std::wstring backupPath = path + L".himaya-original.bak";
    if (GetFileAttributesW(backupPath.c_str()) == INVALID_FILE_ATTRIBUTES &&
        !CopyFileW(path.c_str(), backupPath.c_str(), TRUE)) {
        return {false, 0, L"Could not create a backup of the hosts file. No changes were made."};
    }
    std::wstring updated;
    bool hadManagedBlock = false;
    if (!removeManagedBlock(current, updated, hadManagedBlock))
        return {false, 0, L"Himaya markers in the hosts file are incomplete or duplicated. The file was not modified to protect your settings."};
    (void)hadManagedBlock;
    if (!updated.empty() && updated.back() != L'\n') updated += L"\r\n";
    updated += kBegin; updated += L"\r\n";
    updated += L"# Adult-domain list: StevenBlack/hosts (porn extension) + user rules\r\n";
    for (const auto& domain : domains) {
        // Write each exact hostname once; www variants are added deliberately above where appropriate.
        updated += L"0.0.0.0 " + domain + L"\r\n";
    }
    updated += kEnd; updated += L"\r\n";
    if (!writeAtomically(path, updated)) return {false, 0, L"Could not update the hosts file. The original file was not replaced."};
    const bool dnsFlushed = flushDns();
    return {true, domains.size(), dnsFlushed ? L"Domain blocking was applied and the DNS cache was flushed." : L"Blocking rules were written, but the DNS cache could not be flushed automatically. Restart your browser or run ipconfig /flushdns."};
}

WebsiteBlockResult WebsiteBlocker::restore() {
    if (!isAdministrator()) return {false, 0, L"Administrator privileges are required to restore website settings."};
    const auto path = hostsPath();
    std::wstring current;
    if (path.empty() || !readFile(path, current)) return {false, 0, L"Could not read the Windows hosts file."};
    std::wstring updated;
    bool foundBlock = false;
    if (!removeManagedBlock(current, updated, foundBlock))
        return {false, 0, L"Himaya markers in the hosts file are incomplete or duplicated. The file was not modified."};
    if (!foundBlock) return {true, 0, L"There are no Himaya rules in the hosts file."};
    if (!writeAtomically(path, updated)) return {false, 0, L"Could not restore the hosts file. The original file was not replaced."};
    DeleteFileW((path + L".himaya-original.bak").c_str());
    const bool dnsFlushed = flushDns();
    return {true, 0, dnsFlushed ? L"Himaya rules were removed. All other hosts file entries were preserved." : L"Himaya rules were removed, but the DNS cache could not be flushed automatically."};
}

} // namespace himaya
