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
        +--> N64-compatible memory adapter
        |
        +--> queued libcurl HTTP transport
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
- [x] Add safe memory callback backed by the compatibility adapter
- [x] Add queued HTTP server callback with an explicit unsupported-platform fallback
- [x] Disable Hardcore by default until Shipwright-specific restrictions exist
- [x] Wire the manager into Shipwright startup through `RegisterShipInitFunc`
- [ ] Verify all supported desktop builds

### Phase 0.2 - Account connection

- [x] Implement nonblocking, frame-pumped HTTP transport for `rc_client_server_call_t`
- [x] Implement password/token login API and development token hook (live-account validation pending)
- [ ] Persist authentication without storing plaintext passwords
- [x] Expose authentication state and username through the manager API
- [x] Queue network requests without waiting for responses during startup/gameplay

### Phase 0.3 - Game identification

- [x] Add an offline N64 ROM hash probe using rcheevos' hashing implementation
- [ ] Locate the original user-provided OoT ROM used for asset extraction automatically
- [x] Implement hash resolution and set loading through `rc_client` (live-ROM validation pending)
- [ ] Confirm the existing Ocarina of Time game/set is identified
- [ ] Test supported OoT ROM revisions

For development, `SHIPWRIGHT_RA_ROM_PATH` may be set to a local OoT ROM. After token login succeeds and a save is active, the fork generates the RetroAchievements Nintendo 64 hash and asks `rc_client` to load the matching set. The hash is written to the normal application log. The path itself is not logged.

Example on Linux/macOS:

```bash
SHIPWRIGHT_RA_ROM_PATH="/path/to/oot.z64" ./soh.elf
```

Example in PowerShell:

```powershell
$env:SHIPWRIGHT_RA_ROM_PATH="C:\path\to\oot.z64"
.\soh.exe
```

Set `SHIPWRIGHT_RA_USERNAME` and `SHIPWRIGHT_RA_TOKEN` in the launching process as well. The token must be a RetroAchievements client login token, not a Web API key. Do not put credentials in source files, screenshots, or bug reports. No password or token is persisted by this scaffold.

Login progresses at the title screen. Set loading waits for an active save because rcheevos validates memory addresses during activation; loading without live scene data would incorrectly mark some achievements unsupported. Exiting a save unloads the set, and entering another save starts a fresh load.

These environment-variable hooks are temporary developer scaffolding, not the planned user-facing configuration.

### Phase 0.4 - Memory research

Determine how RetroAchievements' expected N64 addresses map to Shipwright's native/decompiled runtime state.

Initial probes:

- [x] health
- [x] rupees
- [x] scene ID
- [ ] inventory
- [ ] equipment
- [ ] quest/event flags
- [ ] dungeon state
- [ ] actor-dependent values required by the existing achievement set

Verified existing-set anchors now include:

- RA `0x11A602` -> original `gSaveContext.health` at N64 `0x8011A600`
- RA `0x1C8546` -> low byte of original `PlayState.sceneNum`
- RA `0x1D8872` -> original `PlayState.msgCtx.textId`
- RA `0x124E54` -> current main BGM sequence used by boss/challenge conditions

See `docs/RA_MEMORY_MAP.md` for the compatibility notes and address derivations.

### Phase 0.5 - N64 memory compatibility bridge

- [x] Translate initial RA N64 memory reads into authoritative Shipwright state
- [x] Define byte ordering identical to the Mupen-style N64 memory model used by the existing set
- [x] Return invalid/unmapped reads safely
- [x] Map the original SaveContext bootstrap region through health/magic/rupees
- [x] Map current scene, dialog ID, and main BGM sequence
- [x] Serialize inventory/equipment and event flag regions (gameplay parity validation pending)
- [ ] Support all memory regions used by the existing 97-achievement base set
- [x] Add deterministic tests against native Shipwright structures and original offsets

### Phase 0.6 - First real achievement

- [ ] Load the existing OoT achievement set unchanged
- [x] Wire `rc_client_do_frame` to logical game updates while a normal save is active
- [ ] Trigger at least one existing achievement from native Shipwright gameplay
- [ ] Confirm the same achievement condition behaves identically under an RA-supported N64 emulator

