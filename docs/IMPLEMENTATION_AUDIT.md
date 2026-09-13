# Implementation source audit

Retrieved: 2026-08-27

This note records the primary-source checks used for the controller API. It
does not claim that an operating system maps every listed HID Usage.
Evidence-label definitions and the complete HID/Linux source register are in
`FACT_AUDIT.md`. Every hardware result remains unverified unless a corresponding
physical-target record is present in `TARGET_MATRIX.md`.

| Implemented fact | Evidence label | Official primary source |
|---|---|---|
| Generic Desktop X, Y, Z, Rx, Ry, Rz, Slider, Dial, and Wheel are Usages `0x30` through `0x38`. | `[HUT 1.7 definition]` | USB-IF, *HID Usage Tables 1.7*, Generic Desktop Controls Usage table and §§4.2-4.3: https://www.usb.org/sites/default/files/hut1_7.pdf |
| D-pad Up, Down, Right, and Left are Generic Desktop On/Off Controls `0x90`, `0x91`, `0x92`, and `0x93`. | `[HUT 1.7 definition]` | USB-IF, *HID Usage Tables 1.7*, §4.7: https://www.usb.org/sites/default/files/hut1_7.pdf |
| Rudder and Throttle are Simulation Controls Usages `0xBA` and `0xBB`; Accelerator, Brake, and Steering are `0xC4`, `0xC5`, and `0xC8`. | `[HUT 1.7 definition]` | USB-IF, *HID Usage Tables 1.7*, Simulation Controls Usage table and §§5.2-5.3: https://www.usb.org/sites/default/files/hut1_7.pdf |
| Physical Minimum/Maximum, Unit Exponent, and Unit are Global items. Their state applies to later Main items; zero physical extents restore the default physical interpretation. Unit Exponent is a signed four-bit value. | `[USB HID 1.11 requirement]` | USB-IF, *Device Class Definition for HID 1.11*, §6.2.2.7: https://www.usb.org/sites/default/files/hid1_11.pdf |
| When a Unit is declared for a Main item, Logical and Physical extents plus Unit Exponent must also be declared. | `[HUT 1.7 definition]` | USB-IF, *HID Usage Tables 1.7*, §3.3: https://www.usb.org/sites/default/files/hut1_7.pdf |
| Azimuth is Digitizers Usage `0x3F`, a Dynamic Value representing counter-clockwise rotation through a full circular range. | `[HUT 1.7 definition]` | USB-IF, *HID Usage Tables 1.7*, Digitizers table and §16.3.3: https://www.usb.org/sites/default/files/hut1_7.pdf |
| The audited Linux multitouch mapping treats Azimuth as `[0, logical_maximum)`, maps it to `ABS_MT_ORIENTATION`, and converts stored samples to the kernel orientation convention. | `[Specified Linux implementation observation]` | Android common kernel commit [`35556bed836f8dc07ac55f69c8d17dce3e7f0e25`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/), `drivers/hid/hid-multitouch.c`, symbols `mt_input_mapping`, `mt_touch_input_mapping`, `mt_touch_event`, `mt_process_slot`: https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-multitouch.c |
| The audited Linux absolute-resolution helper uses logical extents, physical extents, Unit, and Unit Exponent, but accepts only specific units for each recognized axis family. | `[Specified Linux implementation observation]` | Same commit, `drivers/hid/hid-input.c`, symbol `hidinput_calc_abs_res`: https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-input.c |
| One-/two-byte Local Usage values select IDs on the current page; four-byte values embed page and ID; an extended Usage Minimum requires an extended Usage Maximum. | `[USB HID 1.11 requirement]` / `[HUT 1.7 definition]` | USB-IF, *HID 1.11* §6.2.2.8 and *HUT 1.7* §3.1: https://www.usb.org/sites/default/files/hid1_11.pdf, https://www.usb.org/sites/default/files/hut1_7.pdf |
| The audited Linux parser forms a Usage Minimum/Maximum interval from raw `item_udata()` endpoints before `complete_usage` page composition, uses the Maximum item's size for added entries, and clips an over-capacity expansion to its finite Usage table. | `[Specified Linux implementation observation]` | Android common commit `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, `drivers/hid/hid-core.c`, symbols `hid_parser_local`, `hid_add_usage`, `complete_usage`: https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-core.c |
| `LIBUSB_TRANSFER_OVERFLOW` means the device sent more data than requested and is distinct from generic transfer failure. | `[libusb contract]` | libusb 1.0.30 asynchronous-transfer status table: https://libusb.sourceforge.io/api-1.0/group__libusb__asyncio.html |
| AOA request 51 returns a nonzero 16-bit little-endian protocol version. | `[AOA requirement]` | Android Open Accessory 1.0, "Attempt to start in accessory mode," request 51: https://source.android.com/docs/core/interaction/accessories/aoa |

Implementation consequence: every public controller axis role is checked
against its exact Usage Page and Usage, and the D-pad representation is an
explicit caller choice (`NONE`, Hat Switch, or the four Generic Desktop OOC
fields). These checks are descriptor facts only; Linux/Android mapping claims
continue to require separate platform evidence supplied by the caller.

Physical metadata is therefore an explicit per-field caller choice. An enabled
declaration emits all four related Global items and resets Physical Minimum,
Physical Maximum, Unit Exponent, and Unit to zero immediately after that
field's Input Main item. This prevents Global state from changing a later
field. The library validates HID encoding and the exact audited Azimuth sample
domain; it does not claim that an arbitrary Unit produces a nonzero Linux
resolution or any Android-visible resolution.
