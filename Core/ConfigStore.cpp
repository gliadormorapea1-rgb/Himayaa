#include "ConfigStore.h"

#include <windows.h>
#include <shlobj.h>
#include <sqlite3.h>
#include <filesystem>
#include <string>
#include <cstring>

namespace himaya {
namespace fs = std::filesystem;

namespace {
std::string toUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<size_t>(needed), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), needed, nullptr, nullptr)) return {};
    return result;
}

std::wstring fromUtf8(const unsigned char* value) {
    if (!value) return {};
    const int length = static_cast<int>(strlen(reinterpret_cast<const char*>(value)));
    if (length == 0) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<const char*>(value), length, nullptr, 0);
    if (needed <= 0) return {};
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<const char*>(value), length, result.data(), needed);
    return result;
}

bool bindText(sqlite3_stmt* statement, int index, const std::wstring& value) {
    const auto utf8 = toUtf8(value);
    return sqlite3_bind_text(statement, index, utf8.c_str(),
        static_cast<int>(utf8.size()), SQLITE_TRANSIENT) == SQLITE_OK;
}
}

ConfigStore::ConfigStore() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_CREATE, nullptr, &raw)) && raw) {
        fs::path base(raw);
        CoTaskMemFree(raw);
        base /= L"Himaya";
        directory_ = base.wstring();
    } else {
        directory_ = (fs::temp_directory_path() / L"Himaya").wstring();
    }

    std::error_code ec;
    fs::create_directories(directory_, ec);
    databasePath_ = (fs::path(directory_) / L"himaya.db").wstring();

    const auto dbUtf8 = toUtf8(databasePath_);
    if (sqlite3_open_v2(dbUtf8.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        setError(db_ ? sqlite3_errmsg(db_) : "SQLite could not open the database");
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        return;
    }
    sqlite3_busy_timeout(db_, 5000);
    if (!initialize()) {
        if (lastError_.empty()) lastError_ = L"تعذر تهيئة قاعدة بيانات Himaya.";
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

ConfigStore::~ConfigStore() {
    if (db_) sqlite3_close(db_);
}

void ConfigStore::setError(const char* message) {
    if (!message) { lastError_ = L"حدث خطأ غير معروف في قاعدة البيانات."; return; }
    const int count = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
    if (count <= 1) { lastError_ = L"حدث خطأ في قاعدة البيانات."; return; }
    lastError_.assign(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, message, -1, lastError_.data(), count);
    lastError_.resize(static_cast<size_t>(count - 1));
}

bool ConfigStore::execute(const char* sql) {
    if (!db_) return false;
    char* error = nullptr;
    const int result = sqlite3_exec(db_, sql, nullptr, nullptr, &error);
    if (result != SQLITE_OK) {
        setError(error ? error : sqlite3_errmsg(db_));
        sqlite3_free(error);
        return false;
    }
    return true;
}

bool ConfigStore::initialize() {
    if (!execute("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON; PRAGMA synchronous=FULL;")) return false;
    if (!execute(
        "CREATE TABLE IF NOT EXISTS entries("
        "id INTEGER PRIMARY KEY, category TEXT NOT NULL, value TEXT NOT NULL,"
        "lock_mode TEXT NOT NULL DEFAULT 'none', profile TEXT NOT NULL DEFAULT 'simple',"
        "created_at INTEGER NOT NULL DEFAULT(unixepoch()), expires_at INTEGER NULL,"
        "UNIQUE(category,value));"
        "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS events("
        "id INTEGER PRIMARY KEY, event_type TEXT NOT NULL, details TEXT NOT NULL,"
        "created_at INTEGER NOT NULL DEFAULT(unixepoch()));"
        "CREATE TABLE IF NOT EXISTS profiles("
        "profile_id TEXT PRIMARY KEY, display_name TEXT NOT NULL, description TEXT NOT NULL);"
    )) return false;

    // Approved defaults: comprehensive adult-domain category, dual unlock, shared administration.
    setSetting(L"adult_block_mode", L"all");
    setSetting(L"unlock_mode", L"both");
    setSetting(L"password_admin", L"both");
    if (getSetting(L"active_profile").empty()) setSetting(L"active_profile", L"simple");
    execute("INSERT OR IGNORE INTO profiles VALUES"
        "('simple','الإدمان البسيط','حدود وتنبيهات وجلسات تركيز'),"
        "('moderate','الإدمان المتوسط','جداول حظر وتأخير فك القيود'),"
        "('severe','الإدمان الحاد','قواعد صارمة ومراجعة إدارية'),"
        "('recovery','التعافي المكثف','متابعة مكثفة وأهداف ومساندة موثوقة');");
    return true;
}

std::vector<Entry> ConfigStore::load(const std::wstring& category) const {
    std::vector<Entry> result;
    if (!db_) return result;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT value,lock_mode,profile FROM entries WHERE category=?1 AND (expires_at IS NULL OR expires_at > unixepoch()) ORDER BY id DESC", -1, &statement, nullptr) != SQLITE_OK) return result;
    bindText(statement, 1, category);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        result.push_back({fromUtf8(sqlite3_column_text(statement, 0)),
                          fromUtf8(sqlite3_column_text(statement, 1)),
                          fromUtf8(sqlite3_column_text(statement, 2))});
    }
    sqlite3_finalize(statement);
    return result;
}

bool ConfigStore::addEntry(const std::wstring& category, const std::wstring& value,
                           const std::wstring& lockMode, const std::wstring& profile) {
    if (!db_ || value.empty()) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO entries(category,value,lock_mode,profile,expires_at) VALUES(?1,?2,?3,?4,"
                      "CASE ?3 WHEN '1h' THEN unixepoch()+3600 WHEN '24h' THEN unixepoch()+86400 "
                      "WHEN '7d' THEN unixepoch()+604800 WHEN '30d' THEN unixepoch()+2592000 "
                      "WHEN '90d' THEN unixepoch()+7776000 ELSE NULL END) "
                      "ON CONFLICT(category,value) DO UPDATE SET lock_mode=excluded.lock_mode,profile=excluded.profile,expires_at=excluded.expires_at";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) { setError(sqlite3_errmsg(db_)); return false; }
    bindText(statement, 1, category); bindText(statement, 2, value);
    bindText(statement, 3, lockMode); bindText(statement, 4, profile);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) setError(sqlite3_errmsg(db_));
    sqlite3_finalize(statement);
    if (ok) logEvent(L"entry_added", value);
    return ok;
}

