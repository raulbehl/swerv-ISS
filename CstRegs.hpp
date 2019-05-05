
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <unordered_map>
#include <type_traits>
#include <assert.h>

namespace WdRiscv
{

  /// Symbolic names of the integer registers.
  enum CstRegNumber {
    RegQ0 = 0,
    RegQ1 = 1,
    RegQ2 = 2,
    RegQ3 = 3
  };

  template <typename URV>
  class Core;

  /// Model a RISCV integer register file.
  /// URV (unsigned register value) is the register value type. For
  /// 32-bit registers, URV should be uint32_t. For 64-bit integers,
  /// it should be uint64_t.
  template <typename URV>
  class CstRegs
  {
  public:

    friend class Core<URV>;

    /// Constructor: Define a register file with the given number of
    /// registers. Each register is of type URV. All registers initialized
    /// to zero.
    CstRegs(unsigned registerCount);


    /// Destructor.
    ~CstRegs()
    { regs_.clear(); }
    
    /// Return value of ith register. Register zero always yields zero.
    URV read(unsigned i) const
    { return regs_[i]; }

    /// Set value of ith register to the given value. Setting register
    /// zero has no effect.
    void write(unsigned i, URV value)
    {
      originalValue_ = regs_[i];
      regs_[i] = value;
      lastWrittenReg_ = i;
    }

    /// Similar to write but does not record a change.
    void poke(unsigned i, URV value)
    {
      regs_.at(i) = value;
    }

    /// Return the count of registers in this register file.
    size_t size() const
    { return regs_.size(); }

    /// Set ix to the number of the register corresponding to the
    /// given name returning true on success and false if no such
    /// register.  For example, if name is "x2" then ix will be set to
    /// 2. If name is "tp" then ix will be set to 4.
    bool findReg(const std::string& name, unsigned& ix) const;

    /// Return the number of bits in a register in this register file.
    static constexpr uint32_t regWidth()
    { return sizeof(URV)*8; }

    /// Return the name of the given register.
    std::string regName(unsigned i, bool abiNames = false) const
    {
      if (abiNames)
      {
        if (i < numberToAbiName_.size())
          return numberToAbiName_[i];
        return std::string("x?");
      }
      if (i < numberToName_.size())
        return numberToName_[i];
      return std::string("x?");
    }

  protected:

    void reset()
    {
      clearLastWrittenReg();
      for (auto& reg : regs_)
        reg = 0;
    }

    /// Clear the number denoting the last written register.
    void clearLastWrittenReg()
    { lastWrittenReg_ = -1; }

    /// Return the number of the last written register or -1 if no register has
    /// been written since the last clearLastWrittenReg.
    int getLastWrittenReg() const
    { return lastWrittenReg_; }

    /// Set regIx and regValue to the index and previous value (before
    /// write) of the last written register returning true on success
    /// and false if no integer was written by the last executed
    /// instruction (in which case regIx and regVal are left
    /// unmodified).
    bool getLastWrittenReg(unsigned& regIx, URV& regValue) const
    {
      if (lastWrittenReg_ < 0) return false;
      regIx = lastWrittenReg_;
      regValue = originalValue_;
      return true;
    }

  private:

    std::vector<URV> regs_;
    int lastWrittenReg_ = -1;  // Register accessed in most recent write.
    URV originalValue_ = 0;    // Original value of last written reg.
    std::unordered_map<std::string, CstRegNumber> nameToNumber_;
    std::vector<std::string> numberToAbiName_;
    std::vector<std::string> numberToName_;
  };
}
