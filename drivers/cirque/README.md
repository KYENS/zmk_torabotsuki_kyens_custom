# Cirque Pinnacle driver (vendored)

This is a copy of `drivers/input` from
[billyjacoby/cirque-input-module](https://github.com/billyjacoby/cirque-input-module) at `ba53fe8`. That fork adds absolute mode to
[petejohanson/cirque-input-module](https://github.com/petejohanson/cirque-input-module), the original ZMK contributors' driver (MIT).

The original is in the commit "Vendor Cirque Pinnacle driver", so `git diff` against it shows every change made here.

## Changes from upstream

- **Fixed: the trackpad stops working after a while.** In absolute mode, the data-ready (DR) interrupt is masked when it fires and only unmasked after a successful read. If one read failed (an I2C glitch, or DR firing without fresh data), the function returned early. The interrupt then stayed masked and the pad went dead until the next reboot. Now the status is always cleared and the interrupt re-armed.
- **Added: watchdog.** Every 500 ms the driver checks whether DR is stuck active with no read pending. If it is, the driver services it, which re-arms the interrupt. The check is skipped while the device is suspended.