bool ConfigStore::removeEntry(const std::wstring& category, const std::wstring& value) {
    if (!db_) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM entries WHERE category=?1 AND value=?2", -1, &statement, nullptr) != SQLITE_OK) return false;
    bindText(statement, 1, category); bindText(statement, 2, value);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) setError(sqlite3_errmsg(db_));
    sqlite3_finalize(statement);
    if (ok) logEvent(L"entry_removed", value);
    return ok;
}

bool ConfigStore::setSetting(const std::wstring& key, const std::wstring& value) {
    if (!db_) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "INSERT INTO settings(key,value) VALUES(?1,?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value", -1, &statement, nullptr) != SQLITE_OK) return false;
    bindText(statement, 1, key); bindText(statement, 2, value);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) setError(sqlite3_errmsg(db_));
    sqlite3_finalize(statement);
    return ok;
}

std::wstring ConfigStore::getSetting(const std::wstring& key, const std::wstring& fallback) const {
    if (!db_) return fallback;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT value FROM settings WHERE key=?1", -1, &statement, nullptr) != SQLITE_OK) return fallback;
    bindText(statement, 1, key);
    std::wstring value = fallback;
    if (sqlite3_step(statement) == SQLITE_ROW) value = fromUtf8(sqlite3_column_text(statement, 0));
    sqlite3_finalize(statement);
    return value;
}

bool ConfigStore::logEvent(const std::wstring& eventType, const std::wstring& details) {
    if (!db_) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "INSERT INTO events(event_type,details) VALUES(?1,?2)", -1, &statement, nullptr) != SQLITE_OK) return false;
    bindText(statement, 1, eventType); bindText(statement, 2, details);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}


std::vector<Event> ConfigStore::loadRecentEvents(size_t limit) const {
    std::vector<Event> result;
    if (!db_) return result;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT datetime(created_at,'unixepoch','localtime'),event_type,details FROM events ORDER BY id DESC LIMIT ?1",
        -1, &statement, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(limit));
    while (sqlite3_step(statement) == SQLITE_ROW) {
        result.push_back({
            fromUtf8(sqlite3_column_text(statement, 0)),
            fromUtf8(sqlite3_column_text(statement, 1)),
            fromUtf8(sqlite3_column_text(statement, 2))
        });
    }
    sqlite3_finalize(statement);
    return result;
}

bool ConfigStore::exportEventsCsv(const std::wstring& path) const {
    if (!db_ || path.empty()) return false;
    const auto events = loadRecentEvents(5000);
    auto quote = [](const std::wstring& value) {
        std::wstring escaped = L"\"";
        for (wchar_t ch : value) {
            if (ch == L'\"') escaped += L"\"\"";
            else if (ch == L'\r') continue;
            else escaped += ch;
        }
        escaped += L"\"";
        return escaped;
    };
    std::wstring csv = L"\uFEFFالتاريخ,الحدث,التفاصيل\r\n";
    for (const auto& event : events) {
        csv += quote(event.timestamp) + L"," + quote(event.type) + L"," + quote(event.details) + L"\r\n";
    }
    const auto bytes = toUtf8(csv);
    if (bytes.empty() && !csv.empty()) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024u * 1024u));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (!FlushFileBuffers(file)) ok = false;
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

} // namespace himaya
