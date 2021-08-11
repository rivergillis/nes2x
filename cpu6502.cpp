#include "cpu6502.h"

#include <fstream>
#include <functional>

#include "common.h"
#include "mappers/nrom_mapper.h"
#include "mapper_id.h"
#include "ppu.h"
#include "memory_view.h"

namespace { 

#define VAL(a) memory_view_->Get(a)
#define DBGPAD(...) DBG("%-32s", string_format(__VA_ARGS__).c_str())
#define DBGPADSINGLE(x) DBG("       "); DBGPAD("%s", x);

// Used for nestest log goldens
void DBG(const char* str, ...) {
  #ifdef DEBUG
  va_list arglist;
  va_start(arglist, str);
  vprintf(str, arglist);
  va_end(arglist);
  #endif
}
// used for everything else
void VDBG(const char* str, ...) {
  #ifdef VDEBUG
  va_list arglist;
  va_start(arglist, str);
  vprintf(str, arglist);
  va_end(arglist);
  #endif
}

uint16_t StackAddr(uint8_t sp) {
  return ((0x01 << 8) | sp);
}

}

Cpu6502::Cpu6502(const std::string& file_path) {
  Reset(file_path);
  VDBG("NES ready. PC: %#04x\n", program_counter_);
}

// TODO: Rename to RunInstruction?
void Cpu6502::RunCycle() {
  std::string prev_flags = string_format("A:%02X X:%02X Y:%02X P:%02X SP:%02X",
    a_, x_, y_, p_, stack_pointer_);
  uint64_t prev_cycle = cycle_;
  uint8_t opcode = memory_view_->Get(program_counter_);
  DBG("%04X  %02X ", program_counter_, opcode);
  program_counter_++;
  instructions_.at(opcode).impl();
  DBG("%s PPU:  0,  0 CYC:%d\n", prev_flags.c_str(), prev_cycle);
}

void Cpu6502::Reset(const std::string& file_path) {
  LoadCartrtidgeFile(file_path);
  memory_view_ = std::make_unique<MemoryView>(internal_ram_, ppu_.get(), mapper_.get());
  assert(ppu_);
  assert(mapper_);
  assert(memory_view_);
  // DbgMem();

  // http://webcache.googleusercontent.com/search?q=cache:knntPlSpFnQJ:forums.nesdev.com/viewtopic.php%3Ff%3D3%26t%3D14231+&cd=4&hl=en&ct=clnk&gl=us
  // The following 7 cycles happens at reset:
  // 1. [READ] read (PC)
  // 2. [READ] read (PC)
  // 3. [READ] read (S), decrement S
  // 4. [READ] read (S), decrement S
  // 5. [READ] read (S), decrement S
  // 6. [WRITE] PC_low = ($FFFC), set interrupt flag
  // 7. [WRITE] PC_high = ($FFFD)

  // nestest should start at 0xC000 till I get input working
  // C000 is start of PRG_ROM's mirror (so 0x10 in .nes)
  if (file_path == "/Users/river/code/nes/roms/nestest.nes") {
    program_counter_ = 0xC000;
  } else {
    program_counter_ =  memory_view_->Get16(0xFFFC);
  }

  // not realistic -- programs should set these
  a_ = x_ = y_ = 0;

  p_ = 0x24;  // for nestest golden
  stack_pointer_ = 0xFD;
  cycle_ = 7;

  // TODO: Do the rest: https://wiki.nesdev.com/w/index.php?title=Init_code

  BuildInstructionSet();
}

