#include "cpu6502.h"

#include <fstream>

#include "common.h"
#include "mappers/nrom_mapper.h"
#include "mapper_id.h"
#include "ppu.h"
#include "memory_view.h"

namespace { 

void DBG(const char* str, ...) {
  #ifdef DEBUG
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
  DBG("NES ready. PC: %#04x\n", program_counter_);
}

void Cpu6502::RunCycle() {
  uint8_t opcode = memory_view_->Get(program_counter_);
  DBG("%s -- executing opcode %#04x\n", Status().c_str(), opcode);
  program_counter_++;
  // TODO: Order these somehow
  switch (opcode) {
    case 0x69:
    case 0x65:
    case 0x75:
    case 0x6D:
    case 0x7D:
    case 0x79:
    case 0x61:
    case 0x71:
      ADC(opcode);
      break;
    case 0x4C:
    case 0x6C:
      JMP(opcode);
      break;
    case 0x00:
      BRK();
      break;
    case 0x40:
      RTI();
      break;
    default:
      DBG("OP %#04x.... ", opcode);
      throw std::runtime_error("Unimplemented opcode.");
  }
}

void Cpu6502::Reset(const std::string& file_path) {
  LoadCartrtidgeFile(file_path);
  memory_view_ = std::make_unique<MemoryView>(internal_ram_, ppu_.get(), mapper_.get());
  assert(ppu_);
  assert(mapper_);
  assert(memory_view_);
  // DbgMem();

  // nestest should start at 0xC000 till I get indput working
  // C000 is start of PRG_ROM's mirror (so 0x10 in .nes)
  if (file_path == "/Users/river/code/nes/roms/nestest.nes") {
    program_counter_ = 0xC000;
  } else {
    program_counter_ =  memory_view_->Get16(0xFFFC);
  }

  // not realistic -- programs should set these
  a_ = x_ = y_ = p_ = 0;
  stack_pointer_ = 0xFF;
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
    std::cout << "Found " << bytes.size() << " byte NES 2.0 file." << std::endl;
    // TODO: Load NES 2.0 specific
    // Back-compat with ines 1.0
    LoadNes1File(bytes);
  } else if (is_ines) {
    std::cout << "Found " << bytes.size() << " byte iNES 1.0 file." << std::endl;
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
  DBG("Mapper ID %d PRG_ROM sz %d CHAR_ROM sz %d PRG_RAM sz %d\n",
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
  return memory_view_->Get(program_counter_++);
}
uint8_t Cpu6502::NextZeroPage() {
  uint16_t addr = memory_view_->Get(program_counter_++);
  return memory_view_->Get(addr);
}
uint8_t Cpu6502::NextZeroPageX() {
  uint16_t addr = memory_view_->Get(program_counter_++);
  addr = (addr + x_) % 0xFF;  // Add X to LSB of ZP
  return memory_view_->Get(addr);
}
uint8_t Cpu6502::NextZeroPageY() {
  uint16_t addr = memory_view_->Get(program_counter_++);
  addr = (addr + y_) % 0xFF;  // Add Y to LSB of ZP
  return memory_view_->Get(addr);
}
uint16_t Cpu6502::NextAbsoluteAddress() {
  uint16_t addr = memory_view_->Get16(program_counter_);
  program_counter_ += 2;
  return addr;
}
uint8_t Cpu6502::NextAbsoluteValue() {
  return memory_view_->Get(NextAbsoluteAddress());
}
uint8_t Cpu6502::NextAbsoluteX() {
  uint16_t addr = memory_view_->Get16(program_counter_);
  program_counter_ += 2;
  addr += x_;
  return memory_view_->Get(addr);
}
uint8_t Cpu6502::NextAbsoluteY() {
  uint16_t addr = memory_view_->Get16(program_counter_);
  program_counter_ += 2;
  addr += y_;
  return memory_view_->Get(addr);
}
uint8_t Cpu6502::NextIndirectX() {
  // Get ZP, add X_ to LSB, then read full addr
  uint16_t zero_addr = memory_view_->Get(program_counter_++);
  zero_addr = (zero_addr + x_) % 0xFF;
  uint16_t addr = memory_view_->Get16(zero_addr);
  return memory_view_->Get(addr);
}
uint8_t Cpu6502::NextIndirectY() {
  // get ZP addr, then read full addr from it and add Y
  uint16_t zero_addr = memory_view_->Get(program_counter_++);
  uint16_t addr = memory_view_->Get16(zero_addr);
  addr += y_;
  return memory_view_->Get(addr);
}
uint16_t Cpu6502::NextAbsoluteIndirect() {
  uint16_t indirect = memory_view_->Get16(program_counter_);
  program_counter_ += 2;
  return memory_view_->Get16(indirect);
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

void Cpu6502::ADC(uint8_t op) {
  uint8_t val = 0;

  if (op == 0x61) {
    val = NextIndirectX();
  } if (op == 0x65) {
    val = NextZeroPage();
  } else if (op == 0x69) {
    val = NextImmediate();
  } else if (op == 0x6D) {
    val = NextAbsoluteValue();
  } else if (op == 0x71) {
    val = NextIndirectY();
  } else if (op == 0x75) {
    val = NextZeroPageX();
  } else if (op == 0x79) {
    val = NextAbsoluteY();
  } else if (op == 0x7D) {
    val = NextAbsoluteX();
  } else {
    throw std::runtime_error("Bad opcode on ADC");
  }

  uint16_t new_a = a_ + val + GetFlag(Flag::C);
  SetFlag(Flag::C, new_a > 0xFF);
  SetFlag(Flag::V, Pos(a_) && Pos(val) && !Pos(new_a));
  SetFlag(Flag::Z, new_a == 0);
  SetFlag(Flag::N, !Pos(new_a));
  a_ = new_a;
}

void Cpu6502::JMP(uint8_t op) {
  uint16_t val = 0;
  if (op == 0x4c) {
    val = NextAbsoluteAddress();
  } else if (op == 0x6c) {
    val = NextAbsoluteIndirect();
  } else {
    throw std::runtime_error("Bad opcode on JMP");
  }
  DBG("JMP to %#06x\n", val);
  program_counter_ = val;
}

void Cpu6502::BRK() {
  PushStack16(program_counter_);  // PC is already +1 from reading instr.
  PushStack(p_ | 0b0011'0000);  // B=0b11
  program_counter_ = memory_view_->Get16(0xFFFE);
  SetFlag(Flag::I, true);
  DBG("BRK to %#06x\n", program_counter_);
}

void Cpu6502::RTI() {
  p_ = (PopStack() & 0b1100'1111);  // clear B
  program_counter_ = PopStack16();
  DBG("RTI to %#06x\n", program_counter_);
}