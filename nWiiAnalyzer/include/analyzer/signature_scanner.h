#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace nwii {
namespace analyzer {

struct Signature {
    std::string name;
    std::vector<std::string> instructions; // Each string is an 8-char hex or "??" wildcard
};

class SignatureScanner {
public:
    SignatureScanner();

    // Check if the given instructions match any signature in the database
    // The instructions vector should contain the raw 32-bit opcodes of a function
    std::string match(const std::vector<uint32_t>& instructions) const;

private:
    std::vector<Signature> signatures_;
};

} // namespace analyzer
} // namespace nwii
