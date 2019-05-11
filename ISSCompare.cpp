#include <iostream>
#include <fstream>
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <string.h>
#include <boost/program_options.hpp>
#include "CoreConfig.hpp"
#include "WhisperMessage.h"
#include "Core.hpp"
#include "Interactive.hpp"
#include "Server.hpp"
#include "instforms.hpp"
#include "dpi.h"

typedef std::vector<std::string> StringVec;

using namespace WdRiscv;

size_t memorySize = 0x90000000;  // 2 gigs

unsigned  registerCount = 32;
unsigned  hartId = 0;
unsigned  prevPC = 0;
uint32_t  inst= 0;
uint32_t  op0 = 0;
uint32_t  op1 = 0;
int32_t   op2 = 0;
int32_t   op3 = 0;

WdRiscv::Memory memory(memorySize);
template <typename XLEN>
WdRiscv::Core<XLEN> core(hartId, memory, registerCount);
WdRiscv::InstInfo info;

// Hold values provided on the command line.
struct Args
{
  StringVec   hexFiles;        // Hex files to be loaded into simulator memory.
  std::string traceFile;       // Log of state change after each instruction.
  std::string commandLogFile;  // Log of interactive or socket commands.
  std::string consoleOutFile;  // Console io output file.
  std::string serverFile;      // File in which to write server host and port.
  std::string instFreqFile;    // Instruction frequency file.
  std::string configFile;      // Configuration (JSON) file.
  std::string isa;
  StringVec   regInits;        // Initial values of regs
  StringVec   codes;           // Instruction codes to disassemble
  StringVec   targets;         // Target (ELF file) programs and associated
                               // program options to be loaded into simulator
                               // memory. Each target plus args is one string.
  std::string targetSep = " "; // Target program argument separator.

  // Ith item is a vector of strings representing ith target and its args.
  std::vector<StringVec> expandedTargets;

  uint64_t startPc = 0;
  uint64_t endPc = 0;
  uint64_t toHost = 0;
  uint64_t consoleIo = 0;
  uint64_t instCountLim = ~uint64_t(0);

  unsigned regWidth = 32;

  bool help = false;
  bool hasStartPc = false;
  bool hasEndPc = false;
  bool hasToHost = false;
  bool hasConsoleIo = false;
  bool hasRegWidth = false;
  bool trace = false;
  bool interactive = false;
  bool verbose = false;
  bool version = false;
  bool traceLoad = false;  // Trace load address if true.
  bool triggers = false;   // Enable debug triggers when true.
  bool counters = false;   // Enable performance counters when true.
  bool gdb = false;        // Enable gdb mode when true.
  bool abiNames = false;   // Use ABI register names in inst disassembly.
  bool newlib = false;     // True if target program linked with newlib.
};

// Open the trace-file, command-log and console-output files
// specified on the command line. Return true if successful or false
// if any specified file fails to open.
static
bool
openUserFiles(const Args& args, FILE*& traceFile, FILE*& commandLog,
	      FILE*& consoleOut) {
  if (not args.traceFile.empty()) {
    traceFile = fopen(args.traceFile.c_str(), "w");
    if (not traceFile) {
      std::cerr << "Failed to open trace file '" << args.traceFile
                << "' for output\n";
      return false;
    }
  }

  if (args.trace and traceFile == NULL)
    traceFile = stdout;
  if (traceFile)
    setlinebuf(traceFile);  // Make line-buffered.

  if (not args.commandLogFile.empty()) {
    commandLog = fopen(args.commandLogFile.c_str(), "w");
    if (not commandLog) {
      std::cerr << "Failed to open command log file '"
		    << args.commandLogFile << "' for output\n";
      return false;
    }
    setlinebuf(commandLog);  // Make line-buffered.
  }

  if (not args.consoleOutFile.empty()) {
    consoleOut = fopen(args.consoleOutFile.c_str(), "w");
    if (not consoleOut) {
	  std::cerr << "Failed to open console output file '"
      << args.consoleOutFile << "' for output\n";
	  return false;
    }
  }
  return true;
}

// Counterpart to openUserFiles: Close any open user file.
static
void
closeUserFiles(FILE*& traceFile, FILE*& commandLog, FILE*& consoleOut) {
  if (consoleOut and consoleOut != stdout)
    fclose(consoleOut);
  consoleOut = nullptr;

  if (traceFile and traceFile != stdout)
    fclose(traceFile);
  traceFile = nullptr;

  if (commandLog and commandLog != stdout)
    fclose(commandLog);
  commandLog = nullptr;
}

