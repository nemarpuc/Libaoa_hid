# HID Input Profiles

## Document contract

This document defines the descriptor and state-machine contract for the generated Input-only profiles. Evidence labels and the complete source register are in `FACT_AUDIT.md`. Numeric Usage values are from HID Usage Tables 1.7 and were retrieved from the official PDF on 2026-08-27.

None of these profiles has been validated on a physical Android target. "Portable candidate" means that the form is based on USB HID, the audited Linux source, and official Android documentation where cited; it does not mean demonstrated Android compatibility.

## Profile consolidation (current revision)

The public factory surface was consolidated from fourteen `aoahid_profile_kind` values to eight in 0.1.0. Every state machine and byte-for-byte descriptor behavior described below is unchanged; only which factory function/profile-kind constant reaches it changed. In 0.4.0, `AOAHID_PROFILE_TOUCHPAD` was reintroduced as a ninth, independent value — not, as in 0.1.0-0.2.0, folded into `aoahid_touch_options`; see the "Touchpad" section below:

| Removed factory / kind | Reached today through |
|---|---|
| `aoahid_spec_create_barcode_wedge` / `AOAHID_PROFILE_BARCODE_WEDGE` | `aoahid_spec_create_keyboard` (Wedge was already the Keyboard state machine verbatim) |
| `aoahid_spec_create_consumer` / `AOAHID_PROFILE_CONSUMER` | `aoahid_spec_create_toggle` with `application_page`/`application_usage`/`field_page` set to the Consumer Control page |
| `aoahid_spec_create_system_control` / `AOAHID_PROFILE_SYSTEM_CONTROL` | `aoahid_spec_create_toggle` with those three fields set to Generic Desktop / System Control |
| `aoahid_spec_create_camera_keys` / `AOAHID_PROFILE_CAMERA_KEYS` | `aoahid_spec_create_toggle` with `field_page` set to Camera Control (`0x90`); the Auto-focus/Shutter-only restriction is still enforced whenever `field_page == 0x90` |
| `aoahid_spec_create_telephony_keys` / `AOAHID_PROFILE_TELEPHONY_KEYS` | `aoahid_spec_create_toggle` with those three fields set to the Telephony Device page |
| `aoahid_spec_create_joystick` / `AOAHID_PROFILE_JOYSTICK` | Removed in 0.3.0. `aoahid_gamepad_options` no longer has an `application` field; `aoahid_spec_create_gamepad` always emits the Game Pad Application Collection. |
| `aoahid_spec_create_touchpad` / `AOAHID_PROFILE_TOUCHPAD` (0.1.0-0.2.0 form) | Removed in 0.3.0, when it was a `touchpad_button_count` field on `aoahid_touch_options`. Reintroduced in 0.4.0 as its own independent profile with its own `aoahid_touchpad_options` struct; see the "Touchpad" section below. |
| Touchscreen one-contact MT / pure ST (`aoahid_touch_protocol`) | Removed. Every Touchscreen Spec is now the fixed-slot Multi-Touch form only; `aoahid_touch_options` no longer has `protocol` or `target_verified` fields |

The runtime (per-report) API was also consolidated: `aoahid_keyboard_key_down`/`key_up`/`release_all` became `aoahid_kbd(node, usage, down)`; `aoahid_consumer_press`/`release`/`tap` and the redundant `aoahid_system_press`/`release`/`tap` became `aoahid_toggle(node, usage, down)`; `aoahid_gamepad_hat`/`aoahid_gamepad_dpad` became `aoahid_dpad(node, up, down, right, left)`; and `aoahid_touch_down`/`move`/`up` became `aoahid_touch(node, contact_id, down, x, y, extra)`, which auto-detects placement versus movement from whether `contact_id` is already active.

## Rules shared by every generated profile

1. **Report identity.** **[USB HID 1.11 requirement]** A report is identified by type and Report ID. This library emits Input reports only. If Report IDs are enabled, the declared ID is 1 through 255, the Report ID item precedes the first Input item, and every wire report begins with that byte. If Report IDs are disabled, no zero prefix is emitted.
2. **Collection placement.** **[USB HID 1.11 requirement]** A Report ID may follow an Application Collection declaration. `Collection -> Report ID -> Input` is valid.
3. **Top-level ownership.** **[USB HID 1.11 requirement]** Section 8.4 requires every top-level collection to be an Application Collection and prohibits one report from spanning more than one top-level collection. The collection Usage describes the device function but does not prove Android classification.
4. **Bit encoding.** **[USB HID 1.11 requirement]** Each value is encoded little-endian at the offset generated with the descriptor. Its logical range fits its explicit bit width. A signed field uses two's-complement encoding.
5. **Field span.** **[USB HID 1.11 requirement]** One field touches no more than four bytes. A 32-bit field begins on a byte boundary. The complete report can exceed four bytes. The generated structural pass applies this to both Input fields and declarative Constant Feature fields.
6. **Padding.** **[USB HID 1.11 requirement]** Unused trailing report bits are zero. Generated profiles use explicit Constant padding as **[Guide policy]** so that descriptor inspection and serializer layout remain aligned.
7. **No Output/Feature transport behavior.** **[Guide policy]** These profiles do not advertise behavior that requires an Output report or a returned Feature response. A descriptor may contain declarative Constant Feature metadata, such as Contact Count Maximum, without creating Feature transport. Keyboard LEDs, force feedback, rumble, and host-configured precision-touch features are outside this Input-only surface.
8. **Close state.** **[Guide policy]** A stateful profile serializes its neutral or release state before unregistering. Whether that report reached a disconnected target still depends on transport completion and target state.

