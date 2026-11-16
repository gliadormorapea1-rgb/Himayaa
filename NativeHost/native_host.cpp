#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <iterator>
#include <ctime>

namespace {
bool readExact(void* data, size_t size) {
    auto* bytes = static_cast<unsigned char*>(data);
    size_t done = 0;
    while (done < size) {
        const int count = _read(_fileno(stdin), bytes + done, static_cast<unsigned>(size - done));
        if (count <= 0) return false;
        done += static_cast<size_t>(count);
    }
    return true;
}
bool writeMessage(const std::string& message) {
    if (message.size() > 1024 * 1024) return false;
    const uint32_t length = static_cast<uint32_t>(message.size());
    const unsigned char header[4] = {
        static_cast<unsigned char>(length & 0xff),
        static_cast<unsigned char>((length >> 8) & 0xff),
        static_cast<unsigned char>((length >> 16) & 0xff),
        static_cast<unsigned char>((length >> 24) & 0xff)
    };
    return _write(_fileno(stdout), header, 4) == 4 &&
           _write(_fileno(stdout), message.data(), static_cast<unsigned>(message.size())) ==
               static_cast<int>(message.size());
}
std::wstring readString(HKEY key, const wchar_t* name, const wchar_t* fallback) {
    wchar_t value[2048]{};
    DWORD bytes = sizeof(value);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value, &bytes) != ERROR_SUCCESS)
        return fallback;
    value[std::size(value) - 1] = L'\0';
    return value;
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), count, nullptr, nullptr);
    result.resize(static_cast<size_t>(count - 1));
    return result;
}
std::string escapeJson(const std::string& value) {
    std::string out;
    for (unsigned char ch : value) {
        if (ch == '"') out += "\\\"";
        else if (ch == '\\') out += "\\\\";
        else if (ch < 0x20) out += ' ';
        else out += static_cast<char>(ch);
    }
    return out;
}
std::string readState() {
    HKEY key{};
    const LONG opened = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Himaya\\Reels", 0, KEY_QUERY_VALUE, &key);
    if (opened != ERROR_SUCCESS)
        return "{\"enabled\":false,\"mode\":\"basic\",\"until\":0,\"sites\":\"youtube.com;instagram.com;facebook.com\"}";
    const auto enabled = readString(key, L"Enabled", L"0");
    const auto mode = readString(key, L"Mode", L"basic");
    const auto untilText = readString(key, L"Until", L"0");
    const auto sites = readString(key, L"Sites", L"youtube.com;instagram.com;facebook.com");
    RegCloseKey(key);
    long long until = 0;
    try { until = std::stoll(untilText); } catch (...) { until = 0; }
    const bool active = enabled == L"1" && until > static_cast<long long>(std::time(nullptr));
    return std::string("{\"enabled\":") + (active ? "true" : "false") +
        ",\"mode\":\"" + escapeJson(utf8(mode)) + "\",\"until\":" +
        std::to_string(until) + ",\"sites\":\"" + escapeJson(utf8(sites)) + "\"}";
}
}
int wmain() {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    for (;;) {
        uint32_t length = 0;
        if (!readExact(&length, sizeof(length))) return 0;
        if (length == 0 || length > 1024 * 1024) return 2;
        std::string request(length, '\0');
        if (!readExact(request.data(), request.size())) return 3;
        if (!writeMessage(readState())) return 4;
    }
}
