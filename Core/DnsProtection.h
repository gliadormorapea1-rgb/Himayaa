#pragma once
#include <string>
namespace himaya {
struct DnsProtectionResult { bool success{}; std::wstring message; };
class DnsProtection {
public:
    static DnsProtectionResult enable();
    static DnsProtectionResult synchronize();
    static DnsProtectionResult restore();
};
}
