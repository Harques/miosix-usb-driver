# Vendored TinyUSB

The `tinyusb/` subdirectory is a vendored copy of upstream TinyUSB with local
modifications for the Miosix USB MSC host driver.

- Upstream: https://github.com/hathach/tinyusb.git
- Version: 0.20.0 (see `tinyusb/src/tusb_option.h`)

Local changes are committed directly on top of this base (the upstream `.git`
history was removed when vendoring). To see the delta against upstream, diff
these files against the `0.20.0` tag of upstream TinyUSB.
