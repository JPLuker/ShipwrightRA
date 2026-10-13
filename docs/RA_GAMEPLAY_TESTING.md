# ShipwrightRA gameplay test checklist

Use a build from `feature/retroachievements` containing the RetroAchievements test panel. The current integration is experimental. A successful build and synthetic runtime tests do not establish compatibility with the existing OoT achievement set.

## Start a session

1. Launch with your existing game assets. Keep your original save backed up; start with a normal vanilla save, default game speed, and no cheats, randomizer, or save states. Start with NTSC-U 1.0 for the first parity comparison: the adapter currently reconstructs that memory layout.
2. Open **Settings → RetroAchievements → Open gameplay test panel**.
3. Sign in with your RetroAchievements username and password. Credentials are not saved to configuration files. The password input clears after Sign in; the session token stays in memory until logout/exit. Never paste a password/token into a bug report.
4. Browse to the original OoT ROM used for your installation, then select **Use ROM / reload set**. Selecting a ROM identifies the RA set; it does not replace your installed game assets.
5. Leave **Test mode - do not submit unlocks** enabled. This is the default each process launch. In this mode, achievement triggers are local test results and do not award achievements to your account.
6. Enter a normal save. The panel should show the resolved game title/ID, achievement groups, and a rising evaluated-frame count. Loading a set waits until gameplay exists so scene-dependent memory is available.

## Test and record

| Check | Action | Expected result |
| --- | --- | --- |
| Sign-in | Use correct credentials, then repeat with an intentionally wrong password after signing out | Correct login shows your account; bad login shows a useful error without freezing the game |
| Title screen | Remain at file select after selecting a ROM | Login can finish, but the set does not load and evaluated frames do not rise |
| Identification | Enter a vanilla save | The title/ID correspond to Ocarina of Time; an unrelated title is a failure—stop the session |
| Supported conditions | Open the achievement groups and choose an available early-game condition whose description you can reproduce | The achievement is active rather than unsupported; meeting the condition produces a `Test trigger` event |
| Test-mode isolation | Trigger a condition with Test mode on | The panel records the trigger; no account unlock is submitted |
| Unsupported conditions | Inspect the Unsupported group | Unsupported achievements are visible; unknown reads are reported instead of invented values |
| Return to title | Exit the save normally | The set unloads; frame evaluation stops; logging in remains possible |
| Fresh load | Re-enter a save | A fresh set loads and local hit/trigger state does not carry across saves |
| Retry | Select a missing/invalid ROM, then correct it and select Use ROM / reload set | Error is visible; corrected input can start a new load |
| Connectivity | Disconnect/reconnect the network during sign-in or loading | Gameplay remains responsive; errors are visible; reload/sign-in can be retried |
| Stop | Press Stop session | No automatic reload occurs until Use ROM / reload set is pressed again |

Copy the diagnostic report **while the set is still loaded**, especially after a trigger or unexpected result. It includes the game ID/hash, frame count, achievement group counts, unsupported IDs/titles, the first 64 distinct unmapped addresses, and recent trigger/connection events. It excludes the account username, password/token, and local ROM path. Add your OS, build commit, scene, action taken, and expected result when sharing it.

## Compare against an emulator

For one unchanged existing achievement, reproduce the same starting state and actions in an RA-supported N64 emulator and this port. Record whether each side triggers, the relevant scene/health/inventory state, and any unsupported addresses reported by the port. A synthetic test achievement passing in automation is not a substitute for this comparison.

Only after that comparison succeeds, use **Stop session**, turn Test mode off, and select **Use ROM / reload set** for an explicit softcore submission test. Changing submission mode is blocked while a set is loading/loaded. Hardcore is always disabled. A trigger event by itself is not proof of a server-confirmed unlock; verify the achievement on your account and check the log for submission errors.

## Still unverified

- Complete coverage of the original achievement set and actor-dependent memory.
- Every supported ROM revision and parity between native gameplay and emulator behavior.
- Save-state loading, cheats, randomizer, and gameplay-affecting enhancements.
- A real account unlock from native gameplay.

The test panel is a diagnostic interface, not the finished achievements UI. Session-only sign-in and ROM selection are intentional; secure token persistence and automatic ROM discovery remain later work.