## Automation audit across every generated profile

The library automates only values that follow mechanically from an already
declared descriptor and the node's accepted state. It does not invent a product
contract. The audit below records both the automation and its boundary.

| Generated profile | Mechanically derived state | Lifecycle protection | Deliberately caller-supplied product values | Evidence and current Android classification |
|---|---|---|---|---|
| Keyboard, full-NKRO bitmap (`aoahid_spec_create_keyboard`, also reached via the former Barcode/MSR wedge factory) | Modifier routing and bitmap offset selection are derived from the declared range as each `aoahid_kbd` call arrives; every simultaneously pressed key is its own bit, so there is no slot count and no overflow encoding to synthesize. | A press or release through `aoahid_kbd` cannot erase an opposite edge that has not reached its first accepted report. There is no release-all call; the caller releases each Usage it pressed. | Usage interval and Report ID. | Variable Selector bitmap is **[HUT 1.7 definition]** §3.4.2.1; offset derivation and the edge guard are **[Guide policy]**. Conditional, **[Unverified on hardware]**. |
| Mouse | Signed 64-bit pending totals are maintained independently for X, Y, Wheel, and AC Pan. Each report clamps every total to its declared field range; only the submitted fragment is consumed on terminal completion. | Button press/release edges are retained until their first accepted report; one report per node may be in flight. | Axis logical ranges and widths, button count, optional Wheel/Pan selection, Report ID, and pointer acceleration policy. | Relative-field meaning is **[USB HID 1.11 requirement]** and **[HUT 1.7 definition]**; accumulation, fragmentation, and completion consumption are **[Guide policy]**. Portable candidate, **[Unverified on hardware]**. |
| Toggle: Consumer Control (`aoahid_spec_create_toggle`, Consumer Control page) | The allow-list index selects one exact one-bit field. `aoahid_toggle` with `down=1`/`down=0` produces the required asserted and zero reports without the caller assembling a bitmap. | A changed active Usage cannot be replaced or released before its first accepted report. | Allow-list, exact HUT semantic for every Usage, application/field page, Report ID, and expected target event evidence. | Field semantics are **[HUT 1.7 definition]**; sparse-field selection and the transition guard are **[Guide policy]**. Conditional and **[Unverified on hardware]**. |
| Toggle: System Control (same factory, Generic Desktop / System Control page) | Same one-active-control state machine as the Consumer Control page. | Same accepted-report transition guard. | Allow-list, semantics, and target event evidence. | **[HUT 1.7 definition]** plus **[Guide policy]**; conditional and **[Unverified on hardware]**. |
| Toggle: Camera keys (same factory, `field_page = 0x90`) | Auto-focus and Shutter are fixed to their audited OSC fields by a validator rule keyed on `field_page == 0x90`; `down=1` then `down=0` emits assertion and re-arm reports. | Same accepted-report transition guard. | Whether Android exposes or intercepts the events is not inferred. | Camera Usage and OSC type are **[HUT 1.7 definition]**; the Consumer Application Collection is **[Guide policy]**. Conditional and **[Unverified on hardware]**. |
| Toggle: Telephony keys (same factory, Telephony Device page) | Same exact-field state machine for the caller allow-list. | Same accepted-report transition guard. | Telephony allow-list, semantics, and target event evidence. | **[HUT 1.7 definition]** plus **[Guide policy]**; conditional and **[Unverified on hardware]**. |
| Gamepad with canonical Hat (`aoahid_spec_create_gamepad`) | Four boolean directions passed to `aoahid_dpad` are converted to one of eight Hat values; no direction emits the deterministic Null value `15`; button edges and close-time axis/Hat neutralization are derived from state. | Button and Hat direction edges cannot be replaced before their first accepted report. Opposite Hat pairs are rejected instead of guessed. | Axis roles, ranges, widths, neutral values, buttons, Report ID, and target mappings. | Android Hat metadata and A/B/X/Y Button mappings are **[Android platform documentation]**; Usage meaning is **[HUT 1.7 definition]**; conversion and edge handling are **[Guide policy]**. Portable-candidate status requires the Game Pad collection, canonical Hat, and a contiguous Button range beginning at `1` with count at least `5`; otherwise it is conditional. **[Unverified on hardware]**. |
| Gamepad with raw D-pad fields | The four `aoahid_dpad` input booleans are copied to independent D-pad Up/Down/Right/Left OOC bits; simultaneous bits are not converted into an angle. Close clears all four bits along with buttons and explicit axis neutrals. | The four-bit state shares one accepted-report transition guard. | All gamepad axes and button product values. | Raw fields are **[HUT 1.7 definition]** §4.7; state derivation and close neutralization are **[Guide policy]**. Android combination behavior is not established, so the manifest is conditional and **[Unverified on hardware]**. |
| Gamepad without a D-pad | No directional value is synthesized. Buttons retain their per-edge guards, axes accept only explicit in-range samples, and close uses each explicit axis neutral. | Button edges cannot be replaced before their first accepted report; one report per node may be in flight. | Whether omitting a D-pad is suitable, plus every axis, button, and target mapping. | Absence of the CDD Hat contract is **[Guide policy]** and leaves the manifest conditional and **[Unverified on hardware]**. |
| Touchscreen, fixed MT (`aoahid_spec_create_touchscreen`; the only Multi-Touch form) | The frame is split at `contacts_per_report`; total Contact Count is emitted only in the first packet and continuation packets carry zero; inactive slots are zero-filled. `aoahid_touch` auto-detects a new contact_id as placement and an already-active one as movement. | Contact mutations and ID reuse are guarded across the complete multi-packet frame. | Maximum contacts, contacts per report, every field domain, and optional-field selection. | Packet interpretation is a **[Specified Linux implementation observation]**; deterministic packet construction is **[Guide policy]**. Portable candidate and **[Unverified on hardware]**. |
| Touchpad (`aoahid_spec_create_touchpad`) | Uses the same fixed-MT contact/count/Up automation as Touchscreen, sharing the identical `TouchFields`-based state machine; derives a button-only frame with Contact Count zero via `aoahid_touchpad_button`. | Same contact guards as Touchscreen; button changes are busy between contact-frame packets, so contact and button edges cannot be coalesced away. | Contact domains, button count (may be zero for a buttonless clickpad), physical size, gesture, palm, and pointer-acceleration policy. | Button processing is a **[Specified Linux implementation observation]** at the cited revision; frame coordination is **[Guide policy]**. Always conditional (never a portable candidate), because Android converts Touchpad contacts to ordinary mouse-source `MotionEvent` motion and gesture value-add is release/OEM dependent; see `FACT_AUDIT.md` A-14a. **[Unverified on hardware]**. |
| Direct pen | A sample with Tip set is accepted only with In Range set; contact pressure is floored to one; Away serializes In Range, Tip, pressure, Invert, and barrel buttons as zero; an in-range pen/eraser change emits an automatic departure before re-entry. The caller's In Range value is checked, not inferred. | Tip, In Range, tool-end, and barrel edges are retained until their first accepted report. | X/Y, pressure, tilt and Twist ranges, barrel Usages, hover support, and display association. | Usage meanings are **[HUT 1.7 definition]**; pressure contact behavior is **[Android platform documentation]**; Away normalization and tool switching are **[Guide policy]** informed by the specified Linux implementation. Portable candidate and **[Unverified on hardware]**. |
| Indirect pen/tablet | Uses the same pen state machine and Away normalization. | Same pen transition guards. | Indirect collection choice, field domains, target mapping, and display policy. | **[Guide policy]** over HUT fields; conditional and **[Unverified on hardware]**. |
| Battery Strength | A known value is serialized directly; unknown is converted to a deterministic representable value outside the logical interval when Null support was declared. | State replacement is blocked only by the node's one-report-in-flight rule; no key-like edge is invented. | Strength range, width, Report ID, and whether unknown/Null is supported. | Null semantics are **[USB HID 1.11 requirement]** and **[HUT 1.7 definition]**; deterministic encoding is **[Guide policy]**. Conditional and **[Unverified on hardware]**. |
| Raw Input report | No semantic state is synthesized. The supplied bytes are checked against the immutable parsed layout before submission. | One report per node may be in flight. | The complete descriptor, report bytes, Report IDs, Usages, ranges, and all target semantics. | Structural checks are **[USB HID 1.11 requirement]** and specified-parser compatibility policy; no Android semantic support is claimed. |

