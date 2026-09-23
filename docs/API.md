# C API

`include/aoahid.h` is the stable C ABI. It exposes opaque handles and fixed-width
types only; no libusb or C++ type crosses the boundary.

Evidence-label definitions and the transport/platform primary-source register
are in `SOURCE_CONFLICTS.md`; HID/profile sources are in `FACT_AUDIT.md`.

Every public result/option domain is an `int32_t` typedef with integral constants,
including the values callers commonly describe as enums. This keeps field and
return-value width invariant when a consumer enables a compiler's short-enum
mode. `tests/abi/test_c_abi.c` is configured to compile with that mode on the
applicable CI compilers and asserts every domain width; `TARGET_MATRIX.md`, not
this API description, records whether a remote run passed.

## Required structure discipline

Every top-level options structure has `struct_size` and `reserved`. Set
`struct_size = sizeof(struct_type)` and every reserved byte/field to zero. Set
every product- and target-specific field yourself. Zero is rejected as
`AOAHID_ERR_UNSET_FIELD` where it has no defined meaning, except for the
explicitly documented host-transport tuning fallbacks below. Optional product
features use a separate enable/has flag, so a zero value is never guessed as
absence.

No initializer or profile template is part of the API. An old binary and a new
header cannot silently reinterpret a structure: an exact size mismatch fails.

## Object graph and ownership

| Handle | Owner and lifetime |
|---|---|
| `aoahid_context` | Owns one libusb runtime and optionally one event thread. Destroy after all devices. |
| `aoahid_discovery` | Owns a point-in-time list and all strings/pointers returned by `aoahid_discovery_get`. |
| `aoahid_device` | Owns one USB handle, transfer pool, registered nodes, and open channels. HID IDs come from the context-lifetime, physical-identity-domain allocator described in `SOURCE_CONFLICTS.md` T-05. |
| `aoahid_spec` | Immutable and reference-counted. It can be registered on multiple devices/contexts. |
| `aoahid_node` | Owns mutable profile state and one AOA HID registration; it retains its spec. |
| `aoahid_channel` | Owns one claimed Bulk interface, its fixed transfer pool, and read-ahead state on its Device's USB handle. |

`aoahid_spec_descriptor` and manifest pointers remain valid while the spec is
retained. A discovery entry remains valid only until discovery destruction.

## Threading domains

The caller must explicitly select one event mode.

- `AOAHID_EVENT_CALLER_POLL`: the application serializes all calls that touch a
  context and invokes `aoahid_context_poll`. This path has no internal event
  thread.
- `AOAHID_EVENT_INTERNAL_THREAD`: the context creates one event thread. The
  application still serializes state updates for each context; this is not a
  promise that arbitrary calls on one handle are concurrently safe.

The internal thread calls libusb's wrapped event-handling function rather than
polling libusb file descriptors directly. **[libusb contract]** The libusb
1.0.30 "Multi-threaded applications and asynchronous I/O" page states that its
wrapped handlers implement the event-lock scheme and that synchronous I/O in
another thread uses the event-waiter mechanism. A libusb callback itself never
calls a synchronous or other event-handling libusb API.

The internal pump uses a bounded 60-second idle event wait. Context teardown sets its stop flag, calls
`libusb_interrupt_event_handler` if the event thread has not already exited,
and then joins the thread; it does not periodically wake only to discover that
shutdown was requested. An available USB event wakes the blocking handler
before the bound, so 60 seconds is not a report-polling interval. An
immediately failing backend is separately rate-limited by an interruptible 10 ms
condition-variable wait. **[libusb contract]** plus **[project policy]**; see
`SOURCE_CONFLICTS.md` C-19.

A node permits one report in flight. A state mutation while that report is in
flight returns `AOAHID_ERR_BUSY`; this prevents a completion from consuming a
newer relative delta or lifecycle state.

On Linux with GNU or Clang, `cmake --preset tsan`,
`cmake --build --preset tsan`, and `ctest --preset tsan` execute the complete
deterministic suite with ThreadSanitizer instrumentation. In particular, the
internal-thread test repeatedly overlaps terminal callbacks and Node close.
TSan reports only races reached by that run; passing it does not extend the API
contract to concurrent calls the caller is required to serialize.

## Open sequence

1. `aoahid_context_create`.
2. `aoahid_discover`; choose an entry while the discovery object is alive.
3. `aoahid_device_open` with every product/target policy supplied. Set a host-
   transport tuning field explicitly, or leave that field zero to select its
   documented fallback.
