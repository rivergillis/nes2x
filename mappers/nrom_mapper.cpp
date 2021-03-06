#include "mappers/nrom_mapper.h"

#include "mapper_id.h"

NromMapper::NromMapper(uint8_t* cpu_ram, Ppu* ppu, uint8_t* apu_ram_, uint8_t* prg_rom, size_t prg_rom_size)
      : Mapper(cpu_ram, ppu, apu_ram_, prg_rom, prg_rom_size) {
  assert(prg_rom_size_ == 0x4000 || prg_rom_size_ == 0x8000);
  DBG("Created NROM mapper with %llu byte PRG_ROM and 8k PRG_RAM\n", static_cast<uint64_t>(prg_rom_size_));
}

uint8_t NromMapper::Get(uint16_t addr) {
  if (addr < 0x2000) {
    return cpu_ram_[addr % 0x800];
  } else if (addr < 0x4000) {
    uint16_t ppu_addr = 0x2000 + (addr % 8);  // get mirror of $2000-$2007
    switch (ppu_addr) {
      case 0x2000:  // PPUCTRL
        return ppu_->GetLatch();
      case 0x2001:  // PPUMASK
        return ppu_->GetLatch();
      case 0x2002:  // PPUSTATUS
        return ppu_->GetSTATUS();
      case 0x2003:  // OAMADDR
        return ppu_->GetLatch();
      case 0x2004:  // OAMDATA
        return ppu_->GetOAMDATA();
      case 0x2005:  // PPUSCROLL
        return ppu_->GetLatch();
      case 0x2006:  // PPUADDR
        return ppu_->GetLatch();
      case 0x2007:  // PPUDATA
        return ppu_->GetLatch();
        break;
      default:
        throw std::runtime_error("Invalid CPU->PPU addr -- default.");
    }
  } else if (addr <= 0x4020) {
    return apu_ram_[addr % 0x4000];
  } else if (addr < 0x6000) {
    throw std::runtime_error("Invalid read addr");  // Battery Backed Save or Work RAM
  } else if (addr < 0x8000) {
    // I think providing 8k ram here is wrong, but it should be fine...
    return prg_ram_[addr - 0x6000];
  } else if (addr < 0xC000) {
    return prg_rom_[addr - 0x8000];
  } else {
    if (prg_rom_size_ == 0x4000) {
      // Provide a mirror for NROM-128
      return prg_rom_[addr - 0xC000];
    } else {
      return prg_rom_[addr - 0x8000];
    }
  }
}

uint16_t NromMapper::Set(uint16_t addr, uint8_t val, uint64_t current_cycle) {
  if (addr < 0x2000) {
    cpu_ram_[addr % 0x800] = val;
  } else if (addr < 0x4000) {
    uint16_t ppu_addr = 0x2000 + (addr % 8);  // get mirror of $2000-$2007
    switch (ppu_addr) {
      case 0x2000:  // PPUCTRL
        ppu_->SetCTRL(val);
        break;
      case 0x2001:  // PPUMASK
        ppu_->SetMASK(val);
        break;
      case 0x2002:  // PPUSTATUS
        ppu_->SetLatch(val);
        break;
      case 0x2003:  // OAMADDR
        ppu_->SetOAMADDR(val);
        break;
      case 0x2004:  // OAMDATA
        ppu_->SetOAMDATA(val);
        break;
      case 0x2005:  // PPUSCROLL
        ppu_->SetPPUSCROLL(val);
        break;
      case 0x2006:  // PPUADDR
        ppu_->SetPPUADDR(val);
        break;
      case 0x2007:  // PPUDATA
        ppu_->SetPPUDATA(val);
        break;
      default:
        throw std::runtime_error("Invalid PPU addr -- default.");
    }
  } else if (addr == 0x4014) {  // OAMDMA
    // TODO: Make this easier somehow. -> GetDataPointer() then just read/write there?
    uint16_t new_addr = static_cast<uint16_t>(val) << 8;
    uint8_t* data = nullptr;
    {
      if (new_addr < 0x2000) {
        data = cpu_ram_ + (new_addr % 0x800);
      } else if (new_addr >= 0x2000 && new_addr < 0x8000) {
        throw std::runtime_error("Invalid read addr for DMA");
      } else if (new_addr < 0x8000) {
        // I think providing 8k ram here is wrong, but it should be fine...
        data = prg_ram_ + (new_addr - 0x6000);
      } else if (new_addr < 0xC000) {
        data = prg_rom_ + (new_addr - 0x8000);
      } else {
        if (prg_rom_size_ == 0x4000) {
          // Provide a mirror for NROM-128
          data = prg_rom_ + (new_addr - 0xC000);
        } else {
          data = prg_rom_ + (new_addr - 0x8000);
        }
      }
    }
    ppu_->SetOAMDMA(data);
    return 513 + (current_cycle % 2 == 0 ? 0 : 1);
  } else if (addr <= 0x4020) {
    apu_ram_[addr % 0x4000] = val;
  } else if (addr < 0x6000 || addr >= 0x8000) {
    throw std::runtime_error("Invalid write addr");
  } 
  prg_ram_[addr - 0x6000] = val;
  return 0;
}