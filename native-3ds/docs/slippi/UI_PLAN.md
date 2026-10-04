# Slippi Direct UI/UX plan (3DS fork)

Goal: the vanilla Slippi Direct experience on the top screen, driven by the
real Melee menus. The bottom screen is a touch companion in the port's
existing theme: header, cards, pills, ink/muted/gold, Melee font.

Sources:
- Vanilla behaviour: `slippi-ssbm-asm/Online/*` and Dolphin `EXI_DeviceSlippi.cpp`.
- Engine hooks: decomp `mn/` and `gm/`.
- Bottom-screen idiom: `port/3ds/bottom_draw.c`.

## Flow (matches Slippi)

1. **Boot.** The title screen and main menu boot normally. The console boot menu goes away.
   - `config.ini boot_direct=1` keeps the old straight-to-match path for automated tests.
2. **Main menu.** 1P → **Online Play** (the hidden 1P option at index 2, as in Slippi).
   - It opens the Online submenu: Ranked, Unranked, **Direct**, Teams, Party, Log-in, Log-out, Update.
   - Only Direct is enabled; the rest are shown locked. They are not supported by this fork.
3. **Direct** loads the online mode: `GM_HANYU_CSS` (8), with its state table replaced as Slippi does.
   - **CSS:** 1P-style, only your own door.
   - **SSS:** used by the loser of the last game.
   - **VS splash:** both names; the announcer calls the opponent's character.
   - **In-game.**
   - Then back to the CSS. There is no results screen.
4. **CSS states** (top-screen SIS text, as in `LoadCSSText.asm`):
   - "Direct Mode"
   - "User" / name
   - "Connect Code" / own code
   - Three status lines with spinners:
     - "Select your character" / "Character selected"
     - "Press START to enter code / lock in / select stage" / "Locked in"
     - "Searching for X" / "Connecting to X" / "Waiting on opponent"
   - Grey hints:
     - "Press Z to cancel"
     - "Press Z to clear error"
     - "Hold Z to disconnect"
     - "Use D-Pad to Chat"
   - When connected: "Playing: <name>".
   - Errors: red text, wrapped.
5. **Controls** (as vanilla):
   - START (character chosen, idle) opens code entry.
   - Confirming the code locks in and searches. The first stage is random.
   - Z cancels a search or clears an error. Holding Z (49 frames) disconnects.
   - A/B cannot unselect while locked in. X/Y colour changes only before lock-in.
   - B (vanilla CSS back-out) leaves to the Online submenu and cleans up the connection.
6. **Connect code entry.** It is on the bottom screen (touch keyboard), opened by START.
   - Physical controls mirror the vanilla name entry:
     - D-pad/stick move;
     - A types; B erases, or goes back when empty;
     - L/R cycle recent codes; Z accepts the suggestion;
     - START confirms.
   - The suggestion is the most recent code with a matching prefix, drawn in grey after the typed characters.
   - History is kept in `sdmc:/3ds/melee/slippi/direct-codes.txt`, newest first.
   - The top screen keeps the CSS and shows "Enter code on the touch screen".
7. **Stage after game 1:**
   - The winner presses START to lock in.
   - The loser presses START, goes to the SSS, and picks; the pick locks in.
   - Z on the SSS toggles frozen Stadium.
   - The first player (by index) with a stage selected wins.
8. **Chat.** On the CSS while connected:
   - D-pad page, then direction, with the 16 default messages.
   - The bottom screen also shows the four pages as touch pills.
   - Incoming and outgoing messages show as `name: msg` in port colours, on the top CSS and the bottom log.
   - A setting turns chat on or off; when off, the client auto-replies "has chat disabled".
9. **In game:**
   - Names above percents and "Delay: Nf" (as `InitInGame.asm`).
   - "DISCONNECTED" ends the game after 90 frames.
   - "DESYNC DETECTED".
   - Vanilla pause and LRAS (no contest; the LRAS player picks the next stage).
10. **Bottom-screen online pages** (themed):
    - CSS companion:
      - header "DIRECT";
      - user card (name, code) and opponent card (name, code, ping);
      - status lines;
      - pills: ENTER CODE / CANCEL / DISCONNECT, CHAT, SETTINGS.
    - Code keyboard: QWERTY plus 0-9 and #, a recent-codes list, ERASE and CONFIRM.
    - Settings: delay frames 1-9 (default 2) and quick chat on/off. Stored in `config.ini`.
    - In match: both player cards (as the existing battle page) plus a connection strip (ping, delay).
