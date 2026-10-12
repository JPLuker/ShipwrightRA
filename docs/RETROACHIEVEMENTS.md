# ShipwrightRA RetroAchievements Integration

This fork is an experimental attempt to integrate the official `rcheevos` client runtime directly into Shipwright / Ship of Harkinian.

The immediate objective is technical validation. Upstream acceptance and official RetroAchievements support are separate questions and are intentionally deferred until the integration is proven.

## Architecture

```text
Shipwright / OoT runtime
        |
        v
ShipwrightRA RetroAchievementsManager
        |
        +--> N64-compatible memory adapter (planned)
        |
        +--> HTTP transport (planned)
        |
        v
rcheevos rc_client
        |
        v
RetroAchievements API
```

`rcheevos` is pinned to release **v12.5.0** by `soh/CMake/GlobalSettingsInclude.cmake`.

The project-local CMake hook is used so the experiment does not require invasive edits to Shipwright's upstream build files. This should reduce conflicts when rebasing the fork onto newer Shipwright revisions.

## Current status

### Phase 0.0 - Baseline

- [x] Fork Shipwright into `JPLuker/ShipwrightRA`
- [x] Create isolated `feature/retroachievements` development branch
- [x] Keep upstream Shipwright source unchanged for the initial bootstrap

### Phase 0.1 - rcheevos bootstrap

- [x] Pin rcheevos v12.5.0
- [x] Compile the `rc_client` runtime with hashing support
- [x] Export `RC_CLIENT_SUPPORTS_HASH` to the Shipwright consumer target
- [x] Add a ShipwrightRA-owned client lifecycle wrapper
- [x] Add rcheevos logging through Shipwright's spdlog logger
- [x] Add safe placeholder memory callback
- [x] Add safe placeholder server callback
- [x] Disable Hardcore by default until Shipwright-specific restrictions exist
- [x] Wire the manager into Shipwright startup through `RegisterShipInitFunc`
- [ ] Verify all supported desktop builds

### Phase 0.2 - Account connection

- [ ] Implement HTTP transport for `rc_client_server_call_t`
- [ ] Login with RetroAchievements credentials/token
- [ ] Persist authentication without storing plaintext passwords
- [ ] Expose connection state and username to Shipwright
- [ ] Handle network/API failures without impacting game startup

### Phase 0.3 - Game identification

- [x] Add an offline N64 ROM hash probe using rcheevos' hashing implementation
- [ ] Locate the original user-provided OoT ROM used for asset extraction automatically
- [ ] Resolve the hash through RetroAchievements
- [ ] Confirm the existing Ocarina of Time game/set is identified
- [ ] Test supported OoT ROM revisions

For development, `SHIPWRIGHT_RA_ROM_PATH` may be set to a local OoT ROM. On startup the fork will generate the RetroAchievements Nintendo 64 hash and write it to the normal application log. The path itself is not logged.

Example on Linux/macOS:

```bash
SHIPWRIGHT_RA_ROM_PATH="/path/to/oot.z64" ./soh.elf
```

Example in PowerShell:

```powershell
$env:SHIPWRIGHT_RA_ROM_PATH="C:\path\to\oot.z64"
.\soh.exe
```

This environment-variable hook is intentionally temporary developer scaffolding, not the planned user-facing configuration.

### Phase 0.4 - Memory research

Determine how RetroAchievements' expected N64 addresses map to Shipwright's native/decompiled runtime state.

Initial probes:

- [ ] health
- [ ] rupees
- [ ] scene ID
- [ ] inventory
- [ ] equipment
- [ ] quest/event flags
- [ ] dungeon state
- [ ] actor-dependent values required by the existing achievement set

### Phase 0.5 - N64 memory compatibility bridge

- [ ] Translate RA N64 memory reads into authoritative Shipwright state
- [ ] Support reads spanning mapped fields/regions
- [ ] Define byte ordering identical to the RA N64 memory model
- [ ] Return invalid reads safely
- [ ] Add deterministic tests for mapped addresses

