# Slippi Direct beta: release notes and go-live checklist

This file is for the maintainer's review before launch. The release text below is ready to paste into GitHub.

## Release title

`Slippi Direct beta v0.1.0 (pre-release)`

## Release body (paste into the GitHub release)

> **This is a beta pre-release.** The regular Melee for New 3DS is unchanged. Its latest release and builder are [here](https://github.com/2gifts/melee-3ds/releases/latest).

Your New 3DS can now play **1v1 online against a friend on Slippi Dolphin (PC)** with Slippi's **Direct** mode: each of you types the other's connect code.

**Download:** `Melee-3DS-Slippi-Beta-Builder-v0.1.0.zip`. Extract it and run **Build Melee Slippi Beta.bat** with your own US Melee v1.02 `.iso`, just like the regular builder. It installs as its own HOME Menu app (**Melee: Slippi Direct beta**, green icon) **next to** the regular game.

**Read first:** the [Slippi beta guide](https://github.com/2gifts/melee-3ds/blob/3ds/native-3ds/docs/slippi/SLIPPI_BETA.md).

**What works**
- Slippi's online menus: 1-P Mode → Online Play → Direct, and Slippi's online character select.
- Connect codes on the touch screen or with the buttons, with recent codes remembered.
- Lock-in, rematches, and loser-picks stages (the first stage is random), including frozen Pokémon Stadium.
- Quick chat on the D-pad, as on Slippi, plus a touch-screen chat page.
- Sheik / Zelda selector, in-game names, delay display, and the DISCONNECTED / DESYNC DETECTED messages.
- Delay (1–9) and chat settings.
- The PC saves the `.slp` replay as usual.

**How it differs from Slippi on a PC (please read)**
- **No rollback on the 3DS.** It waits for your opponent's real inputs. When they arrive late, the 3DS briefly freezes instead of rolling back. On good Wi-Fi this is rare; on a bad connection it's choppy, but it stays in sync. The PC side still uses rollback.
- **Direct 1v1 only.** No Ranked, Unranked, Teams or spectating. The beta never enters Slippi's queues.
- **Loading:** the first game takes about 4–5 s on the 3DS (the PC waits for it). Later games against the same opponent start almost at once.
- **No VS splash screen**, so matches load faster.

**You need**
- A New 3DS / New 3DS XL / New 2DS XL with custom firmware and FBI.
- Your own Melee disc image.
- A Slippi account (log in to Slippi Launcher on a PC). The builder can copy it to the SD card for you.
- A friend with their **own** Slippi account.

**Privacy**
- `user.json` is your Slippi login: never share it.
- The 3DS sends it only to Slippi's matchmaking server.
- `game.log` never contains it, and it hides public IP addresses.

**Bugs:** use the **Slippi beta problem** issue form, with `SD:/3ds/melee/game.log` and your opponent's `.slp` replay. **Please don't contact the Slippi team about this beta**: it's an unofficial fan project, not made or supported by Project Slippi.

Thanks to Project Slippi (Fizzi and contributors) for Slippi, its protocol, menus and art, and to everyone who tests.

## What was tested before release

| Check | Result |
|---|---|
| Hardware run 10 (New 3DS vs PC, LAN, 4 games) | No desyncs; 0–1 waits per game; disconnect with Z and searching again worked |
| Hardware run 11 (4 games, after the load-time fixes) | First load about 4.5 s (was 9.8 s); later loads 0.1–0.5 s; no desyncs |
| Hardware run 9 (phone hotspot, 72–113 ms ping) | Stayed in sync to the end; choppy, as expected without rollback |
| PC replays from runs 10 and 11 in the replay harness | 8 of 8 match the 3DS frame for frame (only known pre-game harness rounding) |
| Two-emulator pair test (2 games) | 4531 of 4531 records identical in each game |
| Emulator UI test (menus, code entry, chat, loser's stage pick, game 2 from the RAM cache) | Passes |
| Builder clean-room run (zip extracted to a path with spaces, fresh `MeleeSlippiBuild` work folder, real disc image, dummy Slippi account) | All 8 steps passed. The output holds the beta CIA (title 000400000F4D4700, green disc icon, 3D banner), `3ds/melee-slippi/melee-slippi.3dsx`, the shared game files, Slippi's 5 menu files and `slpCSS.dat`, the licenses (with ENet), the guide and "What to do next". There is no `3ds/melee/melee.3dsx`, so the regular game is untouched. |
| SD copy onto a card that already has another Slippi account | That account is kept and the builder says so |
| Beta first start (emulator) | `saves/slippi/` is created and seeded from `saves/unlocked/` (1 save copied). Without `user.json`, both screens say "No Slippi account" and how to fix it. |

## Go-live checklist (nothing here has been done yet)

1. **Review** the `slippi-beta` branch of the fork (`slippi-3ds`), especially:
   - this file;
   - `native-3ds/docs/slippi/SLIPPI_BETA.md`;
   - the README section;
   - `.github/ISSUE_TEMPLATE/3-slippi-beta.yml`.
2. **Decide where the code lives.** Recommended: push `slippi-beta` to the main repo as its own branch, and keep `3ds` (the stable build) unchanged until the beta settles.
   ```bash
   git push upstream-github slippi-beta
   ```
3. **Build the builder zip** from the committed tree:
   ```bash
   python native-3ds/tools/make_builder_zip.py 0.1.0 --slippi
   ```
   This writes `native-3ds/dist/Melee-3DS-Slippi-Beta-Builder-v0.1.0.zip`.
4. **Create the release as a pre-release that is NOT "Latest".** The README and the stable builder links point at `releases/latest`, which must stay the stable release.
   ```bash
   gh release create slippi-beta-v0.1.0 --repo 2gifts/melee-3ds --target slippi-beta --prerelease --latest=false --title "Slippi Direct beta v0.1.0 (pre-release)" --notes-file <body.md> native-3ds/dist/Melee-3DS-Slippi-Beta-Builder-v0.1.0.zip
   ```
   Afterwards, confirm that `gh release view --repo 2gifts/melee-3ds` still shows v1.3.1 as Latest.
5. **Create the `slippi-beta` issue label**, which the form uses.
6. **Optional soft launch:** share the release link in a small community first, for a week, before announcing it widely.
7. **Merge into `3ds` later**, once the beta has settled. The README section and the issue form only appear on the default branch after that merge. Until then, link people to the guide on the `slippi-beta` branch, or copy its text into the release body.
