#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace himaya {

struct WebsiteBlockResult {
    bool success{};
    size_t blockedDomains{};
    std::wstring message;
};

class WebsiteBlocker {
public:
    // Applies only user-managed website rules. Adult-content filtering is handled separately.
    // The caller must run elevated. Existing non-Himaya hosts-file content is preserved.
    static WebsiteBlockResult apply(const std::vector<std::wstring>& userDomains,
                                    bool includeAdultFeed);

    // Removes only the managed Himaya block; all other hosts-file lines are preserved.
    static WebsiteBlockResult restore();

    // Restores the complete hosts file from Himaya's first-use backup.
    static WebsiteBlockResult restoreOriginal();

    static bool isAdministrator();
};

} // namespace himaya
