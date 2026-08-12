# SDLPal for M5Stack Tab5

ESP-IDF port of [SDLPal](https://github.com/sdlpal/sdlpal) for the ESP32-P4
based M5Stack Tab5. The current firmware runs the Windows 95 edition of PAL in
landscape orientation with touch controls, SD-card saves, sound effects and
RIX/OPL music.

Game data is copyrighted and is not included. Use resource files from a copy of
the Windows 95 game that you own.

## Current status

- M5Stack Tab5 revision 2, ESP32-P4 revision 1.3 tested
- 320x200 PAL framebuffer scaled and rotated to the 1280x720 display
- touch D-pad plus confirm, cancel, item/repeat, force and auto-battle buttons
- Windows 95 resources loaded from a FAT32 SD card
- save and load support in the same SD-card directory
- ES8388 mono output at 44.1 kHz
- Windows sound effects and RIX music through the DOSBox integer OPL core
- SDL thread stack allocated in PSRAM; audio kept on core 1

## Toolchain

The validated toolchain is ESP-IDF 6.0.1. On the development Mac used for this
port, activate it in every new shell with:

```sh
source /Users/flex/.espressif/tools/activate_idf_v6.0.1.sh
```

For another machine, install ESP-IDF 6.0.1 and use that installation's normal
`export.sh` or activation script.

## SD card

Format the card as FAT32 with an MBR partition table. Put the game files in
`/pal` on the card:

```text
/pal/
├── abc.mkf
├── ball.mkf
├── data.mkf
├── f.mkf
├── fbp.mkf
├── fire.mkf
├── gop.mkf
├── map.mkf
├── mgo.mkf
├── pat.mkf
├── rgm.mkf
├── rng.mkf
├── sss.mkf
├── word.dat
├── sounds.mkf
└── mus.mkf
```

The first 14 files are the core Windows 95 resource set. `sounds.mkf` supplies
sound effects and `mus.mkf` supplies RIX music. Save files and SDLPal's
configuration are written back to `/pal`.

The firmware checks every required file and performs an SD write/readback probe
before starting the engine. If the resource set is incomplete, it stays on a
diagnostic screen instead of launching the game.

## Controls

| On-screen control | Action |
| --- | --- |
| D-pad | Walk or move the selection |
| A | Confirm, search or interact |
| B | Cancel or open the menu |
| X | Use item outside battle; repeat the previous command in battle |
| Y | Force/defend action supported by SDLPal |
| AUTO | Toggle automatic battle |

The D-pad has enlarged touch targets and supports sliding directly from one
direction to another while the finger remains down.

## Build and flash

After activating ESP-IDF:

```sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/cu.usbmodem101 flash monitor
```

Replace the serial device with the one assigned to your Tab5. Exit the serial
monitor with `Ctrl+]`.

For a clean reproducibility check:

```sh
idf.py fullclean
idf.py build
```

`sdkconfig.defaults` and `dependencies.lock` are kept in Git. Generated
`sdkconfig`, `build` and `managed_components` directories are ignored.

## Board-specific reliability settings

The official `espressif/m5stack_tab5_noglib` 1.2.0~1 package remains unmodified.
Board-specific reliability policy lives in project code:

- `main/resource_check.c` caps SDMMC at 20 MHz to avoid intermittent MKF short
  reads with the tested card.
- the SDL Tab5 adapter retries transient PI4IOE5V6408 initialization failures
  before display startup.
- the adapter owns the PAL PPA presentation path and explicitly synchronizes
  untouched black margins in PSRAM before LCD DMA reads them.
- a project touch bridge normalizes Tab5 coordinates and supplies valid SDL3
  finger events, working around the pinned SDL component's ESP-IDF backend.

## Repository layout

```text
components/georgik__sdl_bsp/       Tab5-specific SDL display/touch adapter
components/sdlpal/                 SDLPal engine, platform port and touch UI
main/                              startup and SD resource diagnostics
docs/porting-plan.md                completed milestones and remaining checks
```

## Remaining validation

The current firmware is a playable baseline. Before calling it a stable release,
exercise a longer session covering field movement, menus, battle, scene changes,
save, reboot and load while watching the serial log for resets, SD errors and
audio write failures.
