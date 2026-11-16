#include "ConfigStore.h"
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace himaya {
namespace fs = std::filesystem;

static std::wstring toWide(const fs::path& p) { return p.wstring(); }

ConfigStore::ConfigStore() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_CREATE, nullptr, &raw)) && raw) {
        fs::path dir(raw);
        CoTaskMemFree(raw);
        dir /= L"Himaya";
        std::error_code ec;
        fs::create_directories(dir, ec);
        directory_ = dir.wstring();
    } else {
        directory_ = (fs::temp_directory_path() / L"Himaya").wstring();
        std::error_code ec;
        fs::create_directories(directory_, ec);
    }
}

std::wstring ConfigStore::fileFor(const std::wstring& category) const {
    if (category != L"applications" && category != L"websites" && category != L"paths")
        return L"";
    return (fs::path(directory_) / (category + L".txt")).wstring();
}

std::vector<std::wstring> ConfigStore::load(const std::wstring& category) const {
    std::vector<std::wstring> values;
    const auto file = fileFor(category);
    if (file.empty()) return values;
    std::wifstream in(file);
    in.imbue(std::locale(""));
    std::wstring line;
    while (std::getline(in, line)) if (!line.empty()) values.push_back(line);
    return values;
}

bool ConfigStore::appendUnique(const std::wstring& category, const std::wstring& value) const {
    if (value.empty()) return false;
    auto values = load(category);
    if (std::find(values.begin(), values.end(), value) != values.end()) return true;
    const auto file = fileFor(category);
    if (file.empty()) return false;
    std::wofstream out(file, std::ios::app);
    out.imbue(std::locale(""));
    out << value << L"\n";
    return out.good();
}

bool ConfigStore::remove(const std::wstring& category, const std::wstring& value) const {
    const auto file = fileFor(category);
    if (file.empty()) return false;
    auto values = load(category);
    const auto oldSize = values.size();
    values.erase(std::remove(values.begin(), values.end(), value), values.end());
    if (values.size() == oldSize) return true;
    std::wofstream out(file, std::ios::trunc);
    out.imbue(std::locale(""));
    for (const auto& item : values) out << item << L"\n";
    return out.good();
}
}
