#include <iostream>
#include <string>

#include "cpu6502.h"
#include "common.h"

// doc: https://www.qmtpro.com/~nes/misc/nestest.txt
// good log: https://www.qmtpro.com/~nes/misc/nestest.log
// start execution at $C000
const std::string kTestRomPath = "/Users/river/code/nes/roms/nestest.nes";
const uint64_t kDefaultNumCycles = 8991; // nestest

void Run(const std::string& rom_path, uint64_t num_cycles) {
  Cpu6502 cpu(rom_path);
      #ifdef DEBUG
      auto start_time = Clock::now();
      #endif
  for (int i = 0; i < num_cycles; i++) {
    cpu.RunCycle();
  }
  DBG( "Executed %llu instructions in %s\n", num_cycles, StringMsSince(start_time).c_str());
}

std::string GetFileName(int argc, char* argv[]) {
  if (argc < 2) {
    throw std::runtime_error("Usage: nes2x path/to/file [optional: num_cycles]");
  }
  return std::string(argv[1]);
}

uint64_t GetNumCycles(int argc, char* argv[]) {
  if (argc < 3) {
    return kDefaultNumCycles;
  }
  return std::stoull(argv[2]);
}

int main(int argc, char* argv[]) {
  try {
    Run(GetFileName(argc, argv), GetNumCycles(argc, argv));
    DBG("Exit main() success\n");
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  }
}
