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

using SaveCore = std::array<uint8_t, N64MemoryAdapter::kSaveContextCoreSize>;

constexpr uint32_t kPlayStateSceneNumAddress = 0x001C8544;
constexpr uint32_t kMessageTextIdAddress = 0x001D8870;
constexpr uint32_t kMainBgmSequenceAddress = 0x00124E56;

constexpr uint32_t kEventChkInfOffset = 0x0ED4;
constexpr uint32_t kItemGetInfOffset = 0x0EF0;
constexpr uint32_t kInfTableOffset = 0x0EF8;

void WriteBE16(SaveCore& output, size_t offset, uint16_t value) {
    output[offset] = static_cast<uint8_t>((value >> 8) & 0xFF);
    output[offset + 1] = static_cast<uint8_t>(value & 0xFF);
}

void WriteBE32(SaveCore& output, size_t offset, uint32_t value) {
    output[offset] = static_cast<uint8_t>((value >> 24) & 0xFF);
    output[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    output[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    output[offset + 3] = static_cast<uint8_t>(value & 0xFF);
}

void SerializeItemEquips(SaveCore& output, size_t offset, const ItemEquips& equips) {
    // Original OoT stores only B + C-left/down/right (4 items) and the three
    // C-button slots. SoH extends both arrays for D-pad equips; those extra
    // entries must not shift the canonical N64 structure.
    for (size_t i = 0; i < 4; ++i) {
        output[offset + i] = equips.buttonItems[i];
    }
    for (size_t i = 0; i < 3; ++i) {
        output[offset + 0x04 + i] = equips.cButtonSlots[i];
    }
    // Original offset 0x07 is alignment padding.
    WriteBE16(output, offset + 0x08, equips.equipment);
}

void SerializeInventory(SaveCore& output, size_t offset, const Inventory& inventory) {
    for (size_t i = 0; i < 24; ++i) {
        output[offset + i] = inventory.items[i];
    }
    for (size_t i = 0; i < 16; ++i) {
        output[offset + 0x18 + i] = static_cast<uint8_t>(inventory.ammo[i]);
    }

    WriteBE16(output, offset + 0x28, inventory.equipment);
    // Original offsets 0x2A-0x2B are alignment padding.
    WriteBE32(output, offset + 0x2C, inventory.upgrades);
    WriteBE32(output, offset + 0x30, inventory.questItems);

    for (size_t i = 0; i < 20; ++i) {
        output[offset + 0x34 + i] = inventory.dungeonItems[i];
    }
    for (size_t i = 0; i < 19; ++i) {
        output[offset + 0x48 + i] = static_cast<uint8_t>(inventory.dungeonKeys[i]);
    }

    output[offset + 0x5B] = static_cast<uint8_t>(inventory.defenseHearts);
    WriteBE16(output, offset + 0x5C, static_cast<uint16_t>(inventory.gsTokens));
    // 0x5E-0x5F are alignment bytes before sceneFlags.
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

template <size_t N>
bool ReadBE16ArrayByte(uint32_t address, uint32_t fieldAddress, const uint16_t (&values)[N], uint8_t& output) {
    const uint32_t size = static_cast<uint32_t>(N * sizeof(uint16_t));
    if (address < fieldAddress || address >= fieldAddress + size) {
        return false;
    }

    const uint32_t relative = address - fieldAddress;
    const uint16_t value = values[relative / 2];
    output = (relative & 1U) == 0 ? static_cast<uint8_t>((value >> 8) & 0xFF)
                                 : static_cast<uint8_t>(value & 0xFF);
    return true;
}

SaveCore BuildSaveContextCore() {
    SaveCore output{};

    // Serialize by original NTSC-U 1.0 SaveContext offsets rather than
    // memcpy'ing the native SoH structure. SoH has expanded nested structures,
    // so its host layout intentionally diverges from N64 RAM.
    WriteBE32(output, 0x00, static_cast<uint32_t>(gSaveContext.entranceIndex));
    WriteBE32(output, 0x04, static_cast<uint32_t>(gSaveContext.linkAge));
    WriteBE32(output, 0x08, static_cast<uint32_t>(gSaveContext.cutsceneIndex));
    WriteBE16(output, 0x0C, gSaveContext.dayTime);
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

    SerializeItemEquips(output, 0x40, gSaveContext.childEquips);
    SerializeItemEquips(output, 0x4A, gSaveContext.adultEquips);
    WriteBE32(output, 0x54, gSaveContext.unk_54);
    for (size_t i = 0; i < 0x0E; ++i) {
        output[0x58 + i] = static_cast<uint8_t>(gSaveContext.unk_58[i]);
    }
    WriteBE16(output, 0x66, static_cast<uint16_t>(gSaveContext.savedSceneNum));
    SerializeItemEquips(output, 0x68, gSaveContext.equips);
    SerializeInventory(output, 0x74, gSaveContext.inventory);

    return output;
}

bool ReadCanonicalByte(uint32_t n64Address, const SaveCore& saveContext, uint8_t& output) {
    if (n64Address >= N64MemoryAdapter::kSaveContextBase &&
        n64Address < N64MemoryAdapter::kSaveContextBase + N64MemoryAdapter::kSaveContextCoreSize) {
        output = saveContext[n64Address - N64MemoryAdapter::kSaveContextBase];
        return true;
    }

    const uint32_t saveOffset = n64Address - N64MemoryAdapter::kSaveContextBase;
    if (n64Address >= N64MemoryAdapter::kSaveContextBase) {
        if (ReadBE16ArrayByte(saveOffset, kEventChkInfOffset, gSaveContext.eventChkInf, output) ||
            ReadBE16ArrayByte(saveOffset, kItemGetInfOffset, gSaveContext.itemGetInf, output) ||
            ReadBE16ArrayByte(saveOffset, kInfTableOffset, gSaveContext.infTable, output)) {
            return true;
        }
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

    const SaveCore saveContext = BuildSaveContextCore();
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
