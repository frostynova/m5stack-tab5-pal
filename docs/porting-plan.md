# SDLPal Windows 95 port status

## Validated baseline

- Target: M5Stack Tab5 / ESP32-P4 revision 1.3
- Toolchain: ESP-IDF 6.0.1
- Board support: `espressif/m5stack_tab5_noglib` 1.2.0~1
- Graphics: `georgik/sdl` 3.3.7~3 plus a Tab5-specific SDL BSP adapter
- Game data: user-owned Windows 95 PAL files on FAT32 SD

## Completed milestones

### Display and orientation

- ST7121/ST7123 display path initializes on the connected Tab5 revision.
- RGB565 colors and backlight are correct.
- PAL's 320x200 framebuffer is scaled, rotated and centered in landscape.
- The opening animation and game view have the correct orientation and extent.

### SD resources and saves

- The SD card mounts at `/sdcard` and PAL resources are read from
  `/sdcard/pal`.
- Startup checks the 14-file Windows 95 core resource set.
- A write, sync and readback probe runs before engine startup.
- Save slots persist on the SD card and load after reboot.
- SDMMC is limited to 20 MHz and MKF reads reject short transfers.

### Engine and controls

- The Windows 95 edition is detected at runtime; no compile-time `PAL_WIN95`
  fork is used.
- Touch coordinates apply the inverse display transform.
- A project-owned bridge normalizes panel coordinates and emits valid SDL3
  finger events instead of relying on the incompatible managed SDL backend.
- A translucent D-pad supports held movement and sliding between directions.
- A/B/X/Y/AUTO map to SDLPal's existing logical input actions without changing
  game rules.
- The physical side margins contain battery status and 10% volume controls,
  with independent raw-panel touch hit testing outside the PAL image.

### Audio and runtime memory

- ES8388 output is 44.1 kHz, mono, 16-bit.
- Windows sound effects and RIX/OPL music are enabled.
- OPL synthesis uses the DOSBox integer core at the device sample rate.
- Audio runs on core 1 with internal DMA/mixing buffers.
- SDL's 1 MiB thread stack is allocated in PSRAM, leaving ample headroom for the
  status and ending paths that place two 64 KiB decode buffers on the stack.
- The tested firmware loads an existing save and runs without the previous
  early-game watchdog/reset failure.

### Battery and charging

- The onboard INA226 at `0x41` reports battery voltage and signed shunt current
  through the board's 5 milliohm measurement path.
- The user-facing sign convention is positive for charging and negative for
  discharging; the displayed percentage is explicitly a voltage-curve estimate.
- The second power expander preserves unrelated outputs while enabling QC
  negotiation and then `CHG_EN`, matching M5Stack's reference sequence.
- On the tested low battery, charging settled at approximately +0.31 A and the
  measured voltage increased after charger initialization.

## Reproducibility boundary

Copyrighted game data is never committed. The source tree pins build inputs.
The official Tab5 BSP remains managed and unmodified; the tested SD-speed policy
and I2C-expander retry both live in project-owned code.

Acceptance for the baseline repository is:

1. remove generated `build` and `managed_components` directories;
2. resolve dependencies from `dependencies.lock`;
3. build the firmware from source;
4. confirm CMake selects the pinned managed `m5stack_tab5_noglib` component;
5. flash and reach the title/save path with display, touch, SD and audio active.

## Remaining validation

Run at least a 30-minute play session containing:

1. field movement and repeated direction changes;
2. menus, item use and cancellation;
3. at least one battle, including repeat and auto-battle;
4. a scene or map transition with music and sound effects;
5. save, reboot and load of the new save;
6. serial-log review for panic, watchdog, SD short read or audio write errors.

Optional work after that baseline passes includes runtime brightness control
and a way to hide the touch overlay. These are polish items, not requirements
for the first playable release.