void Cpu6502::LoadCartrtidgeFile(const std::string& file_path) {
  std::ifstream input(file_path, std::ios::in | std::ios::binary);
  std::vector<uint8_t> bytes(
         (std::istreambuf_iterator<char>(input)),
         (std::istreambuf_iterator<char>()));
  if (bytes.size() <= 0) {
    throw std::runtime_error("No file or empty file.");
  } else if (bytes.size() < 8) {
    throw std::runtime_error("Invalid file format.");
  }

  bool is_ines = false;
  if (bytes[0] == 'N' && bytes[1] == 'E' && bytes[2] == 'S' && bytes[3] == 0x1A) {
    is_ines = true;
  }

  bool is_nes2 = false;
  if (is_ines == true && (bytes[7]&0x0C)==0x08) {
    is_nes2 = true;
  }

  if (is_nes2) {
    VDBG("Found %d byte NES 2.0 file.\n", bytes.size());
    // TODO: Load NES 2.0 specific
    // Back-compat with ines 1.0
    LoadNes1File(bytes);
  } else if (is_ines) {
    VDBG("Found %d byte iNES 1.0 file.\n", bytes.size());
    LoadNes1File(bytes);
  } else {
    throw std::runtime_error("Rom file is not iNES format.");
  }
}

void Cpu6502::LoadNes1File(std::vector<uint8_t> bytes) {
  if (bytes.size() < 16) {
    throw std::runtime_error("Incomplete iNes header.");
  }

  uint32_t prg_rom_size = static_cast<uint32_t>(bytes[4]) * 0x4000;
  uint32_t chr_rom_size = static_cast<uint32_t>(bytes[5]) * 0x2000;

  if (bytes.size() < (prg_rom_size + chr_rom_size + 16)) {
    throw std::runtime_error("Rom file size less than header suggests.");
  }

  // TODO: Handle flags as needed.
  uint8_t flags6 = bytes[6];  // msb are lower nybble of mapper num
  if (flags6 & 0b0010'0000) {
    throw std::runtime_error("Rom has a trainer!");
  }
  uint8_t flags7 = bytes[7];  // lsb are upper nybble of mapper num
  uint8_t mapper_number = ((flags7 >> 4) << 4) | (flags6 >> 4);

  uint8_t prg_ram_size = bytes[8] == 0x0 ? static_cast<uint8_t>(0x2000) : bytes[8] * 0x2000;
  VDBG("Mapper ID %d PRG_ROM sz %d CHAR_ROM sz %d PRG_RAM sz %d\n",
      mapper_number, prg_rom_size, chr_rom_size, prg_ram_size);
  
  if (chr_rom_size > 0) {
    ppu_ = std::make_unique<Ppu>(bytes.data() + 16 + prg_rom_size, chr_rom_size);
  } else {
    ppu_ = std::make_unique<Ppu>(nullptr, 0);
  }

  mapper_ = std::make_unique<NromMapper>(bytes.data() + 16, prg_rom_size);
}

bool Cpu6502::GetFlag(Cpu6502::Flag flag) {
  return Bit(static_cast<uint8_t>(flag), p_) == 1;
}

void Cpu6502::SetFlag(Cpu6502::Flag flag, bool val) {
  if (val) {
    p_ |= (1 << static_cast<uint8_t>(flag));
  } else {
    p_ &= ~(1 << static_cast<uint8_t>(flag));
  }

}
uint8_t Cpu6502::NextImmediate() {
  uint8_t val = memory_view_->Get(program_counter_++);
  DBG("%02X     ", val);
  return val;
}
uint16_t Cpu6502::NextZeroPage() {
  uint16_t addr = memory_view_->Get(program_counter_++);
  DBG("%02X     ", static_cast<uint8_t>(addr));
  return addr;
}
uint16_t Cpu6502::NextZeroPageX() {
  uint16_t addr = memory_view_->Get(program_counter_++);
  DBG("%02X     ", static_cast<uint8_t>(addr));
  addr = (addr + x_) % 0xFF;  // Add X to LSB of ZP
  return addr;
}
uint16_t Cpu6502::NextZeroPageY() {
  uint16_t addr = memory_view_->Get(program_counter_++);
  DBG("%02X     ", static_cast<uint8_t>(addr));
  addr = (addr + y_) % 0xFF;  // Add Y to LSB of ZP
  return addr;
}
uint16_t Cpu6502::NextAbsolute() {
  uint16_t addr = memory_view_->Get16(program_counter_);
  DBG("%02X %02X  ", static_cast<uint8_t>(addr), static_cast<uint8_t>(addr >> 8)); // low first
  program_counter_ += 2;
  return addr;
}
uint16_t Cpu6502::NextAbsoluteX() {
  uint16_t addr = memory_view_->Get16(program_counter_);
  DBG("%02X %02X  ", static_cast<uint8_t>(addr), static_cast<uint8_t>(addr >> 8));
  program_counter_ += 2;
  return addr + x_;
}
uint16_t Cpu6502::NextAbsoluteY() {
  uint16_t addr = memory_view_->Get16(program_counter_);
  DBG("%02X %02X  ", static_cast<uint8_t>(addr), static_cast<uint8_t>(addr >> 8));
  program_counter_ += 2;
  return addr + y_;
}
uint16_t Cpu6502::NextIndirectX() {
  // Get ZP, add X_ to LSB, then read full addr
  uint16_t zero_addr = memory_view_->Get(program_counter_++);
  DBG("%02X     ", static_cast<uint8_t>(zero_addr));
  zero_addr = (zero_addr + x_) % 0xFF;
  return memory_view_->Get16(zero_addr);
}
uint16_t Cpu6502::NextIndirectY() {
  // get ZP addr, then read full addr from it and add Y
  uint16_t zero_addr = memory_view_->Get(program_counter_++);
  DBG("%02X     ", static_cast<uint8_t>(zero_addr));
  uint16_t addr = memory_view_->Get16(zero_addr);
  return addr + y_;
}
uint16_t Cpu6502::NextAbsoluteIndirect() {
  uint16_t indirect = memory_view_->Get16(program_counter_);
  DBG("%02X %02X  ", static_cast<uint8_t>(indirect), static_cast<uint8_t>(indirect >> 8));
  program_counter_ += 2;
  return memory_view_->Get16(indirect);
}

