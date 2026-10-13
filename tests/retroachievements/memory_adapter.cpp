#include "N64MemoryAdapter.h"
#include "functions.h"
#include "variables.h"

#include <array>
#include <cstdlib>
#include <iostream>

extern "C" {
SaveContext gSaveContext{};
PlayState *gPlayState = nullptr;
static u16 musicSequence;
u16 func_800FA0B4(u8) { return musicSequence; }
}

static void Check(bool condition, const char *label) {
  if (!condition) {
    std::cerr << "FAILED: " << label << '\n';
    std::exit(1);
  }
}

static uint32_t ReadLE(uint32_t address, uint32_t width) {
  std::array<uint8_t, 4> bytes{};
  Check(ShipwrightRA::N64MemoryAdapter::Read(address, bytes.data(), width) ==
            width,
        "mapped read length");
  uint32_t value = 0;
  for (uint32_t i = 0; i < width; ++i) {
    value |= uint32_t(bytes[i]) << (8 * i);
  }
  return value;
}

int main() {
  using ShipwrightRA::N64MemoryAdapter;
  constexpr uint32_t base = N64MemoryAdapter::kSaveContextBase;
  gSaveContext.health = 0x1234;
  gSaveContext.rupees = 0x2345;
  gSaveContext.entranceIndex = 0x12345678;
  Check(ReadLE(0x11A602, 2) == 0x1234, "health Mupen u16");
  Check(ReadLE(base + 0x34 + 2, 2) == 0x2345, "rupees Mupen u16");
  Check(ReadLE(base, 4) == 0x12345678, "entrance Mupen u32");
  Check(ReadLE((base + 0x30) ^ 3, 1) == 0x12, "health high byte");
  Check(ReadLE((base + 0x31) ^ 3, 1) == 0x34, "health low byte");

  // Native equips have D-pad extensions. They must not shift inventory.
  for (size_t i = 0; i < sizeof(gSaveContext.equips.buttonItems); ++i) {
    gSaveContext.equips.buttonItems[i] = static_cast<uint8_t>(0xA0 + i);
  }
  gSaveContext.equips.equipment = 0x5678;
  gSaveContext.inventory.items[0] = 0x5A;
  gSaveContext.inventory.items[23] = 0x3B;
  gSaveContext.inventory.ammo[0] = -1;
  gSaveContext.inventory.upgrades = 0x1234ABCD;
  gSaveContext.inventory.questItems = 0x89ABCDEF;
  gSaveContext.inventory.dungeonKeys[18] = -1;
  for (uint32_t i = 0; i < 4; ++i) {
    Check(ReadLE((base + 0x68 + i) ^ 3, 1) == 0xA0 + i, "canonical B/C equips");
  }
  Check(ReadLE((base + 0x6F) ^ 3, 1) == 0, "equip padding, not D-pad data");
  Check(ReadLE(base + 0x70 + 2, 2) == 0x5678,
        "equipment after native extension");
  Check(ReadLE((base + 0x74) ^ 3, 1) == 0x5A, "first inventory item");
  Check(ReadLE((base + 0x8B) ^ 3, 1) == 0x3B, "last inventory item");
  Check(ReadLE((base + 0x8C) ^ 3, 1) == 0xFF, "signed ammo byte");
  Check(ReadLE(base + 0xA0, 4) == 0x1234ABCD, "upgrades offset");
  Check(ReadLE(base + 0xA4, 4) == 0x89ABCDEF, "quest flags offset");
  Check(ReadLE((base + 0xCE) ^ 3, 1) == 0xFF, "signed dungeon key byte");
  gSaveContext.eventChkInf[0] = 0x1234;
  gSaveContext.eventChkInf[13] = 0xABCD;
  gSaveContext.itemGetInf[0] = 0x5678;
  gSaveContext.infTable[0] = 0x9ABC;
  Check(ReadLE(base + 0xED4 + 2, 2) == 0x1234, "first event flag");
  Check(ReadLE((base + 0xED4 + 26) ^ 2, 2) == 0xABCD, "last event flag");
  Check(ReadLE(base + 0xEF0 + 2, 2) == 0x5678, "item-get flags");
  Check(ReadLE(base + 0xEF8 + 2, 2) == 0x9ABC, "info flags");

  std::array<uint8_t, 4> buffer{0xCC, 0xCC, 0xCC, 0xCC};
  Check(N64MemoryAdapter::Read(0x1C8546, buffer.data(), 1) == 0,
        "no stale scene when no PlayState");
  static PlayState play{};
  gPlayState = &play;
  play.sceneNum = 0x45;
  play.msgCtx.textId = 0x1234;
  musicSequence = 0x5678;
  Check(ReadLE(0x1C8546, 1) == 0x45, "live scene");
  Check(ReadLE(0x1D8872, 2) == 0x1234, "live dialog");
  Check(ReadLE(0x124E54, 2) == 0x5678, "live BGM");
  Check(N64MemoryAdapter::Read(0, buffer.data(), 4) == 0,
        "unknown memory fails closed");
  Check(buffer[0] == 0xCC, "unknown memory does not invent data");
  Check(N64MemoryAdapter::Read(N64MemoryAdapter::kRdramSize, buffer.data(),
                               1) == 0,
        "out of range");
  Check(N64MemoryAdapter::Read(0xFFFFFFFF, buffer.data(), 4) == 0,
        "overflow address");
  Check(N64MemoryAdapter::Read(base, nullptr, 4) == 0, "null buffer");
  Check(N64MemoryAdapter::Read(base, buffer.data(), 0) == 0, "empty read");
  Check(N64MemoryAdapter::Read(base + 0xD2, buffer.data(), 4) == 2,
        "mapped-to-unmapped boundary");
  gSaveContext.health = 0x56;
  Check(ReadLE(0x11A602, 2) == 0x56, "next snapshot uses fresh state");
  std::cout << "Native N64 memory adapter tests passed\n";
}