This is the main technical go/no-go checkpoint. If existing achievement definitions require memory semantics that cannot reasonably be reproduced, the architecture must be reconsidered before UI work begins.

### Phase 0.7 - Full runtime

- [x] Per-frame evaluation hook, with idle network/retry processing outside gameplay
- [x] Runtime unlock submission/event logging wired through `rc_client` (real unlock validation pending)
- [ ] achievement progress
- [ ] session reconnect/recovery
- [ ] save/load handling
- [x] Reset runtime on save entry and unload on save exit (save-state handling still pending)

### Phase 0.8 - Native UI

- [x] Session-only gameplay test panel under Settings → RetroAchievements
- [x] Connected account display and password sign-in (no persistence)
- [x] Test-panel achievement list including unsupported entries
- [ ] unlock notifications
- [ ] progress notifications
- [x] Test-panel login/load errors and copyable diagnostics

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

## Current runtime behavior and validation

The manager is created through `RegisterShipInitFunc`. It owns `rc_client` and a libcurl multi transport. Requests copy their inputs before returning; responses and rcheevos callbacks are dispatched on the game thread during idle processing. Shutdown cancels pending transfers and completes their callbacks before destroying the client. HTTP redirects are not followed, TLS verification uses libcurl defaults, and response bodies are capped at 16 MiB.

Gameplay frames are evaluated only with an active normal save. Authentication and request retries continue at the title/file-select screens. Unlocks, completion, disconnect/reconnect, and server errors currently go to the Shipwright log. Hardcore remains disabled. Test mode defaults on: triggers are evaluated without account submissions. Stop the session before explicitly choosing softcore submission mode.

The memory adapter remains sparse. Unsupported addresses return invalid reads; this can disable achievements that use still-unmapped fields. No claim of full set support, official support, or a verified real unlock is made yet. The session-only gameplay test panel provides password sign-in, ROM selection, achievement groups, recent triggers, and copyable diagnostics. Secure token persistence, automatic ROM discovery, save-state handling, and complete memory coverage remain unfinished.

### Automated validation

The validation workflow compiles each integration source before the full Linux build and runs the HTTP tests against the same pinned rcheevos headers used by the app:

```bash
python3 tests/retroachievements/run_http_tests.py --rcheevos build-cmake/_deps/shipwright_ra_rcheevos_source-src
```

The tests use only a local loopback server, synthetic request data, and no account or ROM. They cover delayed responses without blocking enqueue, POST/URL ownership, chained callbacks, HTTP status propagation, redirect refusal, response limits, shutdown, reinitialization, and the no-curl fallback. The workflow also runs manager lifecycle tests against the real rcheevos runtime using a local mock login API: successful/failed login, duplicate login, logout during an outstanding request, replacement login, shutdown, and reinitialization. Pass `--spdlog /path/to/spdlog` to run these locally; distro spdlog packages may also need `--fmt-external`. These tests are not proof of in-game achievement correctness.

### Build integration follow-up

Full desktop builds reached the final link, then exposed an MD5 symbol collision with StormLib on Linux/macOS and a Windows CRT mismatch. The rcheevos target now privately prefixes its MD5 functions and uses the same static MSVC runtime as Shipwright. A small link regression test uses the actual CMake dependency target, a competing `md5_init` symbol, and a known digest so this failure is caught before the full game build. The dedicated full Linux validation build passed for `87916d4e`; the desktop package builds are tracked separately.

### Next validation checkpoint

1. Get the full Linux build green; inspect the desktop build matrix independently.
2. Launch with a user-owned supported ROM and client login token; verify login and game ID in the log.
3. Enter a normal save and inspect which achievements rcheevos marks unsupported.
4. Compare one existing achievement against an RA-supported emulator before expanding the UI.

## Gameplay test entry point

Use **Settings → RetroAchievements → Open gameplay test panel**. See [RA_GAMEPLAY_TESTING.md](RA_GAMEPLAY_TESTING.md) for the ordered checklist and emulator comparison. Credentials and ROM paths are session-only. The environment hooks remain available, with Test mode enabled by default.