uint16_t Cpu6502::NextRelativeAddr() {
  uint8_t offset_uint = memory_view_->Get(program_counter_++);
  DBG("%02X     ", static_cast<uint8_t>(offset_uint));
  // https://stackoverflow.com/questions/14623266/why-cant-i-reinterpret-cast-uint-to-int
  int8_t tmp;
  std::memcpy(&tmp, &offset_uint, sizeof(tmp));
  const int8_t offset = tmp;

  return program_counter_ + offset;
}

void Cpu6502::PushStack(uint8_t val) {
  memory_view_->Set(StackAddr(stack_pointer_--), val);
}
void Cpu6502::PushStack16(uint16_t val) {
  // Store LSB then MSB
  PushStack(static_cast<uint8_t>(val));
  PushStack(val >> 8);
}
uint8_t Cpu6502::PopStack() {
  return memory_view_->Get(StackAddr(++stack_pointer_));
}
uint16_t Cpu6502::PopStack16() {
  // Value was stored little-endian in top-down stack, so get MSB then LSB
  return (PopStack() << 8) | PopStack();
}

void Cpu6502::DbgMem() {
  for (int i = 0x6000; i <= 0xFFFF; i += 0x10) {
    DBG("\nCPU[%03X]: ", i);
    for (int j = 0; j < 0x10; j++) {
      DBG("%#04x ", mapper_->Get(i + j));
    }
  }
  DBG("\n");
}

void Cpu6502::DbgStack() {
  for (int i = 0x0100; i <= 0x01FF; i += 0x10) {
    DBG("\nCPU[%03X]: ", i);
    for (int j = 0; j < 0x10; j++) {
      DBG("%#04x ", memory_view_->Get(i + j));
    }
  }
  DBG("\n");
}

std::string Cpu6502::Status() {
  return string_format("[PC: %#06x, A: %#04x, X: %#04x, Y: %#04x, P: %#04x, SP: %#04x]",
    program_counter_, a_, x_, y_, p_, stack_pointer_);
}

void Cpu6502::ADC(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint8_t val = addrval.val;
  DBGPAD("ADC %s", AddrValString(addrval, mode).c_str());
  uint16_t new_a = a_ + val + GetFlag(Flag::C);
  SetFlag(Flag::C, new_a > 0xFF);
  SetFlag(Flag::V, Pos(a_) && Pos(val) && !Pos(new_a));
  SetFlag(Flag::Z, new_a == 0);
  SetFlag(Flag::N, !Pos(new_a));
  a_ = new_a;
}

