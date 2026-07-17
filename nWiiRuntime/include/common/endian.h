#pragma once

#include <cstdint>
#include <bit>

#ifdef _MSC_VER
#include <stdlib.h>
#define BSWAP16 _byteswap_ushort
#define BSWAP32 _byteswap_ulong
#define BSWAP64 _byteswap_uint64
#else
#define BSWAP16 __builtin_bswap16
#define BSWAP32 __builtin_bswap32
#define BSWAP64 __builtin_bswap64
#endif

namespace nwii {

template <typename T>
inline T swap_endian(T u) {
    if constexpr (std::endian::native == std::endian::little) {
        if constexpr (sizeof(T) == 2) {
            return BSWAP16(u);
        } else if constexpr (sizeof(T) == 4) {
            return BSWAP32(u);
        } else if constexpr (sizeof(T) == 8) {
            return BSWAP64(u);
        }
    }
    return u;
}

template <typename T>
class be_t {
    T value;
public:
    be_t() = default;
    be_t(T v) : value(swap_endian(v)) {}
    
    operator T() const { 
        return swap_endian(value); 
    }
    
    be_t& operator=(T v) { 
        value = swap_endian(v); 
        return *this; 
    }
};

using be32_t = be_t<uint32_t>;
using be16_t = be_t<uint16_t>;
using be64_t = be_t<uint64_t>;

} 
