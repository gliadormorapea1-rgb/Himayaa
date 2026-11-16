#pragma once
#include <string>
#include <vector>
namespace himaya {
class ConfigStore;
struct PolicySyncResult { bool success{}; size_t applied{}; std::wstring message; };
class ExecutionPolicy {
public:
    // Applies exact executable rules and folder execution rules through Windows SRP.
    // Must be called by the LocalSystem HimayaGuard service.
    static PolicySyncResult synchronize(ConfigStore& store);
    // Removes Himaya-managed execution rules and restores saved folder ACLs.
    static PolicySyncResult restoreAll();
};
}
