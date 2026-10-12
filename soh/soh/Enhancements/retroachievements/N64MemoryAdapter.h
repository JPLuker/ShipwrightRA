#pragma once

#include <cstdint>

namespace ShipwrightRA {

// Presents selected Shipwright runtime state in the same address/byte layout
// exposed by the supported N64 cores that the existing RetroAchievements set
// was authored against.
class N64MemoryAdapter final {
  public:
    // RetroAchievements addresses are offsets into the exposed 8 MiB RDRAM
    // region, not KSEG0 virtual addresses (0x80000000+).
    static constexpr uint32_t kRdramSize = 0x00800000;

    // OoT NTSC-U 1.0 gSaveContext physical RDRAM offset. The original virtual
    // address is 0x8011A5D0.
    static constexpr uint32_t kSaveContextBase = 0x0011A5D0;
    static constexpr uint32_t kSaveContextBootstrapSize = 0x40;

    // Fills buffer using the raw host-native N64 RDRAM layout expected by
    // existing RA N64 sets. Returns the number of bytes successfully read.
    static uint32_t Read(uint32_t address, uint8_t* buffer, uint32_t numBytes);
};

} // namespace ShipwrightRA
