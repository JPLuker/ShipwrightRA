# RetroAchievements OoT Memory Compatibility Notes

This document records addresses from the existing Ocarina of Time RetroAchievements set and how they correspond to the original NTSC-U 1.0 runtime state that ShipwrightRA must reproduce.

The purpose is compatibility, not emulation. Shipwright is a native port, so these addresses are reconstructed from authoritative native game state rather than backed by a literal N64 RDRAM allocation.

## N64 byte layout

Existing Nintendo 64 RetroAchievements sets were authored against the raw RDRAM layout exposed by supported Mupen-based cores on little-endian hosts. Within each 32-bit word, byte addresses therefore appear reversed relative to canonical N64 big-endian memory.

For a byte read:

```text
canonical_n64_address = ra_address ^ 3
```

For multi-byte values, each byte must be translated individually. Do not simply subtract `0x80000000` and copy host-native Shipwright structures.

## Verified anchors

| RA address | Width | Canonical N64 address | Native Shipwright source | Evidence / use |
| --- | --- | --- | --- | --- |
| `0x11A602` | 16-bit | `0x8011A600` | `gSaveContext.health` | Used by the existing OoT achievement **Rolling Thunder** to detect damage. `SaveContext.health` is original offset `0x30` from `0x8011A5D0`. |
| `0x1C8546` | 8-bit | `0x801C8545` | low byte of `gPlayState->sceneNum` | Used throughout the existing set for current-scene checks. Original `PlayState` base is `0x801C84A0`; `sceneNum` is at offset `0xA4`. |
| `0x1D8872` | 16-bit | `0x801D8870` | `gPlayState->msgCtx.textId` | Public OoT autosplitter data identifies this as the current dialog/text ID; existing achievements use it for progression events. |
| `0x124E54` | 16-bit | `0x80124E56` | current main BGM sequence (`func_800FA0B4(SEQ_PLAYER_BGM_MAIN)`) | Community OoT RA scripts name this value `musicId`; the base set uses it heavily around boss encounters. |

### Health example

The original health field is:

```text
SaveContext base:       0x8011A5D0
health offset:          +0x30
canonical health:       0x8011A600
```

A two-byte value beginning at canonical `0x...600` appears in Mupen-style raw memory at RA address `0x...602`. This is exactly the address used by the existing achievement set, validating the `^ 3` adapter model.

## High-priority SaveContext regions

The current set also reads addresses in these ranges:

- `0x11A64x` - `0x11A67x`: equipment / inventory / quest-state region.
- `0x11B4A4` and nearby: `eventChkInf` progression flags.
- `0x11B92C`: title/file-select state is referenced by public OoT tooling and related sets.

Shipwright's native `SaveContext` cannot be exposed directly. Several structures were expanded by the port (for example D-pad equipment slots), so the bridge must serialize the original N64 layout field-by-field.

## Other live-state regions

The set also uses addresses outside SaveContext, including:

- `0x1C8546`: current scene (`PlayState.sceneNum`).
- `0x1D8872`: active dialog/text ID (`PlayState.msgCtx.textId`).
- `0x124E54`: current music sequence.
- addresses around `0x1DABxx`, `0x1E71xx`, `0x1E84xx`, and similar dynamic gameplay state used by boss/challenge achievements.

The dynamic regions should not be implemented by trying to recreate original pointers or host object addresses. Each required value should be mapped to the equivalent authoritative Shipwright actor/game state, or to a small compatibility snapshot when a contiguous original structure is required.

## Implementation rules

1. Preserve the address semantics expected by the existing OoT achievement definitions.
2. Prefer semantic mappings (`gPlayState->sceneNum`, `gSaveContext.health`, etc.) over host-memory tricks.
3. Unknown memory must fail closed; do not fabricate zero-filled RDRAM and pretend it is valid.
4. Add mappings only when their original meaning is understood and testable.
5. Keep Hardcore disabled until all state-affecting Shipwright features are audited.
6. Use the existing OoT set unchanged as the compatibility target. Any need to rewrite achievements is treated as a design failure until proven otherwise.
