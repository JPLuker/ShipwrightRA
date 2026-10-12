#include "N64MemoryAdapter.h"

#include <array>
#include <cstddef>
#include <cstdint>

extern "C" {
#include "functions.h"
#include "sequence.h"
#include "variables.h"
}

namespace ShipwrightRA {
namespace {

using SavePrefix = std::array<uint8_t, N64MemoryAdapter::kSaveContextBootstrapSize>;

constexpr uint32_t kPlayStateSceneNumAddress = 0x001C8544;
constexpr uint32_t kMessageTextIdAddress = 0x001D8870;
constexpr uint32_t kMainBgmSequenceAddress = 0x00124E56;

void WriteBE16(SavePrefix& output, size_t offset, uint16_t value) {
    output[offset] = static_cast<uint8_t>((value >> 8) & 0xFF);
    output[offset + 1] = static_cast<uint8_t>(value & 0xFF);
}

void WriteBE32(SavePrefix& output, size_t offset, uint32_t value) {
    output[offset] = static_cast<uint8_t>((value >> 24) & 0xFF);
    output[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    output[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    output[offset + 3] = static_cast<uint8_t>(value & 0xFF);
}

bool ReadBE16Byte(uint32_t address, uint32_t fieldAddress, uint16_t value, uint8_t& output) {
    if (address == fieldAddress) {
        output = static_cast<uint8_t>((value >> 8) & 0xFF);
        return true;
    }
    if (address == fieldAddress + 1) {
        output = static_cast<uint8_t>(value & 0xFF);
        return true;
    }
    return false;
}

SavePrefix BuildSaveContextPrefix() {
    SavePrefix output{};

    // Serialize by the original NTSC-U 1.0 SaveContext offsets rather than
    // memcpy'ing the native SoH structure. SoH has expanded several nested
    // structures, so its host layout intentionally diverges from N64 RAM.
    WriteBE32(output, 0x00, static_cast<uint32_t>(gSaveContext.entranceIndex));
    WriteBE32(output, 0x04, static_cast<uint32_t>(gSaveContext.linkAge));
    WriteBE32(output, 0x08, static_cast<uint32_t>(gSaveContext.cutsceneIndex));
    WriteBE16(output, 0x0C, gSaveContext.dayTime);
    // 0x0E-0x0F are alignment bytes in the original structure.
    WriteBE32(output, 0x10, static_cast<uint32_t>(gSaveContext.nightFlag));
    WriteBE32(output, 0x14, static_cast<uint32_t>(gSaveContext.totalDays));
    WriteBE32(output, 0x18, static_cast<uint32_t>(gSaveContext.bgsDayCount));

    for (size_t i = 0; i < 6; ++i) {
        output[0x1C + i] = static_cast<uint8_t>(gSaveContext.newf[i]);
    }

    WriteBE16(output, 0x22, gSaveContext.deaths);
    for (size_t i = 0; i < 8; ++i) {
        output[0x24 + i] = gSaveContext.playerName[i];
    }

    WriteBE16(output, 0x2C, static_cast<uint16_t>(gSaveContext.n64ddFlag));
    WriteBE16(output, 0x2E, static_cast<uint16_t>(gSaveContext.healthCapacity));
    WriteBE16(output, 0x30, static_cast<uint16_t>(gSaveContext.health));
    output[0x32] = static_cast<uint8_t>(gSaveContext.magicLevel);
    output[0x33] = static_cast<uint8_t>(gSaveContext.magic);
    WriteBE16(output, 0x34, static_cast<uint16_t>(gSaveContext.rupees));
    WriteBE16(output, 0x36, gSaveContext.swordHealth);
    WriteBE16(output, 0x38, gSaveContext.naviTimer);
    output[0x3A] = gSaveContext.isMagicAcquired;
    output[0x3B] = static_cast<uint8_t>(gSaveContext.unk_3B[0]);
    output[0x3C] = gSaveContext.isDoubleMagicAcquired;
    output[0x3D] = gSaveContext.isDoubleDefenseAcquired;
    output[0x3E] = gSaveContext.bgsFlag;
    output[0x3F] = gSaveContext.ocarinaGameRoundNum;

    return output;
}

bool ReadCanonicalByte(uint32_t n64Address, const SavePrefix& saveContext, uint8_t& output) {
    if (n64Address >= N64MemoryAdapter::kSaveContextBase &&
        n64Address < N64MemoryAdapter::kSaveContextBase + N64MemoryAdapter::kSaveContextBootstrapSize) {
        output = saveContext[n64Address - N64MemoryAdapter::kSaveContextBase];
        return true;
    }

    if (gPlayState != nullptr) {
        if (ReadBE16Byte(n64Address, kPlayStateSceneNumAddress, static_cast<uint16_t>(gPlayState->sceneNum), output)) {
            return true;
        }
        if (ReadBE16Byte(n64Address, kMessageTextIdAddress, gPlayState->msgCtx.textId, output)) {
            return true;
        }
    }

    // The base OoT achievement set uses the original main-BGM sequence value
    // around boss encounters. Community RA scripts identify raw address
    // 0x124E54 as musicId; after Mupen word swizzling the canonical u16 lives
    // at 0x124E56.
    if (ReadBE16Byte(n64Address, kMainBgmSequenceAddress, func_800FA0B4(SEQ_PLAYER_BGM_MAIN), output)) {
        return true;
    }

    return false;
}

} // namespace

uint32_t N64MemoryAdapter::Read(uint32_t address, uint8_t* buffer, uint32_t numBytes) {
    if (buffer == nullptr || numBytes == 0 || address >= kRdramSize) {
        return 0;
    }

    const SavePrefix saveContext = BuildSaveContextPrefix();
    uint32_t bytesRead = 0;

    for (; bytesRead < numBytes; ++bytesRead) {
        const uint32_t raAddress = address + bytesRead;
        if (raAddress >= kRdramSize) {
            break;
        }

        // Mupen64Plus exposes N64 RDRAM as host-native 32-bit words on the
        // little-endian platforms where the set was authored. Existing N64 RA
        // conditions therefore observe each four-byte group in ^3 byte order.
        // Convert the requested RA byte back to its canonical N64 byte address.
        const uint32_t n64Address = raAddress ^ 0x3U;

        if (!ReadCanonicalByte(n64Address, saveContext, buffer[bytesRead])) {
            break;
        }
    }

    return bytesRead;
}

} // namespace ShipwrightRA
