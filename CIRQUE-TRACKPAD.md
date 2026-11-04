# Cirque TM040040 trackpad (cirque-trackpad branch)

This branch adds a Cirque TM040040 trackpad on the 6-pin sensor port, connected over I2C through the adapter board. Everything else in this repo stays as it is: the pointer acceleration, the layer-2 scroller and the keymap.

## Which files to flash

| Setup | Right half | Left half |
|---|---|---|
| Trackpad on the right, replacing the trackball | `torabo_tsuki_lp_cirque_right_central` | `torabo_tsuki_lp_left_peripheral` |
| Trackpad on the left, trackball stays on the right | `torabo_tsuki_lp_cirque_left_ball_right_central` | `torabo_tsuki_lp_cirque_left_peripheral` |

To flash, turn the power switch off, then hold P (right half) or Q (left half) while plugging in USB, and copy the `.uf2` file onto the `BLEMICROPRO` drive. If the halves won't pair after switching, flash `settings_reset` to both halves first.

## How it behaves

**Trackpad on the right.** The trackpad uses the right half's existing `pointing_listener`. That means:
- `pointer_accel` applies to it.
- Holding layer 2 (left thumb, `&mo 2`) scrolls, using the existing `scroller`: same direction as the trackball, at 1/16 speed.

If that speed is too slow for the trackpad, lower the 16 in `torabo_tsuki_lp_right.overlay`. Note this also changes the trackball's scroll speed whenever the trackball is used on that half.

**Trackpad on the left.**
- It moves the cursor without acceleration.
- Holding layer 1 (right thumb, `&lt 1 ENTER`) scrolls in the natural direction, like a laptop touchpad.
- The right trackball keeps working as before.

**Both setups:** tapping the pad gives a left click.

## Files

- `snippets/input-trackpad-cirque/`: the trackpad on `i2c0` at address 0x2A.
  - SDA is P0.18 and SCL is P0.16. DR is on P0.20.
  - A GPIO hog keeps P0.08 (sensor power) on.
  - The pad sleeps when the keyboard is idle.
  - `x-invert; y-invert;` cancel the X/Y flip that the listeners apply for the trackball.
- `snippets/input-trackpad-cirque-split-scroll/`: the layer-1 scroll mode when the trackpad is on the left half.
- `config/west.yml`: adds `petejohanson/cirque-input-module`, pinned to commit 0de55f3.
- `build.yaml`: adds the three targets above.

## Tuning

- **Cursor goes the wrong way:** in `input-trackpad-cirque.overlay`:
  - both axes wrong: delete both `x-invert;` and `y-invert;`
  - one axis wrong: delete just that line
  - axes swapped: add `rotate-90;`
- **Sensitivity:** `sensitivity = "2x";` takes values from `"1x"` (most sensitive) to `"4x"` (least sensitive).
- **Tap-to-click:** uncomment `no-taps;` to turn it off.
- **Left-trackpad scrolling:** in the split-scroll snippet:
  - which layer: `layers = <1>;`
  - speed: `zip_scroll_scaler 1 3` (`1 2` is faster, `1 6` is slower)
  - direction: change `Y_INVERT` to `X_INVERT`

## Hardware

- On the trackpad, remove R1 (470 kΩ) to switch it to I2C.
- On the adapter, fit R1 and R2 (4.7 kΩ). The trackpad has no I2C pull-ups of its own.
- Wiring (adapter → trackpad pad):
  - GND → GND
  - SDA → SDA
  - SCL → SCL
  - DR → DR
  - VCC → VDD