11. **Menu art.** Slippi's `MnMaAll`, `SdMenu` and `MnSlMap` VCDIFF patches (GPL, from Slippi Dolphin) are applied to the player's own disc files by a PC tool. That gives the "Online Play" label, the Online submenu labels and descriptions, and the frozen-Stadium SSS art.
    - Without the patched files, the menu falls back to native text labels.

## Phases

1. **Mode skeleton.**
   - Normal boot, the 1P → Online entry, and the online mode state table.
   - The CSS with a non-blocking network poll and lock-in.
   - Match start from the CSS, and the return to the CSS.
   - Hold Z / cancel, and B to leave.
   - A bottom companion page with live status.
2. Code entry keyboard with history and autocomplete.
3. Loser stage pick (SSS), frozen Stadium toggle, random first stage.
4. Top-screen CSS text exactly as vanilla, VS splash names, in-game HUD (names, delay, DISCONNECTED).
5. Chat (CSS and bottom), settings page, sounds.
6. Menu art patches (VCDIFF tool) and the Online submenu.
7. Hardware passes and edge cases (from the network test list).

## Status (2026-10-03)

**Done, tested in Azahar against the fake peer** (`tools/slippi/ui/ui_test.py`):
- Normal boot. 1P shows Online Play, which opens Slippi's Online submenu (only Direct is available) and then the online CSS.
- Online CSS:
  - Slippi's top-screen text: status lines with spinners, User / Connect Code, Playing:, hints, errors.
  - START opens the code keyboard. Confirm locks in and searches; the first stage is random.
  - Z cancels or clears an error; hold Z to disconnect.
  - Locked in, the fighter cannot be changed or recoloured.
- Code keyboard on the bottom screen: recent codes (L/R), suggestion (X), B to erase. History lives in `direct-codes.txt`, seeded from config.ini's `opponent=`.
- After a game: winner START locks in; loser START goes to the SSS. Z on the SSS (or the bottom page) toggles frozen Stadium.
- Quick chat:
  - D-pad page, then direction.
  - Each side's message set, shown as `name: message` on the top CSS.
  - The bottom QUICK CHAT page.
- Bottom pages:
  - online CSS companion;
  - keyboard;
  - chat;
  - SSS;
  - settings (delay 1-9, chat on/off, clear codes);
  - match cards with display names and ping.
- Menu art: `tools/slippi/slippi_files.py` writes Slippi's patched menu files, made from the player's own disc, to `sdmc:/3ds/melee/slippi/files/`.

**Added 2026-10-03 (after hardware run 9):**
- **VS splash before every game** (`online_mode.c`, state `ST_SPLASH`). It is Classic mode's intro scene with Slippi's template, as in SplashScenePrep. You are on the left and the opponent on the right, so the announcer calls the opponent's fighter. "P1/P2 name" labels in port colours and the stage name come from InitVsSplash.
- **In-game text** (`online_hud.c`, InitInGame): each name under its damage display, "Delay: Nf", and DISCONNECTED (red) or DESYNC DETECTED (amber). A desync now ends the game as a no-contest, as Slippi's StartEngineLoop does.
- **Zelda/Sheik:** the Zelda icon plays Sheik by default (Slippi's MajorSceneLoad); a SHEIK/ZELDA pill on the bottom page switches it. Zelda is restored when leaving online play.
- **CSS title:** slpCSS.dat (Slippi's art, installed by slippi_files.py) supplies the MODE animation, frame 0 = MELEE, on joint 36. Without the file the EVENT MATCH title is hidden.
- **Code keyboard buttons:** edges come from held keys, because game.c's extra HID scan ate about half the presses. Directions auto-repeat. Buttons still held when the keyboard closes do not reach the CSS.

**Changed after hardware run 10:**
- **VS splash: off.** The CSS goes straight to the match (`css_exit` sets `ST_VS`). The splash scene added about two seconds to a 3DS load that was already slower than the PC's. `ST_SPLASH` and its code stay in place.
- **Larger chat on the top screen.**
  - Messages are size 0.55, up from Slippi's 0.4, on lines 19-21.
  - The open chat page is size 0.5, on its own lines 24-28, and takes the place of the "Hold Z" and "D-Pad" hints.
  - A long line is narrowed to fit the panel instead of running off it.
- **No "Choose your character!" after the stage pick.** It made the CSS sound like a new selection. It still plays on entry from the menus and after each game, as in VS mode (`mp_slippi_css_quiet`).

**Still open:** custom chat messages are shown from the matchmaking reply when present (Slippi Launcher settings), but the 3DS has no editor for its own.