4. Create immutable specs with the profile factories.
5. `aoahid_node_open` for each spec. It allocates Node state, reserves final
   publication capacity, retains the Spec, and installs the transfer-pool
   reservation before request 54. Requests 54 and 56 then complete
   synchronously, and any registration failure unwinds those host resources.
6. Update profile state and call `aoahid_node_submit` or
   `aoahid_node_submit_blocking`.
7. Optionally `aoahid_channel_open` for Bulk data on the same USB handle (for
   example ADB, or the accessory interface for an Android app).

### Switching a device to accessory mode

AOA HID works in two USB modes. In the current (Mode A) mode the host sends
requests 54-57 without restarting the device (target-conditional, T-07). The
standard AOA flow instead switches the device first; the library exposes that
switch as one explicit call and leaves every other step to the application:

1. `aoahid_discover`, then `aoahid_accessory_start(context, info, &options)`.
   It sends request 51, request 52 for each non-null string (string IDs 0-5,
   each NUL-terminated UTF-8 of at most 256 bytes including the NUL, checked
   before any request), and request 53 on EP0, closes the device, and returns.
   Manufacturer and model are required product values: Android matches them
   against an application's accessory filter. **[AOA requirement]** for the
   request sequence; the strings are **[product policy]**.
2. The device disconnects and re-enumerates as VID `0x18D1`, PID `0x2D00`
   (accessory) or `0x2D01` (accessory plus ADB); AOA 2.0 audio adds
   `0x2D02`-`0x2D05`. The library does not wait for this and never retries.
   Rediscover on the application's own schedule and match the same bus and
   port path. Android cancels the request when the host does not configure the
   device within 10 seconds (**[AOSP source]**
   `services/usb/java/com/android/server/usb/UsbDeviceManager.java`
   `ACCESSORY_REQUEST_TIMEOUT`, main at
   `15c8b135d2c41940f53aa77f3d48354bc2e7e2d7`, retrieved 2026-09-23).
3. Open the accessory-mode entry with `aoahid_device_open`, then continue at
   step 4 above.

A selection already in accessory mode receives no request. The library has no
call that leaves accessory mode: unplugging the device, or `svc usb
setFunctions` through ADB, returns it to its default USB functions.

Device options keep byte transport policies separate from five caller-selected
policies for the exact target HID parser: maximum registered fields in one
report type plus Report ID, Local Usage capacity, Global Push depth, greatest
Report Size declaration, and accumulated report data bits. The library records
these requirements while constructing or validating every descriptor and
compares them immediately before registration. A policy failure returns
`AOAHID_ERR_OVERFLOW` before request 54. The library supplies no target value;
values such as 256 fields, 12288 Usages, stack depth 4, Report Size 256, and
65,528 report data bits belong only to their cited kernel revision.

### Device and Node tuning fallbacks

`aoahid_device_open` copies the caller's structure and replaces only these
zero-valued host-transport tuning fields before validation. The caller's memory
is not modified and the public structure layout is unchanged.

| Public field | Zero selects | Scope |
|---|---:|---|
| `control_timeout_ms` | 500 ms | Failure deadline for synchronous AOA control transfers. |
| `send_timeout_ms` | 500 ms | Failure deadline for an asynchronous request-57 transfer. |
| `descriptor_fragment_bytes` | 64 bytes | Maximum request-56 descriptor payload per fragment. |
| `transfer_pool_slots` | 8 slots | Per-Device asynchronous transfer-pool capacity. |
| `maximum_report_bytes` | 1024 bytes | Per-slot report payload capacity and public report-length policy. |
| `close_drain_timeout_ms` | 1000 ms | Bounded Device close/drain budget. |
| `aoahid_node_options.has_reserved_slots == 0` and `reserved_slots == 0` | no reservation | The Node uses the shared Device pool without a reserved slot. |

Every value in this table is **[project policy]**. The USB specifications and
libusb documentation do not recommend these universal millisecond values.
libusb defines a timeout of zero as unlimited/no timeout, but public
`aoahid_device_options` intercepts zero and applies the bounded values above;
use an explicit nonzero value to override them. A 500 ms timeout is a failure
deadline passed to the backend, not a sleep and not latency added after a
successful transfer.