template<typename URV>
bool
loadElfFile(Core<URV>& core, const std::string& filePath)
{
  size_t entryPoint = 0, exitPoint = 0;

  if (not core.loadElfFile(filePath, entryPoint, exitPoint))
    return false;

  core.pokePc(URV(entryPoint));

  if (exitPoint)
    core.setStopAddress(URV(exitPoint));

  ElfSymbol sym;
  if (core.findElfSymbol("tohost", sym))
    core.setToHostAddress(sym.addr_);

  if (core.findElfSymbol("__whisper_console_io", sym))
    core.setConsoleIo(URV(sym.addr_));

  if (core.findElfSymbol("__global_pointer$", sym))
    core.pokeIntReg(RegGp, URV(sym.addr_));

  if (core.findElfSymbol("_end", sym))   // For newlib emulation.
    core.setTargetProgramBreak(URV(sym.addr_));
  else
    core.setTargetProgramBreak(URV(exitPoint));

  return true;
}

template <typename URV>
bool ISSinit(bool filetype, std::string filename) {
  Args args;
  // Determine simulated memory size. Default to 4 gigs.
  // If running a 32-bit machine (pointer siz = 32 bits), try 2 gigs.

  FILE* traceFile = nullptr;
  FILE* commandLog = nullptr;
  FILE* consoleOut = stdout;
  if (not openUserFiles(args, traceFile, commandLog, consoleOut))
    return false;

  core<URV>.setConsoleOutput(consoleOut);

  core<URV>.reset();

  std::cout << "In ISSinit\n"<<std::endl;

  if (filetype) {
    if (not core<URV>.loadHexFile(filename)) {
      std::cerr<<"ERROR: Couldn't load hex file"<<std::endl;
      return false;
    }
  }
  else {
    if (not loadElfFile(core<URV>, filename)) {
      std::cerr<<"ERROR: Couldn't load elf file"<<std::endl;
      return false;
    }
  }

  // Command line to-host overrides that of ELF and config file.
  if (args.hasToHost)
    core<URV>.setToHostAddress(args.toHost);

  // Command-line entry point overrides that of ELF.
  if (args.hasStartPc)
    core<URV>.pokePc(URV(args.startPc));

  // Command-line exit point overrides that of ELF.
  if (args.hasEndPc)
    core<URV>.setStopAddress(URV(args.endPc));

  // Command-line console io address overrides config file.
  if (args.hasConsoleIo)
    core<URV>.setConsoleIo(URV(args.consoleIo));

  // Set instruction count limit.
  core<URV>.setInstructionCountLimit(args.instCountLim);

  // Print load-instruction data-address when tracing instructions.
  core<URV>.setTraceLoad(args.traceLoad);

  core<URV>.enableTriggers(args.triggers);
  core<URV>.enableGdb(args.gdb);
  core<URV>.enablePerformanceCounters(args.counters);
  core<URV>.enableAbiNames(args.abiNames);
  core<URV>.enableNewlib(args.newlib);

  closeUserFiles(traceFile, commandLog, consoleOut);
  prevPC = core<uint32_t>.pc_;
}

extern int issInit (int argc, char* argv)  {

  unsigned regWidth = 32;
  bool ok = true;

  try {
    if (regWidth == 32)
      ok = ISSinit<uint32_t>(argc, argv);
    else if (regWidth == 64)
      ok = ISSinit<uint64_t>(argc, argv);
    else {
      std::cerr << "Invalid register width: " << regWidth;
      std::cerr << " -- expecting 32 or 64\n";
      ok = false;
    }
  }
  catch (std::exception& e) {
    std::cerr << e.what() << '\n';
    ok = false;
  }
	
  return ok? 0 : 1;
}

bool compareRD (uint32_t spirit_rd_addr, uint32_t spirit_rd_wdata, uint32_t iss_rd_addr) {
  uint32_t iss_rd_wdata;

  iss_rd_wdata = core<uint32_t>.intRegs_.read(iss_rd_addr);

  if (spirit_rd_addr != iss_rd_addr) {
    fprintf (stdout, "Unexpected R%-.2d Register\n", spirit_rd_addr);
    fprintf (stdout, "Expecting  R%-.2d Register\n", iss_rd_addr);
    fprintf (stdout, "RD Value Mismatch\n");
    return false;
  }
  else if (spirit_rd_wdata != iss_rd_wdata) {
    fprintf (stdout, "RTL R%-.2d: %-.8x\t ISS R%-.2d: %-.8x\n", spirit_rd_addr, spirit_rd_wdata,
                                                       iss_rd_addr, iss_rd_wdata);
    fprintf (stdout, "RD Value Mismatch\n");
    return false;
  }
  return true;
}

