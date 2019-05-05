#include "CstRegs.hpp"


using namespace WdRiscv;


template <typename URV>
CstRegs<URV>::CstRegs(unsigned regCount)
  : regs_(regCount, 0)
{
  numberToName_.resize(4);

  for (unsigned ix = 0; ix < 4; ++ix)
    {
      std::string name = "q" + std::to_string(ix);
      nameToNumber_[name] = CstRegNumber(ix);
      numberToName_[ix] = name;
    }

  numberToAbiName_ = {"q0", "q1", "q2", "q3"};

  for (unsigned ix = 0; ix < 4; ++ix)
    {
      std::string abiName = numberToAbiName_.at(ix);
      nameToNumber_[abiName] = CstRegNumber(ix);
    }
}


template <typename URV>
bool
CstRegs<URV>::findReg(const std::string& name, unsigned& ix) const
{
  const auto iter = nameToNumber_.find(name);
  if (iter == nameToNumber_.end())
    return false;

  ix = iter->second;
  return true;
}


template class WdRiscv::CstRegs<uint32_t>;
template class WdRiscv::CstRegs<uint64_t>;
