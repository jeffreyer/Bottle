# Bottle Script Draft

This is the developer-facing module language target. The first reference module is:

- `modules_dev/rhythm_spectrum/manifest.json`
- `modules_dev/rhythm_spectrum/main.bottle`

## Module Shape

Every script module exports three lifecycle functions:

```bottle
fn setup(ctx) {
}

fn loop(ctx) {
}

fn unload(ctx) {
}
```

The device owns frame timing. `loop(ctx)` is called once per frame.

## Context

Available context fields:

```bottle
ctx.led.clear()
ctx.led.set(x, y, color)
ctx.led.show()

ctx.sensor.spectrum
ctx.sensor.accel

ctx.config.get("key")
ctx.config.set("key", value)

ctx.time.ms
```

## Builtins

```bottle
rgb(r, g, b)
hsv(h, s, v)
blend(color_a, color_b, amount)
min(a, b)
max(a, b)
```

## State

Persistent module state is declared explicitly:

```bottle
state color_phase = 0
state peaks[17] = 0
```

The compiler maps `state` into the module memory block declared by the manifest resource budget.

## Safety Limits

The VM should enforce:

- Maximum instructions per frame.
- Fixed state memory size.
- No direct heap allocation.
- No direct hardware access.
- Read-only sensor snapshots.
- LED writes clipped to panel bounds.

## Packaging

Developer source is packaged as:

```text
module.pkg
- header
- manifest.json
- bytecode
- optional assets
```

## Current Device Runtime

The current firmware mounts the `modules` partition, installs the default rhythm spectrum script if missing, reads `/rhythm_spectrum/main.bottle`, and executes it through Bottle VM v0.

Bottle VM v0 is intentionally narrow:

- It reads script directives from user storage.
- It supports the `spectrum` effect.
- It applies script-configured smoothing, gain, peak decay, color phase timing, style key, and palettes.
- It uses real sensor snapshots and LED output.

It does not yet parse the full high-level language shown in earlier sketches. The `.bottle` file is currently a declarative DSL that maps cleanly to the first VM opcodes. Future versions can compile richer source into bytecode.