void Cpu6502::JMP(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint16_t addr = addrval.addr;
  DBGPAD("JMP %s", AddrValString(addrval, mode).c_str());
  program_counter_ = addr;
}

void Cpu6502::BRK(AddressingMode mode) {
  PushStack16(program_counter_);  // PC is already +1 from reading instr.
  PushStack(p_ | 0b0011'0000);  // B=0b11
  program_counter_ = memory_view_->Get16(0xFFFE);
  SetFlag(Flag::I, true);
  DBGPADSINGLE("BRK");
}

void Cpu6502::RTI(AddressingMode mode) {
  p_ = (PopStack() & 0b1100'1111);  // clear B
  program_counter_ = PopStack16();
  DBGPADSINGLE("RTI");
}

void Cpu6502::LDX(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint8_t val = addrval.val;
  DBGPAD("LDX %s", AddrValString(addrval, mode).c_str());
  SetFlag(Flag::Z, val == 0);
  SetFlag(Flag::N, !Pos(val));
  x_ = val;
}

void Cpu6502::STX(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint16_t addr = addrval.addr;
  DBGPAD("STX %s", AddrValString(addrval, mode).c_str());
  memory_view_->Set(addr, x_);
}

void Cpu6502::JSR(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint16_t new_pc = addrval.addr;
  DBGPAD("JSR %s", AddrValString(addrval, mode).c_str());
  PushStack16(program_counter_);
  program_counter_ = new_pc;
}

void Cpu6502::SEC(AddressingMode mode) {
  SetFlag(Flag::C, true);
  DBGPADSINGLE("SEC");
}

void Cpu6502::BCS(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint16_t addr = addrval.addr;
  DBGPAD("BCS %s", AddrValString(addrval, mode).c_str());
  if (GetFlag(Flag::C)) {
    program_counter_ = addr;
  }
}

void Cpu6502::CLC(AddressingMode mode) {
  SetFlag(Flag::C, false);
  DBGPADSINGLE("CLC");
}

void Cpu6502::BCC(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint16_t addr = addrval.addr;
  DBGPAD("BCC %s", AddrValString(addrval, mode).c_str());
  if (!GetFlag(Flag::C)) {
    program_counter_ = addr;
  }
}

void Cpu6502::LDA(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint8_t val = addrval.val;
  DBGPAD("LDA %s", AddrValString(addrval, mode).c_str());
  SetFlag(Flag::Z, val == 0);
  SetFlag(Flag::N, !Pos(val));
  a_ = val;
}

void Cpu6502::BEQ(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint16_t addr = addrval.addr;
  DBGPAD("BEQ %s", AddrValString(addrval, mode).c_str());
  if (GetFlag(Flag::Z)) {
    program_counter_ = addr;
  }
}

void Cpu6502::BNE(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint16_t addr = addrval.addr;
  DBGPAD("BNE %s", AddrValString(addrval, mode).c_str());
  if (!GetFlag(Flag::Z)) {
    program_counter_ = addr;
  }
}

void Cpu6502::STA(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint16_t addr = addrval.addr;
  DBGPAD("STA %s", AddrValString(addrval, mode).c_str());
  memory_view_->Set(addr, a_);
}

void Cpu6502::BIT(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint8_t val = addrval.val;
  DBGPAD("BIT %s", AddrValString(addrval, mode).c_str());
  uint8_t res = val & a_;
  SetFlag(Flag::Z, res == 0);
  SetFlag(Flag::V, Bit(6, val) == 1);
  SetFlag(Flag::N, Bit(7, val) == 1);
}

void Cpu6502::BVS(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint16_t addr = addrval.addr;
  DBGPAD("BVS %s", AddrValString(addrval, mode).c_str());
  if (GetFlag(Flag::V)) {
    program_counter_ = addr;
  }
}

void Cpu6502::BVC(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint16_t addr = addrval.addr;
  DBGPAD("BVC %s", AddrValString(addrval, mode).c_str());
  if (!GetFlag(Flag::V)) {
    program_counter_ = addr;
  }
}

void Cpu6502::BPL(AddressingMode mode) {
  AddrVal addrval = NextAddrVal(mode);
  uint16_t addr = addrval.addr;
  DBGPAD("BPL %s", AddrValString(addrval, mode).c_str());
  if (!GetFlag(Flag::N)) {
    program_counter_ = addr;
  }
}

void Cpu6502::RTS(AddressingMode mode) {
  program_counter_ = PopStack16();
  DBGPADSINGLE("RTS");
}

void Cpu6502::NOP(AddressingMode mode) {
  DBGPADSINGLE("NOP");
 }

uint16_t Cpu6502::NextAddr(AddressingMode mode) {
  switch (mode) {
    case AddressingMode::kZeroPage:
      return NextZeroPage();
    case AddressingMode::kZeroPageX:
      return NextZeroPageX();
    case AddressingMode::kZeroPageY:
      return NextZeroPageY();
    case AddressingMode::kAbsolute:
      return NextAbsolute();
    case AddressingMode::kAbsoluteX:
      return NextAbsoluteX();
    case AddressingMode::kAbsoluteY:
      return NextAbsoluteY();
    case AddressingMode::kIndirectX:
      return NextIndirectX();
    case AddressingMode::kIndirectY:
      return NextIndirectY();
    case AddressingMode::kAbsoluteIndirect:
      return NextAbsoluteIndirect();
    case AddressingMode::kRelative:
      return NextRelativeAddr();
    default:
      throw std::runtime_error("Undefined addressing mode in NextAddr.");
  }
}

Cpu6502::AddrVal Cpu6502::NextAddrVal(AddressingMode mode) {
  if (mode == AddressingMode::kImmediate) {
    return {0, NextImmediate()};
  }
  AddrVal addrval = {};
  addrval.addr = NextAddr(mode);
  addrval.val = VAL(addrval.addr);
  return addrval;
}

std::string Cpu6502::AddrValString(AddrVal addrval, AddressingMode mode)  {
  switch (mode) {
    case AddressingMode::kImmediate:
      return string_format("#$%02X", addrval.val);
    case AddressingMode::kZeroPage:
      return string_format("$%02X = %02X", addrval.addr, addrval.val);
    case AddressingMode::kZeroPageX: {
      uint8_t target = memory_view_->Get(program_counter_ - 1);
      return string_format("$%02X,X @ %02X = %02X", target, addrval.addr, addrval.val);
    }
    case AddressingMode::kZeroPageY: {
      uint8_t target = memory_view_->Get(program_counter_ - 1);
      return string_format("$%02X,Y @ %02X = %02X", target, addrval.addr, addrval.val);
    }
    case AddressingMode::kAbsolute:
      return string_format("$%04X", addrval.addr);
    case AddressingMode::kAbsoluteX: {
      uint16_t target = memory_view_->Get16(program_counter_ - 2);
      return string_format("$%04X,X @ %04X = %02X", target, addrval.addr, addrval.val);
    }
    case AddressingMode::kAbsoluteY: {
      uint16_t target = memory_view_->Get16(program_counter_ - 2);
      return string_format("$%04X,Y @ %04X = %02X", target, addrval.addr, addrval.val);
    }
    case AddressingMode::kIndirectX: {
      uint8_t target = memory_view_->Get(program_counter_ - 1);
      return string_format("($%02X,X) @ %02X = %04X = %02X", target, target + x_, addrval.addr, addrval.val);
    }
    case AddressingMode::kIndirectY: {
      uint8_t target = memory_view_->Get(program_counter_ - 1);
      return string_format("($%02X),Y @ %02X = %04X = %02X", target, addrval.addr - y_, addrval.addr, addrval.val);
    }
    case AddressingMode::kAbsoluteIndirect: {
      uint16_t target = memory_view_->Get16(program_counter_ - 2);
      return string_format("($%04X) = %04X", target, addrval.addr);
    }
    case AddressingMode::kRelative:
      return string_format("$%04X", addrval.addr);
    case AddressingMode::kNone:
      return "";
  }
  // TODO: Handle the rest of the addr modes.
}

void Cpu6502::BuildInstructionSet() {
  #define ADD_INSTR(op, name, mode) instructions_[op] = {#name, [this]() { name(mode); }};
  ADD_INSTR(0x69, ADC, AddressingMode::kImmediate);
  ADD_INSTR(0x65, ADC, AddressingMode::kZeroPage);
  ADD_INSTR(0x75, ADC, AddressingMode::kZeroPageX);
  ADD_INSTR(0x6D, ADC, AddressingMode::kAbsolute);
  ADD_INSTR(0x7D, ADC, AddressingMode::kAbsoluteX);
  ADD_INSTR(0x79, ADC, AddressingMode::kAbsoluteY);
  ADD_INSTR(0x61, ADC, AddressingMode::kIndirectX);
  ADD_INSTR(0x71, ADC, AddressingMode::kIndirectY);
  ADD_INSTR(0x4C, JMP, AddressingMode::kAbsolute);
  ADD_INSTR(0x6C, JMP, AddressingMode::kAbsoluteIndirect);
  ADD_INSTR(0x00, BRK, AddressingMode::kNone);
  ADD_INSTR(0x40, BRK, AddressingMode::kNone);
  ADD_INSTR(0xA2, LDX, AddressingMode::kImmediate);
  ADD_INSTR(0xA6, LDX, AddressingMode::kZeroPage);
  ADD_INSTR(0xB6, LDX, AddressingMode::kZeroPageY);
  ADD_INSTR(0xAE, LDX, AddressingMode::kAbsolute);
  ADD_INSTR(0xBE, LDX, AddressingMode::kAbsoluteY);
  ADD_INSTR(0x86, STX, AddressingMode::kZeroPage);
  ADD_INSTR(0x96, STX, AddressingMode::kZeroPageY);
  ADD_INSTR(0x8E, STX, AddressingMode::kAbsolute);
  ADD_INSTR(0x20, JSR, AddressingMode::kAbsolute);
  ADD_INSTR(0xEA, NOP, AddressingMode::kNone);
  ADD_INSTR(0x38, SEC, AddressingMode::kNone);
  ADD_INSTR(0xB0, BCS, AddressingMode::kRelative);
  ADD_INSTR(0x18, CLC, AddressingMode::kNone);
  ADD_INSTR(0x90, BCC, AddressingMode::kRelative);
  ADD_INSTR(0xA9, LDA, AddressingMode::kImmediate);
  ADD_INSTR(0xA5, LDA, AddressingMode::kZeroPage);
  ADD_INSTR(0xB5, LDA, AddressingMode::kZeroPageX);
  ADD_INSTR(0xAD, LDA, AddressingMode::kAbsolute);
  ADD_INSTR(0xBD, LDA, AddressingMode::kAbsoluteX);
  ADD_INSTR(0xB9, LDA, AddressingMode::kAbsoluteY);
  ADD_INSTR(0xA1, LDA, AddressingMode::kIndirectX);
  ADD_INSTR(0xB1, LDA, AddressingMode::kIndirectY);
  ADD_INSTR(0xF0, BEQ, AddressingMode::kRelative);
  ADD_INSTR(0xD0, BNE, AddressingMode::kRelative);
  ADD_INSTR(0x85, STA, AddressingMode::kZeroPage);
  ADD_INSTR(0x95, STA, AddressingMode::kZeroPageX);
  ADD_INSTR(0x8D, STA, AddressingMode::kAbsolute);
  ADD_INSTR(0x9D, STA, AddressingMode::kAbsoluteX);
  ADD_INSTR(0x99, STA, AddressingMode::kAbsoluteY);
  ADD_INSTR(0x81, STA, AddressingMode::kIndirectX);
  ADD_INSTR(0x91, STA, AddressingMode::kIndirectY);
  ADD_INSTR(0x24, BIT, AddressingMode::kZeroPage);
  ADD_INSTR(0x2C, BIT, AddressingMode::kAbsolute);
  ADD_INSTR(0x70, BVS, AddressingMode::kRelative);
  ADD_INSTR(0x50, BVC, AddressingMode::kRelative);
  ADD_INSTR(0x10, BPL, AddressingMode::kRelative);
  ADD_INSTR(0x60, RTS, AddressingMode::kNone);

  VDBG("Instruction set built.\n");
}