Fallbacks do not bypass cross-field validation. For example, if an explicit
target EP0 or host-control-buffer policy is below 1024 bytes, set
`maximum_report_bytes` to a compatible explicit nonzero value or open fails
with `AOAHID_ERR_OVERFLOW`; the library does not enlarge the target policy.

These fallbacks do not apply to `startup_mode`, event mode, validation policy,
interface policy, descriptor/EP0/control-buffer policies, the five exact-target
HID parser policies, or any profile Usage, range, width, count, Report ID, or
physical/unit setting. Those values define a product or target contract and
remain caller supplied.

The runtime sends HID requests only on the selected device's current EP0. This
Mode-A path is target-conditional: AOA 2.0 says HID needs no new USB interface,
but does not guarantee that every vendor kernel routes requests 54-57 before
`ACCESSORY_START`. The library does not send requests 52, 53, or 58 and does not
perform re-enumeration. See `SOURCE_CONFLICTS.md` T-07.

`aoahid_device_open` itself never switches modes. `AOAHID_START_ACCESSORY_MODE`,
`reenumeration_timeout_ms`, `accessory_strings`, `enable_deprecated_audio_mode`,
and (since 2.0.0) `first_report_attempts` and `first_report_backoff_us` remain
in `aoahid_device_options` only to preserve the established structure size,
offsets, and language-binding layouts. The retained mode, non-null accessory
strings, and a nonzero audio flag return `AOAHID_ERR_UNSUPPORTED` before USB
I/O; the other retained members are ignored. Switch modes with
`aoahid_accessory_start`, which uses `aoahid_aoa_strings` through
`aoahid_accessory_options`.

## Bulk Channels

`aoahid_channel_open` first selects configuration 1 if, and only if, the device
is unconfigured (AOA 1.0 asks the host to set configuration 1 before using the
accessory endpoints; libusb documents re-selecting the active configuration as
a lightweight device reset, so a configured device is left alone). It then
selects the first interface (alternate setting 0) whose class, subclass, and
protocol match and that has one Bulk IN and one Bulk OUT endpoint, claims it on
the Device's handle, and, in the default stream read mode, starts
`in_transfers` read-ahead IN transfers. HID and Bulk share one handle, so a composite device never needs a
second open (WinUSB refuses one). Examples: ADB `0xFF/0x42/0x01` (**[AOSP
source]** `packages/modules/adb/adb.h` `ADB_CLASS`/`ADB_SUBCLASS`/`ADB_PROTOCOL`,
main at `4516d3cbfb9aafa2fb1c1be0949b8b866cc7801f`, retrieved 2026-09-23); the AOA
accessory interface of a `0x2D00`/`0x2D01` device, which exists only when the
manufacturer and model strings were sent. Channel transfers use their own pool,
so Bulk traffic never takes a HID transfer slot.

