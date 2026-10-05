# Melee for New 3DS: Slippi Direct beta

Play Melee on your New 3DS against a friend on **Slippi Dolphin (PC)**, using Slippi's **Direct** mode. Each of you types the other's connect code, and you play 1v1 online.

**[Download the beta (v0.1.0, pre-release)](https://github.com/2gifts/melee-3ds/releases/tag/slippi-beta-v0.1.0)**: `Melee-3DS-Slippi-Beta-Builder-v0.1.0.zip`.

**This is a beta.** It works well in testing, but it is new, and you may hit bugs. It installs **next to** the regular Melee for New 3DS and never replaces it. Please read "How it differs from Slippi on a PC" below before your first match.

This is an unofficial fan project. It is **not made, endorsed or supported by Project Slippi**, so please do not ask the Slippi team for help with it. Report problems here instead (see [Reporting a problem](#reporting-a-problem)).

---

## Contents

1. [What works](#what-works)
2. [What you need](#what-you-need)
3. [Build and install](#build-and-install)
4. [Set up your Slippi account](#set-up-your-slippi-account)
5. [Play a match](#play-a-match)
6. [How it differs from Slippi on a PC](#how-it-differs-from-slippi-on-a-pc)
7. [Tips for a smooth connection](#tips-for-a-smooth-connection)
8. [Troubleshooting](#troubleshooting)
9. [Reporting a problem](#reporting-a-problem)
10. [Privacy and your account](#privacy-and-your-account)
11. [Credits and licenses](#credits-and-licenses)

---

## What works

- **Slippi Direct 1v1** against a friend on Slippi Dolphin (PC, through Slippi Launcher). You connect with connect codes over the internet or on the same Wi-Fi.
- **Slippi's online menus:** 1-P Mode → Online Play → Direct, and Slippi's online character select screen with your name, your code, the connection status and your opponent's name.
- **Code entry** on a touch-screen keyboard, or with the buttons. Recent codes are remembered.
- **Stage selection the Slippi way:**
  - the first stage is random;
  - after that, the loser of the last game picks;
  - **frozen Pokémon Stadium** is available (press Z on the stage select screen, or tap the switch).
- **Quick chat:**
  - the D-pad opens Slippi's message pages, just like on PC;
  - messages from your opponent show on the top screen;
  - chat can be turned off.
- **Sheik / Zelda:** the Zelda icon plays Sheik by default (as on Slippi). A touch-screen switch picks Zelda.
- **In-game names** under each player's damage, the **delay** you are playing with, and Slippi's **DISCONNECTED** / **DESYNC DETECTED** messages.
- **Rematches** without reconnecting. **Hold Z** on the character select screen to leave.
- **Delay setting** (1–9 frames) and a **chat on/off** setting on the touch screen.
- **Replays:** your opponent's Slippi saves the match as a normal `.slp` replay on their PC.

**Not supported:**
- Ranked, Unranked and Teams;
- doubles and more than two players;
- spectating;
- editing custom chat messages on the 3DS;
- recording replays on the 3DS.

3DS-vs-3DS play has not been tested.

The beta only uses Direct, between two people who choose to play each other. It never enters Slippi's Ranked or Unranked queues.

---

## What you need

- **A New Nintendo 3DS, New 3DS XL or New 2DS XL**, with custom firmware and FBI, the same as the regular port. Original 3DS and 2DS models are not supported.
- **Your own US Melee v1.02 disc image** (`.iso`), the same one the regular builder uses.
- **A Slippi account.** Make one in [Slippi Launcher](https://slippi.gg) on a PC; you'll copy one file from it to the SD card.
- **A friend on PC** with Slippi Launcher and their **own** Slippi account. You cannot play against your own account.
- **Wi-Fi** your 3DS can use. The 3DS only uses 2.4 GHz Wi-Fi, so stay close to the router.
- **A Windows 10 or 11 PC** for the one-click builder, with about 8 GB of free space.

---

## Build and install

The beta has its own one-click builder, **`Melee-3DS-Slippi-Beta-Builder`**. It works exactly like the regular builder.

1. Download **`Melee-3DS-Slippi-Beta-Builder-v0.1.0.zip`** from the [beta release page](https://github.com/2gifts/melee-3ds/releases/tag/slippi-beta-v0.1.0) and **extract it** (right-click → Extract All).
2. Double-click **`Build Melee Slippi Beta.bat`** and choose your Melee `.iso`.
3. Wait. The first build takes 10–30 minutes, and you can keep using your PC.
4. **Slippi account step.** The builder looks for a Slippi Launcher login on this PC.
   - If it finds one, it shows the display name and connect code and asks whether to put that account on the SD card. Type **Y** to add it.
   - If it doesn't find one, follow [Set up your Slippi account](#set-up-your-slippi-account) yourself.
   - If your SD card already has a **different** Slippi account, the builder keeps the one on the card.
5. If your 3DS's SD card is plugged in, the builder offers to copy everything. Otherwise, follow **"What to do next.txt"** in the folder that opens.

The builder works in its own folder, `C:\MeleeSlippiBuild`, separate from the regular `C:\MeleeBuild`.

**What it puts on the SD card:**

| Path | What it is |
|---|---|
| `cias/melee-slippi-beta.cia` | The HOME Menu app, **"Melee: Slippi Direct beta"** (green disc icon). Install it with FBI. |
| `3ds/melee-slippi/melee-slippi.3dsx` | The same beta for the Homebrew Launcher |
| `3ds/melee/files/` | The game files, **shared** with the regular game (nothing is duplicated) |
| `3ds/melee/slippi/files/` | Slippi's online menus, made from your disc, and Slippi's character-select art |
| `3ds/melee/slippi/user.json` | Your Slippi account, if you let the builder add it |

**Install:** on the 3DS, open **FBI → SD → cias → melee-slippi-beta.cia → Install CIA**. It appears on the HOME Menu as its own app, next to the regular Melee.

**Saves:**
- The beta keeps its own saves in `3ds/melee/saves/slippi/`.
- The first time it starts, it copies your regular game's saves (name tags, rules, records) into that folder. After that, the two never share saves.
- The beta never writes to the regular game's saves.

**Updating:** download the newer beta builder, build again, copy the "Copy to SD card" folder over, and reinstall the CIA in FBI.

---

## Set up your Slippi account

The 3DS plays online as **your Slippi account**, using the `user.json` file that Slippi Launcher creates when you log in.

**If the builder did not add it:**

1. On the PC where you are logged in to Slippi Launcher, find
   `%APPDATA%\Slippi Launcher\netplay\User\Slippi\user.json`. Paste that path into File Explorer's address bar.
2. Copy `user.json` to the SD card at `SD:/3ds/melee/slippi/user.json`. Create the `slippi` folder if needed.
3. On the 3DS, the online character select screen should now show your name and connect code under **User**.

**Important:**
- **`user.json` is your Slippi login.** Treat it like a password. Never post it, attach it to a bug report, or share it.
- The 3DS sends it **only** to Slippi's matchmaking server, which is what Slippi Dolphin does too. It is never written to `game.log`.
- **You and your opponent need different accounts.** If you play your friend on your own PC, they must log in to Slippi Launcher with *their* account.

---

## Play a match

1. Start **Melee: Slippi Direct beta** from the HOME Menu (or `melee-slippi` in the Homebrew Launcher).
2. From the main menu, choose **1-P Mode → Online Play → Direct**, where Slippi has it.
3. **Pick your fighter**, then press **START**.
4. **Type your friend's connect code** (for example `ABCD#123`) on the touch-screen keyboard, or with the Circle Pad / D-pad and A. Then press **OK**.
   - **L / R** go through codes you've used before.
5. Your friend, in Slippi Launcher, chooses **Online → Direct** and types **your** code. The top screen goes from *Searching* to *Connected* and shows their name.
6. Press **START** to **lock in**. When you have both locked in, the match starts.
7. **After each game** you return to the character select screen with the same opponent.
   - You can change fighter, chat, or press START again for the next game.
   - **The loser of the last game picks the stage.** If that's you, START takes you to the stage select screen first.

**On the character select screen:**

| Input | Action |
|---|---|
| START | Enter a code / lock in / (loser) choose the stage |
| D-pad | Quick chat: pick a page (Up, Left, Right, Down), then a message |
| Z | Cancel a search. **Hold Z** while connected to disconnect from your opponent. |
| Hold B | Back to the menus (disconnects) |
| Touch **CHAT** | Quick chat on the touch screen |
| Touch **SETTINGS** | Delay (1–9 frames) and chat on/off. Available before you search. |
| Touch **SHEIK / ZELDA** | Choose which one the Zelda icon plays |
| On the stage select screen, **Z** | Frozen Pokémon Stadium (no transformations) |

**During a match:** the controls are the regular port's (see the main README). Each player's name shows under their damage, along with the delay you're playing with. If the connection drops, **DISCONNECTED** appears, the match ends, and you return to the character select screen.

---

## How it differs from Slippi on a PC

### The big one: no rollback on the 3DS

Slippi on PC uses **rollback netcode**:
- each PC guesses what the other player is pressing and plays on immediately;
- when the real input arrives and the guess was wrong, Dolphin quietly re-runs the last few frames;
- so your own inputs always respond on time, and lag shows up as small "teleports".

**The 3DS can't do this.** Running Melee at full speed already uses almost all of its processor, so there is no room to re-run frames. The 3DS uses **delay-based (lockstep) netcode** instead:
- each frame, it waits for your opponent's real input for that frame, so it never needs to guess;
- your own inputs are delayed by your **delay setting** (2 frames by default, the same as Slippi's default);
- **if your opponent's input is late, the 3DS pauses for an instant until it arrives**, instead of rolling back. On a good connection this is rare: in testing, 0 to 1 waits per game on home Wi-Fi.
- On a poor connection (a phone hotspot with about 100 ms ping in testing), the games stay in sync but the 3DS pauses often and play feels choppy. The 3DS automatically allows a little more slack (up to 5 frames) when it keeps waiting.

**Your PC opponent still uses rollback** against the 3DS. For them it feels mostly like normal Slippi. When the 3DS pauses to wait, they may see brief rollbacks or slowdowns.

### Side by side

| | Slippi PC vs PC | 3DS vs PC (this beta) |
|---|---|---|
| Netcode | Rollback on both sides | 3DS: delay-based (waits). PC: rollback. |
| Your input delay | Your delay setting (default 2) | Your delay setting (default 2), plus a short wait when inputs are late |
| What lag looks like | Small teleports; your inputs stay responsive | Brief freezes on the 3DS; the PC may see rollbacks |
| Modes | Direct, Unranked, Ranked, Teams | **Direct 1v1 only** |
| Stage select | Loser picks; frozen Stadium | Same |
| Chat | Quick chat; custom messages set in Slippi Launcher | Quick chat. The 3DS cannot edit custom messages. |
| Replays | Saved by each PC | Saved by the PC only |
| Character select | Slippi's online CSS | Slippi's online CSS, plus touch-screen pages for codes, chat and settings |
| Loading a match | About 1 second | First game about 4–5 s, then usually under a second against the same opponent |
| VS splash screen | Yes | **No** (skipped to load faster) |
| Graphics | Dolphin (any resolution) | The 3DS port: native resolution, stereoscopic 3D, a few simplified effects |
| Controller | GameCube controller (analog triggers) | 3DS buttons (shoulder buttons are digital); remap them in the port's controls page |
| Frame rate | 60 FPS (59.94 Hz) | 60 game updates per second. The 3DS drops a single frame about every 12 seconds to stay in step with Dolphin's 59.94 Hz. |

### Other things you might notice

- **The PC waits for the 3DS when a match starts.** The 3DS loads files from the SD card more slowly than a PC, especially the first game. The PC sits at the start of the match until the 3DS is ready. Later games against the same opponent load from memory and start almost at once.
- **The gameplay is the same game.** The 3DS runs Slippi's online gameplay rules (UCF 0.84 and Slippi's online codes) on the same Melee engine, frame for frame. If the two ever disagree (a *desync*), the match ends with **DESYNC DETECTED**. That should never happen, so please [report it](#reporting-a-problem).
- **Performance.** 1v1 runs at full speed on New 3DS. If the 3DS ever renders below 60 FPS on a busy stage, the game itself still runs at full speed. Only the drawing slows down.

---

## Tips for a smooth connection

- **Sit close to your Wi-Fi router.** The 3DS's Wi-Fi is weaker than a PC's or phone's.
- **On a laggy connection, your opponent can raise *their* delay** in Slippi Launcher (for example to 3 or 4). Their delay is what hides the connection's lag from the 3DS, so this helps the 3DS more than changing yours.
- **Keep the 3DS delay at 2** unless the ping is very high (120 ms or more). The touch-screen hint says the same.
- **Same house?** That works too: Slippi tries your local network first.
- **Avoid phone hotspots** if you can. They work, but they're choppy.

---

## Troubleshooting

| What you see | What to do |
|---|---|
| **"No Slippi account"** on the character select screen | `user.json` is missing. See [Set up your Slippi account](#set-up-your-slippi-account). |
| **"user.json is damaged"** or **"user.json has no login"** | Log in to Slippi Launcher on your PC, then copy `user.json` to the SD card again. |
| **Online Play** goes straight to character select (no Direct menu), or the online menus look wrong | The Slippi menu files are missing from `SD:/3ds/melee/slippi/files/`. Copy the builder's "Copy to SD card" folder again. |
| **Searching** never finds your friend | Both of you must use Direct, each typing the **other's** code. Check for typos (`#` and the number). Make sure your friend's Slippi Launcher is up to date and logged in. |
| **"Failed to connect to mm server"** | The 3DS can't reach the internet. Check the 3DS's Wi-Fi in System Settings. |
| Finds your friend but never **connects** | Some home networks block direct connections (strict NAT), as with Slippi on PC. Try another network, or enable UPnP on the router. |
| **Choppy or freezing** gameplay | See [Tips for a smooth connection](#tips-for-a-smooth-connection). |
| **DISCONNECTED** in a match | The connection dropped. The match ends and you return to the character select screen; search again to rematch. |
| **DESYNC DETECTED** | A bug. Please [report it](#reporting-a-problem) with the log and the PC's replay. |
| Your old name tags are missing | The beta copies your saves only the first time it starts. Delete `SD:/3ds/melee/saves/slippi/` to copy them again on the next start. |

---

## Reporting a problem

Bug reports need facts. Right after the problem:

1. **Turn the 3DS off.** Don't start Melee again: the log is replaced each time the game starts.
2. Copy **`SD:/3ds/melee/game.log`** from the SD card.
3. Ask your opponent for the match's **replay** (`.slp`, in their `Documents\Slippi` folder) and their **Slippi Dolphin version**.
4. Open an issue with the **"Slippi beta problem"** form and attach both files.

**`game.log` is safe to post:**
- it never contains your login key;
- internet IP addresses are replaced with `public-ip`;
- it does contain your and your opponent's display names and connect codes.

**Never attach `user.json`.**

---

## Privacy and your account

- The 3DS talks to **Slippi's matchmaking server** (`mm.slippi.gg`), the same way Slippi Dolphin does, to find your opponent. It then connects **directly** to your opponent's computer.
  - As with any peer-to-peer game, your opponent's computer sees your IP address, and you see theirs.
- The 3DS sends your login key (`playKey`) **only** to Slippi's matchmaking server. It's never logged or sent anywhere else.
- The 3DS introduces itself to Slippi's server as a current Slippi Dolphin netplay version, because the server only matches compatible versions. Advanced users can change this with `app_version=` in `SD:/3ds/melee/slippi/config.ini`, for example if Slippi updates and matching stops working.
- The builder copies `user.json` only if you type **Y**. It stays on your PC and your SD card.

---

## Credits and licenses

- **[Project Slippi](https://github.com/project-slippi)** (Fizzi and contributors) designed Slippi's netplay protocol, online menus, chat, rules and art. This beta reimplements the 3DS side from Slippi Dolphin's source.
  - The online menu patches and `slpCSS.dat` come from [project-slippi/dolphin](https://github.com/project-slippi/dolphin) (GPL-2.0-or-later).
  - The builder downloads `slpCSS.dat` from there (pinned and hash-checked) and makes the menus from **your** disc.
  - **This is not an official Slippi product.**
- **[ENet](http://enet.bespin.org/)** (Lee Salzman, MIT license) is used for the network connection.
- The 3DS port and its credits are described in the main [README](../../../README.md) and [third-party notices](../THIRD_PARTY_NOTICES.md).