bool compareRS1 (uint32_t spirit_rs1_addr, uint32_t spirit_rs1_rdata, uint32_t iss_rs1_addr) {
  uint32_t iss_rs1_rdata;

  iss_rs1_rdata = (op0==iss_rs1_addr) ?
                   core<uint32_t>.intRegs_.originalValue_ :
                   core<uint32_t>.intRegs_.read(iss_rs1_addr);

  if (spirit_rs1_addr != iss_rs1_addr) {
    fprintf (stdout, "Unexpected R%-.2d Register\n", spirit_rs1_addr);
    fprintf (stdout, "Expecting  R%-.2d Register\n", iss_rs1_addr);
    fprintf (stdout, "RS1 Value Mismatch\n");
    return false;
  }
  else if (spirit_rs1_rdata != iss_rs1_rdata) {
    fprintf (stdout, "RTL R%-.2d: %-.8x\t ISS R%-.2d: %-.8x\n", spirit_rs1_addr, spirit_rs1_rdata,
                                                       iss_rs1_addr, iss_rs1_rdata);
    fprintf (stdout, "RS1 Value Mismatch\n");
    return false;
  }
  return true;
}

bool compareRS2 (uint32_t spirit_rs2_addr, uint32_t spirit_rs2_rdata, uint32_t iss_rs2_addr) {
  uint32_t iss_rs2_rdata;

  iss_rs2_rdata = (core<uint32_t>.intRegs_.lastWrittenReg_==iss_rs2_addr) ?
                       core<uint32_t>.intRegs_.originalValue_ :
                       core<uint32_t>.intRegs_.read(iss_rs2_addr);

  if (spirit_rs2_addr != iss_rs2_addr) {
    fprintf (stdout, "Unexpected R%-.2d Register\n", spirit_rs2_addr);
    fprintf (stdout, "Expecting  R%-.2d Register\n", iss_rs2_addr);
    fprintf (stdout, "RS2 Value Mismatch\n");
    return false;
  }
  else if (spirit_rs2_rdata != iss_rs2_rdata) {
    fprintf (stdout, "RTL R%-.2d: %-.8x\t ISS R%-.2d: %-.8x\n", spirit_rs2_addr, spirit_rs2_rdata,
                                                       iss_rs2_addr, iss_rs2_rdata);
    fprintf (stdout, "RS2 Value Mismatch\n");
    return false;
  }
  return true;
}

extern "C"
void issDecode () {

  bool fetchOK = true;
  // Get the instruction at Current PC
  fetchOK = core<uint32_t>.fetchInst(prevPC, inst);
  // Decode the instruction
  info = core<uint32_t>.decode(inst, op0, op1, op2, op3);
  return;
}

extern "C"
int issCompareInst (uint32_t spirit_pc_rdata, uint32_t spirit_inst) {

  bool fetchOK = true;
  std::string disass;

  fetchOK = core<uint32_t>.fetchInst(prevPC, inst);
  // Decode the instruction
  info = core<uint32_t>.decode(inst, op0, op1, op2, op3);

  if (prevPC != spirit_pc_rdata) {
      fprintf (stdout, "RTL PC: %-.8x\t ISS PC: %-.8x\n", spirit_pc_rdata, prevPC);
      fprintf (stdout, "PC Mismatch\n");
      return 0;
  }
  else {
    if (isCompressedInst(inst)) inst = inst&0xFFFF;
    if (inst != spirit_inst) {
      fprintf (stdout, "RTL INSTR: %-.8x\t ISS INSTR: %-.8x\n", spirit_inst, inst);
      fprintf (stdout, "Instruction Word Mismatch\n");
      return 0;
    }
  }
  core<uint32_t>.disassembleInst(inst, disass);
  fprintf(stdout, "%-.08x %-.08x\t%s\n", spirit_pc_rdata, spirit_inst, disass.c_str());
  return 1;
}