- `aoahid_channel_read` returns bytes in order: one call may return part of a
  USB transfer. The caller reassembles its own framing (for ADB, the 24-byte
  message header's length field).
- `read_mode` chooses how IN transfers are sized:

  | `read_mode` | IN transfers | Use when |
  |---|---|---|
  | `AOAHID_CHANNEL_READ_STREAM` (zero) | `in_transfers` transfers of `transfer_bytes` stay submitted (read-ahead). | The device ends every message with a short packet or a zero-length packet, or `transfer_bytes` is one `wMaxPacketSize`. |
  | `AOAHID_CHANNEL_READ_REQUEST` | A read with nothing buffered submits one transfer of its `capacity` rounded up to `wMaxPacketSize`, at most `transfer_bytes`. `in_transfers` is ignored. | The protocol states each length up front, as ADB does. |

  A Bulk IN transfer completes only when its buffer is full or a short packet
  arrives. So in stream mode, a message whose length is a multiple of
  `wMaxPacketSize` and that ends without a zero-length packet waits in an
  unfinished transfer until the device sends more. adbd sends no such
  zero-length packet, and host adb instead reads the 24-byte header, then
  exactly `data_length` bytes (**[AOSP source]**
  `packages/modules/adb` at `1cf2f017d312f73b3dc53bda85ef2610e35a80e9`:
  `client/usb_libusb_device.cpp` `LibUsbDevice::Read`, and
  `client/transport_usb.cpp` `UsbReadPayload`, whose comment reads "The device
  won't send a zero packet for packet size aligned payloads"; read
  2026-09-24). Request mode reads the same way. A request completes as soon as
  it is full, and a timeout leaves it pending for the next read, so no byte is
  lost. Bytes beyond a later, smaller `capacity` stay buffered. A zero-length
  packet carries no data and is waited past in both modes.
- `aoahid_channel_write` copies data into free OUT transfers and returns once
  every byte is submitted; a full pool waits at most `timeout_ms` and then
  reports `AOAHID_ERR_TIMEOUT` with the queued byte count. A failed OUT
  completion is reported once by the next write. Nothing is retried.
- `timeout_ms = 0` never waits, like `aoahid_context_poll`. In caller-poll mode
  a waiting read or write drives libusb events itself; in internal-thread mode
  it sleeps on a condition variable that the event thread signals only while a
  reader or writer is actually blocked.
- `zero_length_termination = 1` ends a write whose length is a nonzero multiple
  of `wMaxPacketSize` with an explicit zero-length transfer, which behaves the
  same on every backend instead of relying on a Linux-only libusb flag.
- A failed IN transfer or a disconnect loses the Channel: reads and writes then
  return `AOAHID_ERR_NO_DEVICE` and the Channel must be closed.
- Opening returns `AOAHID_ERR_BUSY` when another program already holds the
  interface; for the ADB interface that is usually a running `adb` server
  (stop it with `adb kill-server` first). HID on EP0 is unaffected, because it
  claims no interface by default.

Threading: in internal-thread mode one thread may read while another writes,
concurrently with other Context calls, but never concurrently with closing that
Channel, its Device, or the Context. In caller-poll mode read and write belong
to the Context domain like every other call. Read and write take no lock and
allocate nothing.

### Accessory and Channel tuning fallbacks

| Public field | Zero selects | Scope |
|---|---:|---|
| `aoahid_accessory_options.control_timeout_ms` | 500 ms | Failure deadline for each of requests 51, 52, and 53. |
| `aoahid_channel_options.in_transfers` | 4 transfers | IN transfers kept submitted for reading ahead (stream read mode only). |
| `aoahid_channel_options.out_transfers` | 4 transfers | OUT transfer pool; a full pool makes a write wait (back-pressure). |
| `aoahid_channel_options.transfer_bytes` | 65536 bytes | Buffer per transfer, rounded up to a multiple of `wMaxPacketSize`; in request read mode, the largest IN request. |

Every value in this table is **[project policy]**, not a USB, Android, or ADB
requirement. `aoahid_channel_close` uses the Device's `close_drain_timeout_ms`.

## Submission semantics

`aoahid_node_submit` queues every report asynchronously, including the first.
It may perform one nonblocking caller-poll reap after acceptance. Later
completion failures are available from `aoahid_device_latched_error`;
`aoahid_node_submit_blocking` also returns the completion result for its node.
The blocking form pumps until the profile is clean, so it emits every
mouse-delta fragment, touch continuation packet, and pen tool-transition
report.

The library never retries a transfer. A STALL on request 57 means the target
refused it, for example a first report that raced Android's asynchronous HID
registration, so nothing was applied: the refused state stays pending and the
caller's next submit resends it, after whatever delay the caller chooses.
`examples/c/verify/verify_common.h` shows a bounded caller-side resend. Timeout,
cancel, short transfer, and I/O errors consume the submitted state instead,
because the report may have been delivered and resending could duplicate a key,
contact, or relative motion (`SOURCE_CONFLICTS.md` T-03).

When `validate_reports = 1`, every generated report is decoded again before
request 57 and checked against its immutable wire length, Report-ID prefix,
field ranges, signedness, null encoding, and zero padding. Profile state
machines enforce lifecycle rules before serialization. With the flag disabled,
the co-generated serializer still performs its mandatory bounds checks, but the
second wire-image scan is skipped. Raw reports always require an exact declared
ID and length, independent of this option.

## Profile operations

The runtime API was consolidated to one mutation call per profile family; a
`down`/`0`-or-`1` argument replaces what used to be separate `_down`/`_up`,
`_press`/`_release`/`_tap`, or `_hat`/`_dpad` functions.

| Profile | Mutation calls |
|---|---|
| Keyboard | `aoahid_kbd(node, usage, down)` |
| Mouse | `aoahid_mouse_move`, `_scroll`, `_button` |
| Toggle (Consumer/System/Camera/Telephony/caller page) | `aoahid_toggle(node, usage, down)` |
| Gamepad | `aoahid_gamepad_button`, `_set_axis`; `aoahid_dpad(node, up, down, right, left)` |
| Touchscreen | `aoahid_touch(node, contact_id, down, x, y, extra)` |
| Touchpad | `aoahid_touch(node, contact_id, down, x, y, extra)`; `aoahid_touchpad_button(node, button, pressed)` |
| Pen | `aoahid_pen_update`, `_depart` |
| Battery Strength | `aoahid_battery_update` with explicit known/unknown flag |
| Raw | `aoahid_raw_submit` with an exact accepted report ID/length |

### Mechanically derived state

The generated-profile API separates lifecycle values that follow mechanically
from accepted Node state from descriptor/product choices that only the caller
can make. The complete evidence-labeled audit is in `PROFILES.md`; normative
and exact-revision support is registered in `FACT_AUDIT.md` and
`SOURCE_CONFLICTS.md`. The rules below are **[Guide policy]** unless the linked
audit assigns a stronger label to the underlying field meaning.

| Profile | Derived serialization and lifecycle guard | Caller-supplied contract that remains mandatory |
|---|---|---|
| Keyboard, full-NKRO bitmap | Modifier Usages are routed to their separate bits. Every nonmodifier Usage maps to its own bit, so any number of simultaneously pressed keys up to the declared range is reported at once; there is no Array slot count and no ErrorRollOver overflow encoding. Duplicate edges are suppressed and an opposite edge waits for the first accepted report. `aoahid_kbd` has no release-all call; the caller releases each Usage it pressed. | Usage interval and Report ID. |
| Mouse | X, Y, Wheel, and AC Pan each use a signed 64-bit pending total. Serialization clamps each independently to its declared field range; terminal completion consumes only the submitted fragment. Button edges use the same accepted-report guard. | Axis ranges/widths, button count, Wheel/Pan presence, Report ID, and acceleration policy. |
| Toggle (Consumer/System/Camera/Telephony/caller page) | `aoahid_toggle(node, usage, 1)` asserts exactly one allow-listed field; `aoahid_toggle(node, usage, 0)` emits the all-zero state; `usage` must be `0` (release whichever Usage is pressed) or the Usage actually pressed, and any other nonzero Usage returns `AOAHID_ERR_PARAM`. The accepted-edge guard supplies the `1 -> 0` wire lifecycle for Selector bitmap, OOC toggle, OOC maintained, MC, OSC, and RTC descriptor forms. Declaring `field_page = 0x90` (Camera Control) additionally restricts the allow-list to the two audited OSC controls. | Every Usage and `usage_semantics[i]`, `application_page`/`application_usage`/`field_page`, expected Linux event evidence, Report ID, and target delivery/interception evidence. LC, DV, NAry, and two-direction OOC remain unsupported by this key-like API. |
| Gamepad | `aoahid_dpad` maps `(up, down, right, left)` to Hat values Up `0`, Up-right `1`, Right `2`, Down-right `3`, Down `4`, Down-left `5`, Left `6`, Up-left `7`, and no direction to Null `15`. Opposite pairs return `AOAHID_ERR_PARAM` without changing Hat state. In `AOAHID_DPAD_BUTTONS` mode the four booleans remain independent OOC bits, including simultaneous opposites. Buttons and direction changes retain accepted edges; close restores explicit axis/D-pad neutrals. | Axes, ranges, widths, neutrals, ordinary buttons, D-pad representation, Report ID, and target mappings. Only Game Pad + canonical Hat + a contiguous Button range from `1` with at least five fields is a portable candidate; raw/no-D-pad forms remain conditional. |
| Touchscreen | `aoahid_touch(node, contact_id, down, x, y, extra)` auto-detects placement (a not-yet-active `contact_id`) versus movement (an already-active one) from `down=1`, and lift from `down=0`; Contact Count is computed from the records in the frame, only the first packet carries the total, and continuation packets carry zero. An Up record retains the Contact ID and emits one Tip=0 record before reuse. Tip=1 pressure is floored to one when present. Enabled Scan Time is computed at first-packet submission as elapsed steady-clock time in 100-microsecond ticks, reused for continuation packets, wrapped to the declared counter domain, and restarted at zero after inactivity. Contact and packet conflicts return `AOAHID_ERR_BUSY`. | Coordinate, Contact ID/Count/Scan Time ranges and widths, maximum contacts, contacts per report, optional fields, and Report ID. |
| Touchpad | Same `aoahid_touch` contact derivation as Touchscreen (both profiles share the same internal `TouchFields` state machine). `aoahid_touchpad_button(node, button, pressed)` is a one-based, edge-guarded button state machine shared with Mouse/Gamepad buttons; it is rejected during an in-flight or multi-packet contact frame. | Coordinate/contact/count/time ranges and widths as Touchscreen, plus `button_count` (may be zero for a buttonless clickpad). `android_status` is always `AOAHID_ANDROID_CONDITIONAL`, independent of `button_count`. |
| Pen | Tip=1 requires In Range. Contact pressure is floored to one. Away serializes In Range, Tip, pressure, Invert, and barrel fields as zero. Changing pen/eraser end while already in range emits a departure report before re-entry; the remembered eraser selection can remain internal. | In Range remains an explicit checked sample; X/Y and optional pressure/tilt/Twist domains, barrel Usages, eraser/hover support, mode, Report ID, display association, and target mapping remain explicit. Away does not invent X/Y, tilt, or Twist. |
| Battery Strength | `has_value = 1` serializes the supplied in-range value, including a known value of zero. `has_value = 0` requires `strength = 0` and an enabled Null state, then emits the deterministic representable encoding outside the logical interval. | Strength range/width, Null support, Report ID, kernel option, target association, and application visibility. The audited Linux revision's handling of raw zero remains a target caveat, not an API rewrite. |
| Raw Input | No semantic state, transition, neutral, repeat, or timer is synthesized. The report is checked against the immutable accepted ID/length/layout contract and then copied for submission. | The complete descriptor and report bytes, IDs, Usages, ranges, padding, and every target semantic. |

These guards prevent an unreported press/down/tool edge from being silently
replaced by its opposite; they do not prove that a successfully submitted
report was mapped by Android. No generated profile schedules key repeat,
debounce, long-press, RTC cadence, or another periodic action. Applications
remain responsible for any such product policy, and `TARGET_MATRIX.md` remains
the hardware-evidence gate.

`aoahid_toggle_options` pairs each `allowed_usages[i]` with an exact
`usage_semantics[i]`, expected Linux event type, and expected Linux code, plus
the `application_page`/`application_usage`/`field_page` that select which HUT
page the factory reaches (Consumer, System Control, Camera keys, Telephony
keys, or another caller-chosen page). The
accepted key-like semantics are Selector bitmap, OOC toggle, OOC maintained,
MC, OSC, and RTC. LC, DV, NAry, and two-direction OOC return
`AOAHID_ERR_UNSUPPORTED` because `aoahid_toggle` cannot carry their required
value, direction, or collection state. The generated descriptor contains one
1-bit Variable field for each exact sparse Usage; it is not a zero-aligned
Array. See `FACT_AUDIT.md` A-09/A-10 for the HUT 1.7 evidence.

The C++ header wraps these into distinct node-reference types. A typed reference
is default-unbound and can be populated only by `aoa::bind`, which reads the
Node manifest and rejects a profile that the corresponding C mutators do not
accept. The untyped `aoa::node_ref` remains the explicit raw-handle wrapper.
Bindings preserve the same C structures and do not choose tuning overrides.
Zero-valued tuning fields are normalized by the native C API exactly as above.

## Manifest

`aoahid_spec_manifest` and `aoahid_node_manifest` return:

- Input capability (one), Output and Feature-transport capability (zero);
- evidence-conscious Android status;
- profile kind;
- each accepted Input Report ID and exact wire length;
- descriptor length.

A descriptor can contain a declarative Constant Feature item such as Contact
Count Maximum while `feature_transport_supported` remains zero. That metadata
does not add an Output/Feature transport operation or a Feature-response path
back to this host library.

## Close sequence

Close waits for an existing report, completes a profile-neutral/release report
while the device is present, then sends request 55. Device/context teardown does
not free a transfer, buffer, handle, node, device, or runtime before terminal
callbacks. A bounded nonblocking close can return `AOAHID_CLOSE_PENDING`; use
the blocking context close with an explicit deadline when ownership must end in
one call.

Ownership differs by handle: only `AOAHID_OK` from `aoahid_node_close`
consumes the Node. Any failure leaves it live and caller-owned so it can be
retried; `AOAHID_CLOSE_PENDING` specifically identifies an expired drain
budget. `aoahid_channel_close` follows the same rule. The first
`aoahid_device_close` consumes the Device and every child Node and Channel even
when it returns pending; their graph moves to the Context graveyard and is freed
only after the Device's and every Channel's transfers have completed.
`aoahid_context_destroy` returning `AOAHID_CLOSE_PENDING` leaves the Context
valid and caller-owned. This covers both an outstanding callback and a Device
graph that could not yet transfer safely into teardown. A deadline-expired
`AOAHID_ERR_TIMEOUT` from `aoahid_context_destroy_blocking`, identified by the
`context.destroy` field and its deadline diagnostic, also retains the Context.
The same result code can instead expose a previously retained unregister,
completion, or event-pump timeout after terminal cleanup; that diagnostic keeps
its original field and the call consumes Context. Every other result from a
valid destroy call is terminal and consumes Context, including another retained
unregister, cancel, completion, event-pump, or internal teardown error returned
only after all ownership was released.

## Errors

`aoahid_last_error()` returns one thread-local diagnostic record without
altering it. Every other public C function replaces the calling thread's
record. Success clears stale detail to `AOAHID_OK`; a returned error, a failure
sentinel from an accessor, or an invalid call to a `void` ownership function
records a concrete field and reason, plus libusb status, request, HID ID,
Report ID, offset, and length when applicable. Its string pointers remain valid
until the next public call on that thread.

When their output pointer itself is nonnull, `aoahid_discover` and
`aoahid_device_open` set the pointed-to handle to null before validating any
other argument. It therefore remains null on every failure path, including an
exception translated at the C boundary.

The internal event thread never writes an application caller's TLS record.
Terminal request-57 metadata is retained on the Node and Device and rebuilt in
the caller thread when a blocking submit, the next submit, or
`aoahid_device_latched_error()` exposes it. The first internal event-handler
failure is retained on the Context and delivered by a subsequent Context-using
call; the event thread continues after an ordinary backend error and rate-limits
that failure path. If the event thread itself terminates, its error remains a
persistent fatal preflight result so no new asynchronous work is accepted
without a pump. If cancelled transfers remain during blocking teardown, the
destroy call first joins the failed event thread and only then becomes the sole
short-quantum libusb event pumper until callbacks and graveyard objects drain.
A successful fire-and-forget submit remains successful if its
opportunistic poll fails after libusb accepted the transfer; that poll error is
deferred to the next Context-using call.

Device and Context teardown retain the first unregister, cancellation, terminal
completion, or event-pump error in fixed-size internal storage while cleanup
continues. Graveyard reaping publishes that exact result, native libusb status,
request, HID ID, Report ID, offset, and length on an application thread instead
of discarding callback-thread TLS.

`AOAHID_ERR_STALL` means the control request stalled; it does not by itself prove
descriptor rejection. `AOAHID_ERR_SHORT_TRANSFER` compares control
`actual_length` with payload length, excluding the eight-byte setup packet.
An asynchronous `LIBUSB_TRANSFER_OVERFLOW` completion maps specifically to
`AOAHID_ERR_OVERFLOW`, preserving libusb's "device sent more data than
requested" status rather than collapsing it into generic I/O. A successful
request-51 probe that returns fewer than the required two protocol bytes is a
capability failure and returns `AOAHID_ERR_NOT_AOA`; its diagnostic retains the
observed positive byte count and expected length 2. Other short AOA control
requests remain `AOAHID_ERR_SHORT_TRANSFER`. `AOAHID_ERR_NO_DEVICE` is sticky
for the device.

No C++ exception crosses the public C ABI. Allocation failure and any other
unexpected internal exception are caught at the exported function boundary,
reported as `AOAHID_ERR_INTERNAL`, and recorded with the exact API field plus a
concrete allocation/internal-operation reason. Functions whose return type has
a failure sentinel return that sentinel; `void` ownership/reference functions
record the failure in `aoahid_last_error()`. Partially constructed Context,
Discovery, Device, and Node graphs unwind their owned USB and HID resources
before the error is returned.

## Logging

Logging is disabled unless the Context supplies both a non-disabled level and
a sink. Lifecycle and other cold-path records use Info or Error. Submission,
report acceptance, and completion contain no logging calls. Cold-path records contain available bus/port path, physical
VID/PID, AOA version and startup mode, HID/Report IDs, request, offset/length,
result, and native status. Report payload bytes are never logged.

The sink runs synchronously in the domain that produced the record, including
the internal event thread for asynchronous completion. It must return promptly
and must not re-enter or destroy the same Context; it must also tolerate
concurrent calls when the application uses the internal-thread mode. The library holds no
transport-state or Context-container lock while calling the sink.
