# spc-echo — SNES S-DSP echo for the Korg minilogue xd

A delay effect (`delfx`) for the **Korg minilogue xd**, built with Korg's
[logue SDK v1](https://github.com/korginc/logue-sdk). It recreates the echo hardware
block of the **SNES SPC700 S-DSP** — a circular-buffer delay with an 8-tap FIR filter
and feedback.

- **File:** `spc-echo.mnlgxdunit`
- **Display name on the device:** `spcech`
- **Type:** `delfx` (user effect)

## Sound

The S-DSP echo runs every feedback pass through a fixed 8-tap FIR. With the
EarthBound / DOOM coefficients this gives a soft low-pass character: higher
frequencies fade faster with each repeat, so the tail turns dark and "smeared" —
exactly the classic 16-bit console delay.

## Controls

| Knob            | Function           | SNES equivalent        |
|-----------------|--------------------|------------------------|
| **TIME**        | Delay length 16 ms … 480 ms | `EDL` (× 16 ms) |
| **DEPTH**       | Wet/dry mix        | `EVOL`                 |
| **SHIFT+DEPTH** | Echo feedback      | `EFB`                  |

Feedback stays stable across its full range (0 … 1) because the FIR sum gain is
≈ 0.77, below unity.

## How it works

- **FIR filter:** 8 taps, hard-wired to the SNES coefficients
  `FA 0A 1A 13 13 1A 0A FA` (signed int8, /128).
- **Ring buffer:** L + R, 23040 floats each in SDRAM (~180 KB); FIR history
  (8 × 2 floats) kept in SRAM.
- **Sample rate:** 48 kHz. Stereo, processed in place.
- Core per frame:
  ```cpp
  const float rd   = s_buf[s_pos];          // read oldest sample
  fir_push(s_fir, rd);                       // shift history
  const float filt = fir_apply(s_fir);       // 8-tap FIR
  s_buf[s_pos] = clip1m1f(in + fb * filt);   // write feedback back
  if (++s_pos >= delay) s_pos = 0;
  x[0] = clip1m1f(in + depth * filt);        // output
  ```

## Building

Prerequisite: the logue SDK is cloned with its submodules and the Docker image is
built (see the [SDK README](https://github.com/korginc/logue-sdk)). This project lives
under `logue-sdk/platform/minilogue-xd/spc-echo/`.

From the `logue-sdk` directory:

```sh
docker/run_cmd.sh build minilogue-xd/spc-echo
```

The resulting `spc-echo.mnlgxdunit` ends up in the project folder.

## Loading onto the device

1. Open the **minilogue xd Sound Librarian**, drag `spc-echo.mnlgxdunit` onto a free
   slot under *User Effects* and transfer it.
2. Select the effect in the delay slot on the device.
3. Use TIME, DEPTH and SHIFT+DEPTH as described above.

minilogue xd firmware ≥ 2.00 is required for API 1.1-0.

## License

Private hobby project. The FIR coefficients come from documented SNES games and are
used here to emulate the historical S-DSP echo.
