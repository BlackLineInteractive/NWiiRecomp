#pragma once

#include <cstdint>

namespace nwii {
namespace ppc {

class Instruction {
public:
  explicit Instruction(uint32_t value) : value_(value) {}

  uint32_t value() const { return value_; }

  uint32_t opcode() const { return (value_ >> 26) & 0x3F; }

  uint32_t rd() const { return (value_ >> 21) & 0x1F; }
  uint32_t rs() const { return rd(); }
  uint32_t bo() const { return rd(); }
  uint32_t ra() const { return (value_ >> 16) & 0x1F; }
  uint32_t bi() const { return ra(); }
  uint32_t rb() const { return (value_ >> 11) & 0x1F; }
  uint32_t rc() const { return (value_ >> 6) & 0x1F; }
  int16_t simm() const { return static_cast<int16_t>(value_ & 0xFFFF); }
  uint16_t uimm() const { return static_cast<uint16_t>(value_ & 0xFFFF); }
  uint32_t extended_opcode() const { return (value_ >> 1) & 0x3FF; }

  bool lk() const { return (value_ & 1) != 0; }

  bool is_branch_link() const {
    return (opcode() == 18 || opcode() == 16 || opcode() == 19) && lk();
  }

  bool is_unconditional_branch() const { return opcode() == 18; }

  bool is_branch_to_lr() const {
    
    if (opcode() != 19)
      return false;
    uint32_t xo = extended_opcode();
    return xo == 16;
  }

  
  bool is_unconditional_indirect_branch() const {
    if (opcode() != 19)
      return false;
    uint32_t xo = extended_opcode();
    
    if (xo == 16 || xo == 528 || xo == 50) {
      if (xo == 50)
        return true; 

      return (bo() & 0x14) == 0x14;
    }
    return false;
  }

  
  uint32_t branch_target(uint32_t current_pc) const {
    if (opcode() == 18) {
      
      uint32_t li = (value_ >> 2) & 0xFFFFFF; 
      
      int32_t offset = (li & 0x800000) ? (li | 0xFF000000) : li;
      offset <<= 2; 

      bool aa = (value_ >> 1) & 1; 
      if (aa) {
        return static_cast<uint32_t>(offset);
      } else {
        return current_pc + offset;
      }
    }
    if (opcode() == 16) {
      
      uint32_t bd = (value_ >> 2) & 0x3FFF; 
      
      int32_t offset = (bd & 0x2000) ? (bd | 0xFFFFC000) : bd;
      offset <<= 2;

      bool aa = (value_ >> 1) & 1;
      if (aa) {
        return static_cast<uint32_t>(offset);
      } else {
        return current_pc + offset;
      }
    }
    return 0; 
  }

private:
  uint32_t value_;
};

} 
} 
