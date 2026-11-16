#pragma once
#include <string>
#include <vector>

namespace himaya {
struct AppLockerRule {
    std::wstring path;
    std::wstring description;
};

class AppLockerPolicy {
public:
    // Applies Himaya-managed executable deny rules while preserving unrelated local AppLocker rules.
    static bool synchronize(const std::vector<AppLockerRule>& rules,
                            const std::vector<std::wstring>& userSids,
                            const std::wstring& workingDirectory,
                            std::wstring& error);
    // Removes only rules whose descriptions are owned by Himaya.
    static bool restore(const std::wstring& workingDirectory, std::wstring& error);
};
}
