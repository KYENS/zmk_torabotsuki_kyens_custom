# Cirque TM040040 trackpad (cirque-trackpad branch)

This branch adds a Cirque TM040040 trackpad on the 6-pin sensor port, connected over I2C through the adapter board. The trackball builds, the trackball's `pointer_accel` and the keymap are unchanged.

## Which files to flash

| Setup | Right half | Left half |
|---|---|---|
| Trackpad on the right, replacing the trackball | `torabo_tsuki_lp_cirque_right_central` | `torabo_tsuki_lp_left_peripheral` |
| Trackpad on the left, trackball stays on the right | `torabo_tsuki_lp_cirque_left_ball_right_central` | `torabo_tsuki_lp_cirque_left_peripheral` |

To flash: turn the power switch off, then hold P (right half) or Q (left half) while plugging in USB. Copy the `.uf2` file onto the `BLEMICROPRO` drive. If the halves won't pair after switching, flash `settings_reset` to both halves first.

## Gestures

| Do this | Get this |
|---|---|
| Slide one finger | Cursor moves, with smoothing and a trackpad acceleration curve |
| Flick and lift | Cursor stops (inertia is off; set `glide-decay` to 850–950 to turn it on) |
| Tap anywhere | Left click |
| Tap the edge at about 2 o'clock (between 1 and 3) | Right click |
| Start in the outer edge band (outer quarter of the pad) and slide around it | Scroll. Clockwise scrolls down, counter-clockwise scrolls up. One full circle is about 12 notches. The band adapts to how far your pad actually reports. |
| Start on the edge, then move inward | Normal cursor movement |
| Hold the scroll layer and slide | Scroll in the natural direction, like a laptop touchpad |

The scroll layer depends on where the trackpad is:
- Trackpad on the right: layer 2 (left thumb, `&mo 2`).
- Trackpad on the left: layer 1 (right thumb, `&lt 1 ENTER`).

Any new touch, tap or click stops a glide.

## How it works

- The driver lives in this repo, in `drivers/cirque/`. It is copied from `billyjacoby/cirque-input-module` and fixed so the trackpad can't stop working after a failed read; it also has a watchdog. See `drivers/cirque/README.md`. It runs the pad in **absolute mode**, so it reports where the finger is, which circle scroll and the right-click zone need.
- The pad's own sleep mode is never used: neither `sleep;` (it checked for a finger only every 255 ms, so the first move after a pause was laggy) nor the idle sleeper (ZMK only counts key presses as activity, so it switched the pad's sleep on during trackpad-only use). The pad is switched off when the keyboard goes into deep sleep.
- The driver's watchdog re-arms the data-ready interrupt if it gets stuck, and every 2 s checks that the pad still has its settings. If the pad has reset itself, the watchdog reconfigures it.
- `src/tp_gestures_core.h` is the gesture logic. It has no Zephyr dependencies and is tested on a PC with simulated taps, flicks and circles.
- `src/tp_gestures.c` is the ZMK input processor that wraps it. It must be **first** in a listener's processor list.
- Smoothing uses a 1€ ("One Euro") filter. Acceleration is built into the same processor and uses one smoothed speed for both axes, so the gain doesn't flicker between reports and diagonals don't wobble. The trackball's `pointer_accel` isn't used for the trackpad.

## Tuning

All settings are in `snippets/input-trackpad-cirque/tp_processors.dtsi`. Edit, push, and Actions rebuilds.

| Problem | Change |
|---|---|
| Left/right reversed | Toggle `invert-x;` |
| Up/down reversed | Toggle `invert-y;` |
| Cursor too slow or too fast overall | `cursor-scale` (%) |
| Slow, precise moves too slow or too fast | `accel-min` (per-mille) |
| Fast moves too slow or too fast | `accel-max`, `accel-max-speed` |
| Cursor drifts while your finger rests | Raise `rest-threshold` (try 8–12) |
| Cursor jittery when moving slowly | Lower `filter-min-cutoff-mhz` (try 500–700) |
| Cursor jittery when moving fast | Lower `filter-beta` (try 5) |
| Cursor feels laggy | Raise `filter-beta` (try 20) or `filter-min-cutoff-mhz` |
| Glide too long or too short | `glide-decay` (950 is long, 850 is short, 0 is off) |
| Glide triggers too easily | Raise `glide-min-speed` |
| Taps not registering | Raise `tap-ms` or `tap-travel` |
| Accidental clicks | Lower `tap-ms` or `tap-travel` |
| Right-click zone in the wrong place | `right-click-clock` (1–12) and `right-click-width-deg`. Set `right-click-clock` to 0 to turn the zone off. |
| Right-click too hard or too easy to hit | `right-click-reach-pct` (lower = bigger zone) |
| Circle scroll too fast or too slow | `circle-units-per-turn` (15 = 1 notch) |
| Circle scroll goes the wrong way | Uncomment `circle-invert;` |
| Circle scroll hard to start | Lower `circle-reach-pct` (try 65) or `circle-start-deg` (try 15) |
| Circle scroll triggers by accident | Raise `circle-reach-pct` (try 82) or `circle-start-deg` |
| Cursor floats on its own / moves before you touch | Raise `absolute-mode-z-min` in `input-trackpad-cirque.overlay`. The debug log's `z=` shows your real touch strength. |
| Scroll-layer speed | `zip_scroll_scaler 1 4` in the listener snippet |
| Trackpad stops responding | Flash a `_debug` build. `abs read failed`, `DR stuck` or `pad lost its configuration` lines mean the driver hit a problem and recovered. If they appear often, check the solder joints, the adapter's 4.7 kΩ pull-ups and the VCC wire. A pad that resets itself points to a power dip. |

## Debug builds

`torabo_tsuki_lp_cirque_right_central_debug` and `torabo_tsuki_lp_cirque_left_ball_right_central_debug` work like the normal right-half builds, but they log one line per trackpad touch over USB. In these builds, ZMK Studio over USB isn't available.

1. Flash the debug build to the right half and leave the USB cable plugged in.
2. Open the keyboard's COM port in a serial terminal, such as PuTTY or the Arduino serial monitor. Any baud rate works.
3. Look for lines like this:

   ```
   touch: tap/move start=(700,300) r=268 dur=120ms frames=12 (10ms/frame) max_disp=9 glide=0 | pad seen x 40..990 y 35..985
   ```

What the fields mean:

| Field | Meaning |
|---|---|
| `tap/move` / `move` / `ring?` / `circle` | How the touch was classified |
| `r` | Distance of the touch start from the pad centre. The edge is about 512. Circle scroll and right-click need at least `circle-ring-inner`. |
| `ms/frame` | How often the pad reports. It should be about 10. |
| `max_disp` | How far the finger moved from touch-down. A tap needs `tap-travel` or less. |
| `reach` | How far from the centre your pad reports. This is learned automatically; the circle-scroll band is `circle-reach-pct` of it. |
| `z` | Touch strength (min..max, average). Set `absolute-mode-z-min` comfortably below a light touch's minimum. |
| `rest_jitter` | Average noise (pad units) while your finger rests. `rest-threshold` should be about 2–3× this. |
| `pad seen` | The coordinate range seen since boot. Sweep around the edge once to see your pad's full reach. |

## Hardware

- On the trackpad, remove R1 (470 kΩ) to put it in I2C mode.
- On the adapter, fit R1 and R2 (4.7 kΩ). The trackpad has no I2C pull-ups of its own.
- Wiring, adapter → trackpad pad:
  - GND → GND
  - SDA → SDA
  - SCL → SCL
  - DR → DR
  - VCC → VDD
