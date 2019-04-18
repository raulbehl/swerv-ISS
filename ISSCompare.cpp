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
#include "Server.hpp"
#include "Interactive.hpp"

typedef std::vector<std::string> StringVec;
using namespace WdRiscv;

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
static
bool ISSinit(bool filetype, std::string filename) {
  Args args;
  // Determine simulated memory size. Default to 4 gigs.
  // If running a 32-bit machine (pointer siz = 32 bits), try 2 gigs.
  size_t memorySize = size_t(1) << 32;  // 4 gigs
  if (memorySize == 0)
    memorySize = size_t(1) << 31;  // 2 gigs

  unsigned registerCount = 32;
  unsigned hartId = 0;

  WdRiscv::Memory memory(memorySize);
  WdRiscv::Core<URV> core(hartId, memory, registerCount);

  FILE* traceFile = nullptr;
  FILE* commandLog = nullptr;
  FILE* consoleOut = stdout;
  if (not openUserFiles(args, traceFile, commandLog, consoleOut))
    return false;

  core.setConsoleOutput(consoleOut);

  core.reset();

  if (filetype) {
    if (not core.loadHexFile(filename)) {
      std::cerr<<"ERROR: Couldn't load hex file"<<std::endl;
      return false;
    }
  }
  else {
    if (not loadElfFile(core, filename)) {
      std::cerr<<"ERROR: Couldn't load elf file"<<std::endl;
      return false;
    }
  }

  // Command line to-host overrides that of ELF and config file.
  if (args.hasToHost)
    core.setToHostAddress(args.toHost);

  // Command-line entry point overrides that of ELF.
  if (args.hasStartPc)
    core.pokePc(URV(args.startPc));

  // Command-line exit point overrides that of ELF.
  if (args.hasEndPc)
    core.setStopAddress(URV(args.endPc));

  // Command-line console io address overrides config file.
  if (args.hasConsoleIo)
    core.setConsoleIo(URV(args.consoleIo));

  // Set instruction count limit.
  core.setInstructionCountLimit(args.instCountLim);

  // Print load-instruction data-address when tracing instructions.
  core.setTraceLoad(args.traceLoad);

  core.enableTriggers(args.triggers);
  core.enableGdb(args.gdb);
  core.enablePerformanceCounters(args.counters);
  core.enableAbiNames(args.abiNames);
  core.enableNewlib(args.newlib);

  closeUserFiles(traceFile, commandLog, consoleOut);
}

int main (int argc, char* argv[])  {

  unsigned regWidth = 32;
  bool ok = true;

  try {
    if (regWidth == 32)
      ok = ISSinit<uint32_t>(argc, (std::string)argv[1]);
    else if (regWidth == 64)
      ok = ISSinit<uint64_t>(argc, (std::string)argv[1]);
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

