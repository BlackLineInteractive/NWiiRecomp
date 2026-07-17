#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace nwii {
namespace analyzer {

struct Signature {
    std::string name;
    std::vector<std::string> instructions; 
};

class SignatureScanner {
public:
    SignatureScanner();

    
    std::string match(const std::vector<uint32_t>& instructions) const;

private:
    std::vector<Signature> signatures_;
};

} 
} 