### Product values that automation must not invent

The following remain explicit even when a mechanically derived state machine is
available: Usage Page and Usage ID, Application Collection, Report ID, logical
and physical ranges, bit width, axis neutral, deadzone/flat, coordinate and
contact domains, maximum contacts, contacts per report, button count, optional
field presence, target event mappings, and Android classification evidence.
They describe a product or a target, not a frame calculation. **[Guide policy]**

The canonical Android form is the narrow exception to caller-selectable Hat
physical metadata: once the caller chooses it, Android 17 CDD §7.2.6.1 fixes
the Game Pad Application Collection, Logical `0..7`, Physical `0..315`, Unit
Degrees, Report Size four, and Button Page Usage IDs A=`1`, B=`2`, X=`4`, and
Y=`5`. The generated descriptor represents buttons as one contiguous Usage
range, so portable-candidate status additionally requires a range beginning at
`1` with count at least `5`. That contiguity rule is **[Guide policy]**; the
named controller facts are **[Android platform documentation]**. The caller
still explicitly chooses the axes, whether the Hat exists, and the product
ranges that Android does not fix here.

### Revisioned implementation observations

The original observations in this document remain pinned to Android common
commit `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`; they are not silently upgraded
to a later kernel. At that revision, `hid_report_raw_event` and
`hid_input_field` dispatch Variable fields in descriptor/field order, and
`hidinput_hid_event` reads Invert state when In Range selects the pen tool.
For this automation audit, the controller and pen-priority paths were also read
at the `android17-6.18` branch head retrieved on 2026-08-27, commit
[`f67745b7d96806e622db56f4be97af16d6e99850`](https://android.googlesource.com/kernel/common/+/f67745b7d96806e622db56f4be97af16d6e99850/),
specifically `hid_hat_to_axis`, the Generic Desktop Hat/D-pad mapping in
`hidinput_configure_usage`, Hat dispatch in `hidinput_hid_event`, and
`hidinput_usages_priorities` (Eraser, Invert, Tip Switch, Tip Pressure, In
Range) in
[`drivers/hid/hid-input.c`](https://android.googlesource.com/kernel/common/+/f67745b7d96806e622db56f4be97af16d6e99850/drivers/hid/hid-input.c),
plus `hid_process_report`, `__hid_insert_field_entry`, and
`hid_report_process_ordering` in
[`drivers/hid/hid-core.c`](https://android.googlesource.com/kernel/common/+/f67745b7d96806e622db56f4be97af16d6e99850/drivers/hid/hid-core.c).
These are **[Specified Linux implementation observation]** facts about that
exact revision, not an Android-wide or USB requirement. No current-revision
multitouch or battery claim is implied; the current-revision pen observation is
limited to the explicit field-priority relation. All runtime behavior remains
**[Unverified on hardware]**.

## Keyboard and keyboard wedge

### Descriptor

- Application collection: Generic Desktop / Keyboard (`0x01/0x06`). **[HUT 1.7 definition]**
- Key data: Keyboard/Keypad Page `0x07`. **[HUT 1.7 definition]**
- Modifiers: eight Variable bits for `0xe0` Left Control, `0xe1` Left Shift, `0xe2` Left Alt, `0xe3` Left GUI, `0xe4` Right Control, `0xe5` Right Shift, `0xe6` Right Alt, and `0xe7` Right GUI. **[HUT 1.7 definition]**
- Nonmodifier keys are always a caller-sized Variable bitmap: one bit per declared Usage, from `usage_minimum` through `usage_maximum`. There is no Array form, no Array slot count, and no ErrorRollOver overflow encoding — every simultaneously pressed key gets its own bit, so the profile is full N-Key Rollover (NKRO) by construction.
- Constant padding rounds the report up to the next whole byte after the modifier and bitmap fields. **[Guide policy]**
- No LED Output report is declared. **[Guide policy]**

### Full-NKRO bitmap

**[HUT 1.7 definition]** HUT §3.4.2.1 permits Selector Usages to be represented as Variable bits; the caller supplies the exact Usage range. Because every declared key is its own bit rather than a shared Array slot, no key overflow state exists and no ErrorRollOver value is ever emitted. Modern Linux and Android input subsystems treat each one-bit Variable key field as an ordinary `EV_KEY` event; target behavior for each Usage range remains **[Unverified on hardware]**.

### Serialization invariants

- A modifier Usage is routed only to its modifier bit.
- A nonmodifier Usage outside the caller-declared range is rejected.
- Duplicate key-down and key-up for an absent key do not create duplicate selectors.
- Close clears the modifier bitmap and every nonmodifier key. **[Guide policy]**
- A barcode wedge uses the same HID form. Text interpretation still depends on the target key layout, keyboard character map, locale, and IME and is **[Unverified on hardware]**.

## Mouse and relative pointer

### Descriptor

- Application collection: Generic Desktop / Mouse (`0x01/0x02`), with Pointer (`0x01`) as the physical collection where used. **[HUT 1.7 definition]**
- X (`0x30`) and Y (`0x31`) are Variable, Relative fields. **[HUT 1.7 definition]**, **[USB HID 1.11 requirement]**
- A portable bidirectional field has a signed range containing negative and positive values. A range that cannot encode one direction is rejected for this profile. **[Guide policy]**
- Buttons use Button Page Usages selected by the caller-supplied count.
- Wheel is Generic Desktop Wheel (`0x38`). Horizontal pan is Consumer AC Pan (`0x0c/0x0238`). **[HUT 1.7 definition]**

### Delta handling

Delta accumulation and splitting are **[Guide policy]**, not HID behavior. If pending motion is outside the field range, each submission emits one in-range fragment. Every fragment is a full report with the current buttons, wheel, and pan state. The submitted fragment is consumed on completion so that a timeout cannot cause an automatic duplicate move. Releases and state transitions are not coalesced away.

The target must be tested with positive and negative X/Y, simultaneous buttons, wheel, pan, high-rate input, and pointer capture. All are **[Unverified on hardware]**.

## Toggle: Consumer, System, Camera, and Telephony controls

### Descriptor form

The Toggle profile (`aoahid_spec_create_toggle`) is one factory over three caller-supplied fields: `application_page`/`application_usage` select the Application Collection, and `field_page` selects the Usage Page every `allowed_usages[i]` is read from. This is what lets one factory reach every page below instead of requiring one function per page.

- Consumer Control application collection: Consumer Page / Consumer Control (`0x0c/0x01`). **[HUT 1.7 definition]**
- System Control application collection: Generic Desktop / System Control (`0x01/0x80`). **[HUT 1.7 definition]**
- Camera controls use `field_page = 0x90` (Camera Control Page) with the Application Collection left at Consumer Control (`0x0c/0x01`); Table 35.1 defines Auto-focus `0x20` and Shutter `0x21`, both OSC, but defines no Camera-page Application Collection Usage. **[HUT 1.7 definition]** Nesting Camera Control fields inside a Consumer Control Application Collection is **[Guide policy]**; target classification remains conditional and **[Unverified on hardware]**. The validator enforces the Auto-focus/Shutter/OSC-only restriction whenever `field_page == 0x90`, independent of which Application Collection was chosen.
- Telephony controls use `application_page`/`field_page = 0x0b` (Telephony Page); a typical Application Usage is Phone `0x01`. **[HUT 1.7 definition]** §14.
- Each allow-listed Usage is one exact one-bit Variable field. Sparse IDs do not create an intervening Usage range. A field is set only when that exact allow-list entry is the node's active control. **[Guide policy]**
- `aoahid_toggle(node, usage, 0)` writes zero to all of these fields, ignoring `usage`. The state surface permits one active control at a time. **[Guide policy]**

The caller supplies `usage_semantics[i]` beside every `allowed_usages[i]`. The generated flags are:

| Explicit semantic | One-bit Input field | HUT source |
|---|---|---|
| Selector bitmap | Variable, Absolute, Preferred | §3.4.2.1 |
| OOC single-button toggle | Variable, Relative, Preferred | Table 3.2; §3.4.1.2 |
| OOC maintained switch | Variable, Absolute, No Preferred | Table 3.2; §3.4.1.2 |
| Momentary Control | Variable, Absolute, Preferred | Table 3.2; §3.4.1.3 |
| One Shot Control | Variable, Relative, Preferred | Table 3.2; §3.4.1.4 |
| Re-trigger Control | Variable, Absolute, Preferred | Table 3.2; §3.4.1.5 |

These are **[HUT 1.7 definition]** encodings selected explicitly by the caller. HUT §3.4.4 says the descriptor's flags/ranges establish the actual interpretation and permits alternate types, so the generic factory does not silently infer a default type from an arbitrary numeric Usage.

### Unsupported semantics are rejected outright

`aoahid_toggle` rejects `AOAHID_USAGE_ON_OFF_PAIR`, `AOAHID_USAGE_LINEAR`, `AOAHID_USAGE_DYNAMIC_VALUE`, and `AOAHID_USAGE_NAMED_ARRAY` with `AOAHID_ERR_UNSUPPORTED`. It has no direction argument, numeric-value argument, or nested collection state. In particular, Consumer Volume (`0x00e0`, LC) and AC Pan (`0x0238`, LC) cannot be submitted through this key-like profile. **[Guide policy]**

Declaring `field_page = 0x90` additionally rejects any Usage other than `0x20`/`0x21` and any semantic other than OSC. Every other `field_page` requires explicit semantics and target-event evidence; the library does not claim that the declared semantic is the only HUT-permitted alternate form for that page. Duplicate Usage IDs are rejected.

### Press and release

`aoahid_toggle(node, usage, 1)` writes one to the selected one-bit field; `aoahid_toggle(node, usage, 0)` writes zero. The HUT effect differs by the selected semantic: zero re-arms OSC, stops RTC, deasserts MC or maintained OOC, makes no state change for toggle OOC, and clears a Selector bit. A successful transport submission does not prove the target mapped or exposed the control. A caller that wants a guaranteed-observed single click sends `down=1`, waits for that report to complete (`aoahid_node_submit_blocking`), then sends `down=0`.

Every entry still carries an expected Linux event type and code. Audited mappings such as `Play/Pause -> EV_KEY/KEY_PLAYPAUSE`, `Volume Increment -> EV_KEY/KEY_VOLUMEUP`, and `Volume -> EV_ABS/ABS_VOLUME` are **[Specified Linux implementation observation]** from `hidinput_configure_usage` at commit `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`; they do not establish Android delivery. All profiles in this section remain conditional and **[Unverified on hardware]**.

## Gamepad and D-pad

### Collections and controls

- Application collection: Generic Desktop / Gamepad (`0x01/0x05`). **[HUT 1.7 definition]**
- Generic Desktop axes: X `0x30`, Y `0x31`, Z `0x32`, Rx `0x33`, Ry `0x34`, Rz `0x35`, Slider `0x36`, Dial `0x37`, and Wheel `0x38`. **[HUT 1.7 definition]**
- D-pad alternatives: Hat switch `0x39`, or D-pad Up/Down/Right/Left `0x90..0x93`. **[HUT 1.7 definition]**
- Simulation axes admitted by this profile include Rudder `0xba`, Throttle `0xbb`, Accelerator `0xc4`, Brake `0xc5`, and Steering `0xc8` on Simulation Controls Page `0x02`. **[HUT 1.7 definition]**
- Every configured axis includes an exact Usage Page, Usage, logical range, bit width, neutral value, expected Linux code, and expected Android axis. **[Guide policy]**

### Factory constraints and classification boundary

The factory requires at least two explicitly declared axes, including exactly
one X and one Y role, at least one Button Usage, and a nonzero first Button
Usage. Every axis role must match its audited HUT Usage Page and
Usage; duplicate roles, an out-of-range neutral, missing target-evidence names,
and an axis field whose range does not fit its declared bit width are rejected.
**[HUT 1.7 definition]** for the Usage identities; **[Guide policy]** for the
factory validation and evidence gate.

Simulation Accelerator, Brake, and Throttle require logical minimum `0`, a
positive logical maximum, and neutral `0`. Simulation Steering and Rudder
require a negative-to-positive logical interval and neutral `0`. Other axis
roles retain the caller's explicit in-range neutral; the factory does not infer
a center. **[HUT 1.7 definition]** plus **[Guide policy]**.

Hat mode accepts only Logical `0..7` in four bits. Non-Hat modes require the
three Hat option fields to remain zero. Raw-D-pad mode allocates four additional
one-bit fields after the declared Button fields and rejects a count that would
overflow the layout's 16-bit field-instance space. A canonical-Hat Gamepad is
marked portable candidate only when its contiguous ordinary Button Usage range
begins at `1` and has at least `5` entries. That includes the Android 17 CDD
§7.2.6.1 A=`1`, B=`2`, X=`4`, and Y=`5` mappings. Raw D-pad, no D-pad, and every
other Button range remain conditional. The CDD mappings are
**[Android platform documentation]**; range contiguity and status distinctions
are **[Guide policy]**. All remain **[Unverified on hardware]**.

The Android portable-candidate Hat is not a caller-tunable angular format.
The [Android 17 CDD §7.2.6.1](https://source.android.com/docs/compatibility/17/android-17-cdd)
fixes Logical Minimum `0`, Logical Maximum `7`,
Physical Minimum `0`, Physical Maximum `315`, Unit Degrees, and Report Size
four. **[Android platform documentation]** HID 1.11 §6.2.2.7 encodes Degrees as
English Rotation system `4` plus length dimension `1`, hence Unit `0x14` with
Unit Exponent `0`. **[USB HID 1.11 requirement]**

`aoahid_dpad(up, down, right, left)` derives the canonical wire value:

| Up | Down | Right | Left | Hat wire value | Meaning |
|---:|---:|---:|---:|---:|---|
| 0 | 0 | 0 | 0 | `15` | Null / no direction |
| 1 | 0 | 0 | 0 | `0` | Up |
| 1 | 0 | 1 | 0 | `1` | Up-right |
| 0 | 0 | 1 | 0 | `2` | Right |
| 0 | 1 | 1 | 0 | `3` | Down-right |
| 0 | 1 | 0 | 0 | `4` | Down |
| 0 | 1 | 0 | 1 | `5` | Down-left |
| 0 | 0 | 0 | 1 | `6` | Left |
| 1 | 0 | 0 | 1 | `7` | Up-left |

The clockwise order follows the Android CDD wording and the pinned
`hid_hat_to_axis` implementation. The CDD's accompanying value-1 example says
up-left and conflicts with its own clockwise wording; the selected target
implementation maps value 1 to up-right. This resolution is
**[Specified Linux implementation observation]** plus **[Guide policy]**, not a
universal HUT direction-number rule. Up+Down and Left+Right have no single
defined angular result and return `AOAHID_ERR_PARAM` without changing state.
Adjacent pairs produce the intermediate values required by the Hat model.
**[HUT 1.7 definition]** §4.3.

`aoahid_dpad` is the only runtime call for D-pad state; it derives either the
canonical Hat wire value shown above or, when the Spec selected
`AOAHID_DPAD_BUTTONS`, the four raw OOC bits without combining them into an
angle. The current ACK
dispatches those Variable fields individually; that exact source observation
does not establish portable Android combination behavior. The raw D-pad
manifest is therefore conditional. **[HUT 1.7 definition]** §4.7;
**[Specified Linux implementation observation]** at commit `f67745b7d96806e622db56f4be97af16d6e99850`;
**[Unverified on hardware]**.

### Axis neutral state

The library uses the caller-declared neutral value and proves that it fits the field. It does not infer the semantic center from `(minimum + maximum) / 2`.

- A signed stick profile can explicitly declare zero as neutral.
- Simulation Throttle, Accelerator, and Brake are zero-to-maximum controls in HUT; their profile neutral can be the declared minimum.
- Generic Desktop Z or Rz is not treated as a trigger without an explicit caller mapping.

All Android axis mappings remain **[Unverified on hardware]**.

### Hat Null algorithm

The descriptor sets the Null State flag. Given bit width `w`, signedness, and logical interval `[min, max]`, construct the representable domain `D` and choose a deterministic value in `D \ [min, max]`:

1. for a signed field, prefer the most-negative representable value when it is below `min`; otherwise use the most-positive representable value when it is above `max`;
2. for an unsigned field, use zero when it is below `min`; otherwise use the maximum representable value when it is above `max`;
3. reject the descriptor when no value exists outside the logical range.

For unsigned four-bit `0..7`, both `8` and `15` are outside the logical range
and therefore satisfy HID Null semantics. The library's deterministic unsigned
algorithm selects the maximum representable value, so the emitted neutral is
`15`. This exact selection is **[Guide policy]**, while the requirement that the
value be outside `0..7` is **[USB HID 1.11 requirement]** §6.2.2.5. Four-bit
unsigned `0..15` and four-bit signed `-8..7` have no Null encoding and are
rejected. Hat Usage is **[HUT 1.7 definition]** §4.3.

On close, buttons clear, the hat uses its Null encoding, and each axis uses its explicit neutral. **[Guide policy]**

## Touchscreen

### Descriptor forms

`aoahid_spec_create_touchscreen` is the only factory and always emits the Touch Screen Application Collection as the one fixed-slot Multi-Touch state machine. The one-contact-MT and pure-single-touch descriptor forms this library previously also emitted have been removed.

| Profile | Application Usage | Contact Usage | Classification status |
|---|---|---|---|
| Touchscreen | Digitizers Touch Screen `0x0d/0x04` | Finger `0x22` | Portable candidate; **[Unverified on hardware]** |

Multitouch records use Tip Switch `0x42`, Contact Identifier `0x51`, X/Y `0x01/0x30,0x31`, and Contact Count `0x54`. Optional fields are Tip Pressure `0x30`, Width `0x48`, Height `0x49`, Scan Time `0x56`, and a declarative Contact Count Maximum `0x55`. Usage identifiers are **[HUT 1.7 definition]** §16. Contact Count Maximum is emitted as Constant Feature metadata; it does not imply a Feature-response operation, and the manifest continues to report Feature transport as unsupported. **[Guide policy]**

### Contact lifecycle

The state machine is **[Guide policy]**, driven entirely through `aoahid_touch(node, contact_id, down, x, y, extra)`:

```text
None -> Down -> Up -> None
```

- `down=1` with a `contact_id` not currently active places it in a new slot (matching the prior `touch_down`); `down=1` with a `contact_id` already active moves it (matching the prior `touch_move`); both carry Tip=1.
- `down=0` is one reported record with the same Contact ID and Tip=0 (matching the prior `touch_up`).
- The ID is not reused until the Up report's final packet completes successfully.
- A contact cannot disappear as the only release signal because the audited default Linux class lacks `MT_QUIRK_NOT_SEEN_MEANS_UP`. **[Specified Linux implementation observation]**
- Final X/Y may be retained for deterministic diagnostics, but the audited `mt_process_slot` does not emit coordinates for an inactive slot. Retention is not labeled a HID or Linux requirement.

### Contact Count and packetization

Contact Count is the number of contact records carried by the frame, including explicit Tip=0 Up records; it is not the number of Tip=1 contacts. **[Guide policy]**

| Packet position | Contact Count | Evidence |
|---|---:|---|
| Single-packet frame | Number of records in the report | **[Guide policy]** fixed-slot format |
| First packet of a multi-packet frame | Total number of records in the frame | **[Specified Linux implementation observation]** `mt_touch_report` |
| Continuation packet | `0` | **[Specified Linux implementation observation]** `mt_touch_report` |
| After final Up has completed | No isolated empty report | **[Guide policy]** |

Inactive fixed slots are zero-filled. The logical Contact Count range still represents zero through the configured maximum because continuation packets require zero.

### Pressure, size, and time

- **Pressure:** **[Android platform documentation]** pressure is nonzero during contact and zero during hover/noncontact when a pressure axis exists. The profile requires a range that represents 0 and 1; Tip=1 is floored to at least 1. If pressure is unavailable, omit the field.
- **Width and height:** inactive fixed slots require zero to be representable. Application palm and size behavior is **[Unverified on hardware]**.
- **Scan Time:** **[HUT 1.7 definition]** §16.5 defines a relative timestamp,
  a default unit of 100 microseconds, a base at the first frame after
  inactivity, and one value shared by all contacts in a frame. **[Specified
  Linux implementation observation]** `mt_compute_timestamp` at commit
  `35556bed836f8dc07ac55f69c8d17dce3e7f0e25` assumes 100-microsecond units.
  The generated profile therefore accepts only an explicitly selected
  100-microsecond counter with Logical Minimum `0`. On the first packet of a
  frame, the state machine derives elapsed `std::chrono::steady_clock` time in
  100-microsecond ticks from the activity epoch, modulo
  `logical_maximum + 1`. Continuation packets reuse that frame's value. After
  successful completion leaves every contact inactive, the epoch
  resets and the next activity frame begins at `0`. Clock selection, modulo,
  successful-completion boundary, and reset are **[Guide policy]** mechanics;
  they are not added HUT semantics.

## Touchpad

### Descriptor forms

`aoahid_spec_create_touchpad` is an independent factory from `aoahid_spec_create_touchscreen`, with its own `aoahid_touchpad_options` struct, but it shares descriptor generation, validation, and the contact/button state machine with Touchscreen through one internal `TouchFields` representation. Every contact field (`x`, `y`, `contact_identifier`, `contact_count`, optional `pressure`/`width`/`height`/`azimuth`/`scan_time`, `maximum_contacts`, `contacts_per_report`, and the multi-packet/Report ID options) has the same name, meaning, and validation rule as `aoahid_touch_options`; only the Application Collection Usage, `button_count`, and the runtime `android_status` differ.

| Profile | Application Usage | Contact Usage | Classification status |
|---|---|---|---|
| Touchpad | Digitizers Touch Pad `0x0d/0x05` | Finger `0x22` | Conditional; **[Unverified on hardware]** (always conditional, never a portable candidate; see "Classification" below) |

`aoahid_touchpad_options` adds one field beyond `aoahid_touch_options`: `button_count`, a `uint32_t` naming the Touchpad's physical click buttons. `button_count` may be `0` for a buttonless clickpad; a buttonless Touchpad is a fully valid Spec whose `aoahid_touchpad_button` calls are simply never in range (`button > touch->buttons.size()` always holds when `button_count == 0`).

### Contact lifecycle

Identical to Touchscreen: driven entirely through `aoahid_touch(node, contact_id, down, x, y, extra)`, following the same `None -> Down -> Up -> None` state machine described in the Touchscreen section above. The same function accepts a Node of either `AOAHID_PROFILE_TOUCHSCREEN` or `AOAHID_PROFILE_TOUCHPAD` kind.

### Button lifecycle

`aoahid_touchpad_button(node, button, pressed)` is a one-based button index (`1` through `button_count`) with `pressed` either `0` or `1`. It shares its edge-guard state machine with Mouse and Gamepad buttons: a changed value cannot be replaced before its first accepted report (`AOAHID_ERR_BUSY` while a transition is pending), and it is rejected outright during an in-flight or multi-packet contact frame. Button state contributes to the same wire field (Button Page, one-bit-per-Usage, `FieldSemantic::buttons`) as Mouse and Gamepad, emitted after Contact Count in the fixed-slot frame. On close, every button clears alongside contact neutralization, matching the pre-0.3.0 implementation this profile restores. `aoahid_touchpad_button` on a Touchscreen Node, or `aoahid_touch` mutations through a Touchpad Node that violate the shared contact guards, are rejected exactly as documented above.

### Classification

Unlike Touchscreen, `android_status` is always `AOAHID_ANDROID_CONDITIONAL`, independent of `button_count`. Touchpad is a genuinely distinct Linux input property (`INPUT_PROP_POINTER`, versus a touchscreen's `INPUT_PROP_DIRECT`), but Android converts Touchpad contacts into ordinary mouse-source `MotionEvent` cursor motion, and the gesture value-add of a Touch Pad Application Collection over a plain Mouse profile varies by Android release and OEM. See `FACT_AUDIT.md` A-14a for the full historical note.

## Pen and stylus

### Descriptor

- Direct-screen mode uses Digitizers / Pen (`0x0d/0x02`); indirect-tablet mode uses Digitizers / Digitizer (`0x0d/0x01`) with Stylus (`0x20`) as the physical collection. **[HUT 1.7 definition]**
- Core fields are Generic Desktop X/Y, Digitizers Tip Switch `0x42`, and In Range `0x32`.
- Optional fields are Tip Pressure `0x30`, X Tilt `0x3d`, Y Tilt `0x3e`, Twist `0x41`, caller-selected barrel controls including Barrel Switch `0x44` and Secondary Barrel Switch `0x5a`, and Invert `0x3c` for eraser-end selection. **[HUT 1.7 definition]**
- X Tilt and Y Tilt are separate HUT Usages. Requiring the pair together is **[Guide policy]**, not a HUT rule.
- When eraser-end selection is enabled, the descriptor emits its Variable
  fields in the order Invert, Tip Switch, In Range. At commit
  `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, the generic HID path processes
  those fields in descriptor order and In Range reads the already processed
  Invert state. Android 17 ACK commit
  `f67745b7d96806e622db56f4be97af16d6e99850` independently assigns the priority
  order Eraser, Invert, Tip Switch, Tip Pressure, In Range. These are
  **[Specified Linux implementation observation]** facts about the named
  revisions; choosing Invert before Tip before In Range is **[Guide policy]**
  and does not establish hardware acceptance.

### Tool state

The generic-Linux profile uses one Tip Switch for contact and Invert for pen-versus-rubber tool selection. It does not emit a second Eraser Variable field for contact because the audited `hidinput_configure_usage` maps both Tip Switch and Eraser to `BTN_TOUCH`. **[Specified Linux implementation observation]**

Valid profile states are:

| State | In Range | Tip | Pressure | Tool selection |
|---|---:|---:|---:|---|
| Away | 0 | 0 | 0 | Invert is retained as internal next-tool state |
| Hover | 1 | 0 | 0 | Invert selects pen or rubber |
| Contact | 1 | 1 | at least 1 when pressure exists | Invert selects pen or rubber |

`Tip=1 => InRange=1` and the mutually exclusive tool state are **[Guide policy]**. HUT does not impose those implications.

### Away normalization

An Away report is normalized on the wire to In Range `0`, Tip `0`, pressure
`0`, Invert `0`, and every barrel button `0`. This applies whether Away was
requested by `aoahid_pen_depart` or was serialized from an accepted sample with
In Range clear. The remembered eraser-side selection may remain internal so a
later re-entry can select the same tool, but it is not asserted as Invert while
the tool is away. **[Guide policy]** informed by the specified
`hidinput_hid_event` tool-release path.

Away normalization does not invent X/Y, tilt, or Twist values. Those remain the
caller's last explicit product/sample values unless changed by the caller; no
official source establishes zero as their universal away value.
**[Guide policy]**

**[Specified Linux implementation observation]** The audited `hidinput_hid_event` samples Invert when In Range becomes active. Changing tool ends while already in range therefore uses this serialized transition:

```text
old tool in range -> InRange=0, Tip=0 -> change Invert -> InRange=1 with new tool
```

Close or explicit depart emits InRange=0, Tip=0, pressure=0, and cleared barrel buttons. Direct/indirect Android classification, display association, rotation, hover, eraser tool type, tilt, and Twist are **[Unverified on hardware]**.

## Battery Strength metadata

### Descriptor and meaning

- Application collection: Generic Device Controls / Background/Nonuser Controls (`0x06/0x01`). **[HUT 1.7 definition]** §§9 and 9.1.
- Value: Generic Device Controls / Battery Strength (`0x06/0x20`), a Dynamic Value. **[HUT 1.7 definition]** §9.2.
- Logical Minimum and Logical Maximum define the proportion-of-battery-life range. A Null value means unknown battery status. **[HUT 1.7 definition]** §9.2.
- The strength range and bit width are caller supplied. The range is ordered and nonnegative. **[Guide policy]**
- If unknown-state support is enabled, the descriptor sets Null State and the encoded domain must contain a value outside the logical range. A call with no value serializes that deterministic out-of-range encoding. **[USB HID 1.11 requirement]** §6.2.2.5; **[Guide policy]**.

### Exact audited Linux behavior

**[Specified Linux implementation observation]** This path exists only when the target kernel enables `CONFIG_HID_BATTERY_STRENGTH`. At commit `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, `hidinput_configure_usage` routes Battery Strength to the HID battery/power-supply code as `EV_PWR`, not to an ordinary key or motion axis. `hidinput_scale_battery_capacity` scales the logical interval to 0 through 100.

`hidinput_update_battery` ignores raw zero even when zero is inside the descriptor's logical range. It also ignores out-of-range values. Therefore:

- an out-of-range Null value can represent unknown without creating a capacity update;
- a declared `0..100` range does not prove that raw zero is published as 0% by this kernel revision; and
- source-level power-supply registration does not prove Android association or application visibility.

The capability manifest classifies this profile as conditional. Kernel configuration, `power_supply` creation, Android input-device association, and application observation are all **[Unverified on hardware]**.

## Required target tests

No profile is promoted from "candidate" or "conditional" to "verified" until `TARGET_MATRIX.md` records, on the same physical target and build:

1. kernel descriptor parse and the created `/dev/input/event*` capabilities;
2. `adb shell dumpsys input` classification, sources, ranges, and configuration files;
3. `adb shell getevent -lt` press/release or down/move/up event sequences; and
4. the application-visible `KeyEvent`, `MotionEvent`, and `InputDevice` results.

Passing a descriptor parser, unit test, mock AOA transport, or source audit does not satisfy these hardware gates.
