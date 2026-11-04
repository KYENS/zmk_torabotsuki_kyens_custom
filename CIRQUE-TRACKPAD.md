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
| Flick and lift | Cursor keeps gliding and slows down. Stopping before you lift gives no glide. |
| Tap anywhere | Left click |
| Tap the edge at about 2 o'clock (between 1 and 3) | Right click |
| Start on the outer edge and slide around it | Scroll. Clockwise scrolls down, counter-clockwise scrolls up. One full circle is about 12 notches. |
| Start on the edge, then move inward | Normal cursor movement |
| Hold the scroll layer and slide | Scroll in the natural direction, like a laptop touchpad |

The scroll layer depends on where the trackpad is:
- Trackpad on the right: layer 2 (left thumb, `&mo 2`).
- Trackpad on the left: layer 1 (right thumb, `&lt 1 ENTER`).

Any new touch, tap or click stops a glide.

## How it works

- The driver is `billyjacoby/cirque-input-module` (pinned). It runs the pad in **absolute mode**, so it reports where the finger is, which circle scroll and the right-click zone need.
- The pad's own sleep (`sleep;`) is off. It checked for a finger only every 255 ms, so the first movement after a short pause was laggy. The pad still sleeps when the whole keyboard is idle.
- `src/tp_gestures_core.h` is the gesture logic. It has no Zephyr dependencies and is tested on a PC with simulated taps, flicks and circles.
- `src/tp_gestures.c` is the ZMK input processor that wraps it. It must be **first** in a listener's processor list.
- `tp_accel` is a separate acceleration curve for the trackpad.

## Tuning

All settings are in `snippets/input-trackpad-cirque/tp_processors.dtsi`. Edit, push, and Actions rebuilds.

| Problem | Change |
|---|---|
| Left/right reversed | Toggle `invert-x;` |
| Up/down reversed | Toggle `invert-y;` |
| Cursor too slow or too fast overall | `cursor-scale` (%), or `tp_accel` `min-factor` / `max-factor` |
| Cursor shaky | Lower `smoothing` (50 is smoothest) |
| Cursor feels laggy | Raise `smoothing` (100 is off) |
| Glide too long or too short | `glide-decay` (950 is long, 850 is short, 0 is off) |
| Glide triggers too easily | Raise `glide-min-speed` |
| Taps not registering | Raise `tap-ms` or `tap-travel` |
| Accidental clicks | Lower `tap-ms` or `tap-travel` |
| Right-click zone in the wrong place | `right-click-clock` (1–12) and `right-click-width-deg`. Set `right-click-clock` to 0 to turn the zone off. |
| Circle scroll too fast or too slow | `circle-units-per-turn` (15 = 1 notch) |
| Circle scroll goes the wrong way | Uncomment `circle-invert;` |
| Circle scroll triggers by accident | Raise `circle-ring-inner` (closer to the edge; max 512) or `circle-start-deg` |
| Scroll-layer speed | `zip_scroll_scaler 1 4` in the listener snippet |

## Hardware

- On the trackpad, remove R1 (470 kΩ) to put it in I2C mode.
- On the adapter, fit R1 and R2 (4.7 kΩ). The trackpad has no I2C pull-ups of its own.
- Wiring, adapter → trackpad pad:
  - GND → GND
  - SDA → SDA
  - SCL → SCL
  - DR → DR
  - VCC → VDD
