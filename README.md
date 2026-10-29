# SkyrimNet MiniGame 0.1.0

Play table games with the people around you. Open the menu, pick a game and your opponents
from the NPCs nearby, and play on a board drawn over the game. SkyrimNet gives your opponents
a voice: they talk about the game as it goes, know how it stands, and remember it afterwards.

Two games so far:

- **Reversi** (Othello) against one NPC, at four strengths.
- **Liar's Dice** against one to three NPCs. Everyone hides five dice under a cup; in turn you
  raise the bid on how many dice around the table show a face, or call the last bid a lie.

When a game starts, your opponents come over to stand in front of you, and their faces are
photographed for the frames on the board.

## Requirements

| Mod | Notes |
|---|---|
| Skyrim Special Edition / Anniversary Edition | Built and tested on 1.6.1170. |
| [SKSE64](https://skse.silverlock.org/) | |
| [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) | |
| [Prisma UI](https://www.nexusmods.com/skyrimspecialedition/mods/148718) | Draws the board. Tested on 1.5.1. |
| [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin) | Tested on beta 28. The games also run without it, but then nobody talks. |
| [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) | |
| [MCM Helper](https://www.nexusmods.com/skyrimspecialedition/mods/53000) | The settings menu and the hotkey. |

## Installation

Install with your mod manager and enable `skyrimnet_MiniGame.esp`. It is ESL-flagged and needs
no load-order slot. It can be added to an existing save.

Japanese players: also install the separate Japanese translation archive and let it overwrite
this one. In any other game language the menus and the board are in English.

## How to play

Press the hotkey (**K** by default, set in the MCM) to open the menu.

1. Pick a game.
2. Pick your opponents from the people nearby, nearest first. The one under your crosshair is
   picked to begin with. Liar's Dice takes up to three.
3. The screen fades to black, your opponents step up in front of you, and the game begins.

Press the hotkey again or **Esc** to close the board. A game in progress waits: it is offered at
the top of the menu the next time you open it.

### Reversi

You and your opponent take turns placing discs; every line of the other color you close off is
flipped to yours. The player with more discs when the board is full wins. You can resign.

### Liar's Dice

- Everyone has five dice under a cup. Ones are wild: they count as any face.
- In turn, raise the bid ("five dice showing four") on the dice of the whole table, or call the
  last bid a lie.
- The cups come up. If the bid was too high, the bidder loses a die; if it held, the caller does.
- Whoever loses their last die is out. The last one with dice wins.
- If you are out first, the others play on to the end while you watch. *Stop watching* leaves
  the table at once.

The current bid is shown in the middle of the table. Every bid and call is spoken by one person
at the table, and the game waits for each line before the next person moves.

## SkyrimNet

- **Commentary.** Each moment of a game is told to SkyrimNet as a narration from one opponent,
  who reacts to it in their own words: the start, the moves worth a remark, the end (declaring
  victory or conceding defeat).
- **They know the game.** While they play, each opponent's prompt holds the state of the game
  (the `minigame_state` decorator, used by the bundled prompt `0450_minigame.prompt`). In Liar's
  Dice an opponent sees only their own dice, keeps them secret and may bluff.
- **You speak your own bids** (Liar's Dice, MCM option). Your bids and "Liar!" calls go through
  SkyrimNet's Transform Dialogue, so your character says them aloud in their own words.
- **Challenges.** An NPC may challenge you to Reversi in conversation (the action
  `Reversi_Challenge`). The board opens with the challenge, and you can accept or decline.
- Opponents are marked busy in SkyrimNet while a game is played, so they are not pulled into
  other conversations.

## MCM settings

| Setting | Default | |
|---|---|---|
| Open / close the menu | K | The hotkey. |
| Opponent search range | 1500 | How far away people can be to appear in the opponent list, in game units (70 is about a metre). |
| Call opponents over | On | Opponents come to stand in front of you for the photo in their face frames. Off: nobody moves, and only faces already on screen are shown. |
| Pause the game while playing | Off | NPCs cannot speak while the game is paused, so leave this off to hear them. |
| Reversi: opponent strength | 2 | 1 Beginner, 2 Normal, 3 Strong, 4 Master. |
| Reversi: your color | Black | Black, White, Random or Alternate. Black moves first. |
| Reversi: show legal moves | On | |
| Liar's Dice: opponent strength | 2 | Stronger opponents count the odds better and read your bids. |
| Commentary | On | Off: the games are silent. |
| Commentary interval | 8 s | Reversi only: at most one remark on an ordinary move per this many seconds. |
| Voice wait | 15 s | Liar's Dice waits for each line to be spoken; if nobody speaks for this long, the game moves on anyway. |
| You speak your own bids | On | Liar's Dice: your bids and calls go through Transform Dialogue (one LLM request each). Off: only your opponents speak. |
| NPCs may challenge you | On | The `Reversi_Challenge` action. |

## Notes and limits

- **Liar's Dice takes its time.** Every bid is spoken before the next one, so a game of four
  players can last a while, longer still once you are out and only NPCs play.
- **The face photo** is a screenshot of the game: the board steps aside for a moment and the HUD
  is hidden. It is saved as `PrismaUI/views/skyrimnet_MiniGame/face_N.jpg` under Data (in Mod
  Organizer, in the overwrite folder) and deleted when the next one is taken or Skyrim
  starts. An opponent whose face cannot be seen (behind someone, off screen) gets their
  initial instead.
- **Seated NPCs** get up to come over, and go back to what they were doing after the game.
- If your character's lines leave the table waiting for the full *Voice wait* each time, turn
  *You speak your own bids* off and report it.

## Uninstalling

Close the board, save, and remove the mod.

## Translating

- Board, commentary and messages: `SKSE/Plugins/skyrimnet_MiniGame/strings.json` (UTF-8). A
  missing key falls back to English.
- MCM: `Interface/Translations/skyrimnet_MiniGame_<LANGUAGE>.txt` (UTF-16 LE with BOM). The mod
  ships the English texts under every language name.

## Building

The repository holds the source; the release archives hold the built mod.

| Folder | |
|---|---|
| `src/` | The SKSE plugin (C++23, [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng)). |
| `view/` | The Prisma UI view: the menu, both games and their AI (one HTML file). |
| `translations/` | The English MCM texts (`mcm_en.tsv`). |
| `tools/` | Node scripts that export the view's texts to `strings.json`, write the MCM translation files and check a translation against the English keys. |
| `plugin/` | `skyrimnet_MiniGame.esp` as YAML (a quest that runs the MCM and the hotkey). |
| `assets/` | The MCM config, the Papyrus sources and the SkyrimNet prompt bundle. |

You need [xmake](https://xmake.io/) 3.0 or later, Visual Studio 2022 and Node.js. Point
`COMMONLIB_SSE_NG` at a CommonLibSSE-NG checkout, then run `xmake build`. The DLL and the view
are laid out in `dist/`. `build.ps1` is the author's script that also copies them, with the
texts, into the mod project.

## Files

- `skyrimnet_MiniGame_0.1.0.zip`: the mod (English, usable with any game language).
- `skyrimnet_MiniGame_0.1.0_Japanese.zip`: Japanese translation; install after the mod and let
  it overwrite.

## License

MIT, see [LICENSE](LICENSE). The SkyrimNet API headers in `src/skyrimnet/` are MIT licensed by
Fateless.Ai (see the LICENSE there). `src/PrismaUI_API.h` is the Prisma UI modder API header.