### Phase 0.6 - First real achievement

- [ ] Load the existing OoT achievement set unchanged
- [ ] Evaluate frames through `rc_client_do_frame`
- [ ] Trigger at least one existing achievement from native Shipwright gameplay
- [ ] Confirm the same achievement condition behaves identically under an RA-supported N64 emulator

This is the main technical go/no-go checkpoint. If existing achievement definitions require memory semantics that cannot reasonably be reproduced, the architecture must be reconsidered before UI work begins.

### Phase 0.7 - Full runtime

- [ ] Continuous per-frame evaluation
- [ ] Unlock submission
- [ ] achievement progress
- [ ] session reconnect/recovery
- [ ] save/load handling
- [ ] reset handling

### Phase 0.8 - Native UI

- [ ] RetroAchievements settings section
- [ ] connected account display
- [ ] achievement list
- [ ] unlock notifications
- [ ] progress notifications
- [ ] connection/error state

### Phase 0.9 - Hardcore compatibility

Shipwright exposes features that can invalidate RetroAchievements Hardcore assumptions. Hardcore must remain disabled until these are explicitly audited and enforced.

Candidates include:

- save states
- cheats/debug controls
- game-speed manipulation
- gameplay-affecting enhancements
- randomizer behavior
- other state manipulation unavailable on original hardware

### Phase 0.10 - Remaining RA features

- [ ] Rich Presence
- [ ] leaderboards
- [ ] challenge indicators
- [ ] encore/unofficial achievement settings where appropriate

### Phase 0.11 - Compatibility and regression testing

- [ ] Windows
- [ ] Linux
- [ ] macOS where practical
- [ ] supported OoT ROM revisions
- [ ] clean profile
- [ ] existing save files
- [ ] network loss
- [ ] RA API outage
- [ ] upstream Shipwright rebase

### Phase 0.12 - Upstream-quality cleanup

- [ ] remove experimental shortcuts
- [ ] document public interfaces
- [ ] isolate RA-specific changes from generic Shipwright changes
- [ ] split changes into reviewable commits/PRs
- [ ] determine whether any generic hooks should be proposed upstream separately

## Design rules

1. **Do not fake achievement state.** RA conditions should consume game state equivalent to the original N64 values.
2. **Do not modify the existing OoT achievement definitions merely to make the port pass.** The compatibility layer should adapt Shipwright to the set, not the other way around, unless research proves that impossible.
3. **Do not block game startup on RetroAchievements.** Networking and authentication failures must degrade cleanly.
4. **Do not claim Hardcore compatibility before enforcement exists.** The bootstrap explicitly disables Hardcore.
5. **Do not store plaintext RA passwords.** Authentication persistence must use the token/session mechanism supported by the RA client/API.
6. **Keep upstream rebases cheap.** Prefer project-local extension points and small lifecycle hooks over broad modifications to Shipwright core files.
7. **Pin external dependencies.** Updating rcheevos should be an intentional, tested change.
8. **Validate against the pinned rcheevos tag.** Upstream file layout may change after the pinned release and must not be assumed to match it.

## Current bootstrap behavior

The current `RetroAchievementsManager` is created during normal Shipwright startup through `RegisterShipInitFunc` and destroyed during process teardown.

It currently provides:

- an initialized `rc_client_t` lifecycle;
- rcheevos INFO-level diagnostics in the Shipwright log;
- an offline Nintendo 64 ROM hash probe for development;
- Hardcore forced off until restrictions are implemented.

Its integration callbacks are still deliberately limited:

- runtime memory reads return zero bytes / invalid address;
- server requests return a client error immediately because HTTP transport is not implemented;
- no credentials are requested or stored;
- no RA network traffic occurs.

This keeps the bootstrap safe while allowing build, lifecycle, and ROM-hashing validation before account/network integration begins.
