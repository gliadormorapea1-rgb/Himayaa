#pragma once

#include <string>
#include <vector>

struct sqlite3;

namespace himaya {

struct Entry {
    std::wstring value;
    std::wstring lockMode;
    std::wstring profile;
};

struct Event {
    std::wstring timestamp;
    std::wstring type;
    std::wstring details;
};

class ConfigStore {
public:
    ConfigStore();
    ~ConfigStore();
    ConfigStore(const ConfigStore&) = delete;
    ConfigStore& operator=(const ConfigStore&) = delete;

    const std::wstring& directory() const noexcept { return directory_; }
    const std::wstring& databasePath() const noexcept { return databasePath_; }
    bool isReady() const noexcept { return db_ != nullptr; }
    const std::wstring& lastError() const noexcept { return lastError_; }

    std::vector<Entry> load(const std::wstring& category) const;
    bool addEntry(const std::wstring& category, const std::wstring& value,
                  const std::wstring& lockMode, const std::wstring& profile);
    bool removeEntry(const std::wstring& category, const std::wstring& value);
    bool setSetting(const std::wstring& key, const std::wstring& value);
    std::wstring getSetting(const std::wstring& key, const std::wstring& fallback = L"") const;
    bool logEvent(const std::wstring& eventType, const std::wstring& details);
    std::vector<Event> loadRecentEvents(size_t limit = 5000) const;
    bool exportEventsCsv(const std::wstring& path) const;

private:
    bool initialize();
    bool execute(const char* sql);
    void setError(const char* message);

    std::wstring directory_;
    std::wstring databasePath_;
    std::wstring lastError_;
    sqlite3* db_{};
};

} // namespace himaya