extern "C"
int issCompareR (uint32_t spirit_rd_addr, uint32_t spirit_rd_wdata,
                 uint32_t spirit_rs1_addr, uint32_t spirit_rs1_rdata,
                 uint32_t spirit_rs2_addr, uint32_t spirit_rs2_rdata) {

  // Compare RD register and the value if written/read
  if (info.ithOperandMode(0) != OperandMode::None) {
    if (!(compareRD (spirit_rd_addr, spirit_rd_wdata, op0)))
      return 0;
  }
  // Compare RS1 register and the value if written/read
  if (info.ithOperandMode(1) != OperandMode::None) {
    if (!(compareRS1 (spirit_rs1_addr, spirit_rs1_rdata, op1)))
      return 0;
  }
  // Compare RS2 register and the value if written/read
  if ((info.ithOperandMode(2) != OperandMode::None) &&
      (info.ithOperandType(2) != OperandType::Imm)) {
    if(!(compareRS2 (spirit_rs2_addr, spirit_rs2_rdata, op2)))
      return 0;
  }
  fprintf(stdout, "X%.02d: %-.08x\n\n", spirit_rd_addr, spirit_rd_wdata);
  return 1;
}

extern "C"
int issCompareI (uint32_t spirit_rd_addr, uint32_t spirit_rd_wdata,
                 uint32_t spirit_rs1_addr, uint32_t spirit_rs1_rdata) {

  // Compare RD register and the value if written/read
  if (info.ithOperandMode(0) != OperandMode::None) {
    if (!(compareRD (spirit_rd_addr, spirit_rd_wdata, op0)))
      return 0;
  }
  // Compare RS1 register and the value if written/read
  if (info.ithOperandMode(1) != OperandMode::None) {
    if (!(compareRS1 (spirit_rs1_addr, spirit_rs1_rdata, op1)))
      return 0;
  }
  fprintf(stdout, "X%.02d: %-.08x\n\n", spirit_rd_addr, spirit_rd_wdata);
  return 1;
}

extern "C"
int issCompareSB (uint32_t spirit_rs1_addr, uint32_t spirit_rs1_rdata,
                  uint32_t spirit_rs2_addr, uint32_t spirit_rs2_rdata) {

  // Compare RS1 register and the value if written/read
  if (info.ithOperandMode(1) != OperandMode::None) {
    if (!(compareRS1 (spirit_rs1_addr, spirit_rs1_rdata, op1)))
      return 0;
  }
  // Compare RS2 register and the value if written/read
  if ((info.ithOperandMode(2) != OperandMode::None) &&
      (info.ithOperandType(2) != OperandType::Imm)) {
    if (!(compareRS2 (spirit_rs2_addr, spirit_rs2_rdata, op2)))
      return 0;
  }
  fprintf(stdout, "\n");
  return 1;
}

extern "C"
int issCompareUJ (uint32_t spirit_rd_addr, uint32_t spirit_rd_wdata) {

  // Compare RD register and the value if written/read
  if (info.ithOperandMode(0) != OperandMode::None) {
    if (!(compareRD (spirit_rd_addr, spirit_rd_wdata, op0)))
      return 0;
  }
  fprintf(stdout, "X%.02d: %-.08x\n\n", spirit_rd_addr, spirit_rd_wdata);
  return 1;
}

// issExec() is called by spirit top testbench whenever an instruction
// retires. Use the spirit interface to get the current RTL state.
extern "C"
void issExec () {
  // singleStep updates the PC. Use prevPC for comparison/decode
  prevPC = core<uint32_t>.pc_;
  // Execute the ISS model for one cycle
  core<uint32_t>.singleStep();
  return;
}

extern "C"
int isRTypeInst () {
  return core<uint32_t>.isRType;
}

extern "C"
int isITypeInst () {
  return core<uint32_t>.isIType;
}

extern "C"
int isSTypeInst () {
  return core<uint32_t>.isSType;
}

extern "C"
int isBTypeInst () {
  return core<uint32_t>.isBType;
}

extern "C"
int isUTypeInst () {
  return core<uint32_t>.isUType;
}

extern "C"
int isJTypeInst () {
  return core<uint32_t>.isJType;
}

extern "C"
void issSetPC (uint32_t pc) {
  core<uint32_t>.pc_ = pc;
}

extern "C"
void issSetIntReg (uint32_t regNum, uint32_t value) {
  core<uint32_t>.intRegs_.write(regNum, value);
}

extern "C"
void issSetCstReg (uint32_t regNum, uint32_t value) {
  core<uint32_t>.cstRegs_.write(regNum, value);
}
