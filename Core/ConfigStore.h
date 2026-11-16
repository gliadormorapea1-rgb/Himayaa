#pragma once
#include <string>
#include <vector>

namespace himaya {
class ConfigStore {
public:
    ConfigStore();
    const std::wstring& directory() const noexcept { return directory_; }
    std::vector<std::wstring> load(const std::wstring& category) const;
    bool appendUnique(const std::wstring& category, const std::wstring& value) const;
    bool remove(const std::wstring& category, const std::wstring& value) const;
private:
    std::wstring directory_;
    std::wstring fileFor(const std::wstring& category) const;
};
}
