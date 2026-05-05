# Module Runtime Plan

## Decision

Use a hybrid update model:

- OTA updates are for the core firmware, native drivers, BLE protocol, built-in modules, and performance-critical official modules.
- Third-party marketplace modules should use a controlled script or bytecode runtime stored as module packages.

This keeps the device safe and flexible. ESP32 cannot safely hot-load arbitrary C++ modules like a desktop plugin system, and allowing third-party native code would make crashes, memory corruption, hardware abuse, and compatibility problems much harder to control.

## Why Not OTA For Every Module

OTA is useful, and this project already has `app0` and `app1` OTA partitions. It is the right tool for system updates.

It is not ideal for a public module market because:

- Every installed module would require replacing the whole firmware.
- Third-party code would need to be compiled against exact firmware internals.
- A bad module can crash the whole device or abuse hardware directly.
- Removing one module means generating and flashing another firmware.
- The WeChat mini program would need a firmware build/release pipeline, not just a module install flow.

## Why Use Script Or Bytecode Modules

Third-party modules should run inside a limited runtime that exposes only approved APIs:

- LED framebuffer drawing
- Microphone spectrum data
- Acceleration/gravity data
- Time/random helpers
- Key-value module settings
- Optional touch events

This lets developers build visual modules without direct access to BLE, flash partitions, sensors, or low-level hardware.

## Recommended Module Package

A module package should be a small signed binary bundle, transferred by the mini program and stored in the `modules` data partition.

Recommended file format:

- Header: magic, package version, total length, CRC/signature, module id, runtime id.
- Manifest JSON length + manifest JSON.
- Bytecode/script length + bytecode/script.
- Optional asset table and small assets.

The source format developers write should not be uploaded directly. The mini program or marketplace backend should compile/package it first.

A package should contain:

- `manifest.json`
- `main` script or bytecode
- optional small assets or palettes

Manifest fields:

- `id`
- `name`
- `version`
- `author`
- `description`
- `runtime`
- `entry`
- `permissions`
- `configs`
- `resources`

Example permissions:

- `led.draw`
- `sensor.accel`
- `sensor.spectrum`
- `storage.kv`
- `input.touch`

## Runtime API Shape

Minimum API exposed to modules:

- `led.clear()`
- `led.set(x, y, r, g, b)`
- `led.show()`
- `sensor.accel()` returning `{x,y,z}`
- `sensor.spectrum()` returning an array matching panel width or configured bin count
- `config.get(key)`
- `config.set(key, value)`
- `time.ms()`
- `random(min, max)`

The device owns frame timing. Modules should implement something like:

- `setup(ctx)`
- `loop(ctx)`
- `unload(ctx)`

## Native Prototype

The first audio spectrum module has been rewritten as a runtime-style native prototype:

- `include/rhythm_module.h`
- `src/rhythm_module.cpp`
- `include/module_runtime.h`
- `src/module_runtime.cpp`
- `include/rhythm_spectrum_user_module.h`
- `src/rhythm_spectrum_user_module.cpp`

It still compiles as C++, but it is split like the planned module model:

- `rhythm_module.cpp` is the host bridge. It owns microphone capture, FFT, gravity snapshots, and lifecycle.
- `module_runtime.cpp` is the host API shape that a future bytecode VM will expose.
- `rhythm_spectrum_user_module.cpp` is intentionally written like future developer code: `setup(ctx)`, `loop(ctx)`, and `unload(ctx)`.
- Audio FFT is exposed as `ctx->sensor.spectrum`.
- Gravity is exposed as `ctx->sensor.gravity`.
- Rendering goes through `ctx->led.clear()`, `ctx->led.set()`, and `ctx->led.show()`.
- Config style goes through `ctx->config.style()`.

This prototype is the reference shape for the later bytecode VM host API.

## Developer-Facing Script Shape

The intended final source form is now represented by:

- `modules_dev/rhythm_spectrum/manifest.json`
- `modules_dev/rhythm_spectrum/main.bottle`
- `docs/bottle_script.md`

This `.bottle` source is what a module developer should write. It is not meant to be interpreted directly as text on the ESP32. The marketplace toolchain should compile it into bytecode, then package it with its manifest into `module.pkg`.

Current firmware milestone:

- The `modules` SPIFFS partition is mounted at boot.
- A default rhythm spectrum module is installed into `/rhythm_spectrum` if missing.
- The device reads `/rhythm_spectrum/main.bottle` from user space.
- Bottle VM v0 executes a narrow declarative script subset for the spectrum effect.
- This proves the storage/read/execute path before the richer bytecode compiler exists.

The existing C++ files are now reference host/native implementations:

- `src/rhythm_module.cpp`: host bridge for audio FFT, gravity snapshots, lifecycle.
- `src/module_runtime.cpp`: native version of the host API.
- `src/rhythm_spectrum_user_module.cpp`: C++ reference implementation matching `main.bottle`.

## Recommended Runtime

Prefer a tiny custom bytecode or WASM-like stack VM over embedding a full JavaScript/Lua runtime.

Best first version:

- A small bytecode VM designed for LED effects.
- Fixed instruction budget per frame.
- Fixed memory budget per module.
- No dynamic allocation inside module loop.
- Numeric arrays only.
- Host-provided sensor snapshots.

Developer experience can still be friendly:

- Developers write a small TypeScript-like or Python-like DSL.
- The marketplace backend compiles it into device bytecode.
- The device only verifies and runs bytecode.

Avoid full JavaScript on the device for the first version:

- It is heavier in RAM/flash.
- Garbage collection can cause frame jitter.
- Exposing safe native bindings is more complex.

Avoid MicroPython for marketplace modules:

- It is powerful but heavy.
- It needs a larger runtime and careful sandboxing.
- It is better suited for developer firmware, not small user-installed visual modules.

Lua is possible, but still heavier than a purpose-built effect VM. It may be a reasonable second choice if fast development matters more than smallest runtime size.

## Update Strategy

Use OTA for:

- Firmware runtime upgrades
- BLE protocol upgrades
- Sensor driver changes
- Official native modules

Use module packages for:

- Community visual effects
- New palettes
- Sensor-reactive LED scenes
- Small games that fit the runtime API

## Near-Term Implementation Steps

1. Finish module manifest protocol over BLE.
2. Add module package storage in the `modules` SPIFFS or LittleFS partition.
3. Define module package install/delete/list commands.
4. Add a tiny runtime with strict resource limits.
5. Expose read-only microphone spectrum and acceleration snapshots.
6. Add module crash/timeout protection and fallback to a built-in module.
7. Keep OTA for runtime and built-in module upgrades.

## Notes

Current partition table has dual OTA app slots plus one large `modules` data partition for user-installed module packages. Module packages should still stay compact because the same space must hold manifests, bytecode, assets, and installed-module metadata.

Updated target partition strategy:

- `app0`: OTA slot, about current firmware size + 30% reserve.
- `app1`: second OTA slot, same size.
- `modules`: user-installed module packages.

With current firmware around 802KB, each OTA slot can be about `0x110000` bytes. The remaining user space is assigned to the `modules` partition.
