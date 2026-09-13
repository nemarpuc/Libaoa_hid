# libaoahid — Architecture and Design Specification

**Status:** design baseline for implementation
**Normative source:** `docs/AOA_HID_GUIDE.md` (*Android Open Accessory 2.0 HID: Complete Implementation Guide*, verification date 2026-08-27)
**Predecessor:** `libaoa_touch` v1 — touch-only, superseded. Its transport techniques are carried forward; its API, its option model and its documentation are not.

---

## 0. What this document is

This is the complete design the implementation must follow. It fixes the object model, the threading model, the option model, the profile catalog, the automatic-behavior layer, the error model, the file layout, and the build and release pipeline.

It does not restate the protocol. Every protocol fact comes from `docs/AOA_HID_GUIDE.md` and is referenced by section number, for example *(guide §5.1)*. Where this document and the guide disagree, **the guide wins** and this document is a bug.

### 0.1 Goals

1. Cover every HID device profile that the guide classifies as expressible and carryable over AOA Input reports.
2. Be measurably faster and lighter than the predecessor: no allocation, no locking, no syscall on the send path beyond the one `ioctl`/`WriteFile` that libusb itself performs.
3. Automate every behavior that exists only because Android and the Linux HID/Input stack behave the way they do, so the caller never has to know them.
4. Expose every value the HID descriptor can carry as a caller-set parameter, with **no defaults and no presets anywhere**.
5. Support multiple Android devices simultaneously, and multiple logical HIDs per Android device simultaneously.
6. Explain itself: every non-obvious decision carries a comment saying why it is that way.

### 0.2 Non-goals

- Output reports, Feature reports, keyboard LEDs, rumble, haptics, force feedback. The specified AOA gadget is Input-only *(guide §0.3, §7.1)*. These are absent from the API entirely, not present-and-failing.
- Profiles whose whole purpose requires the above: LED (page 0x08), Haptics (0x0E), PID (0x0F), Lighting (0x59), FIDO (0xF1D0), HID Sensors (0x20), Braille (0x41) *(guide §11, §21)*. The library does not generate descriptors for them.
- Android-side software. This is a host-side accessory library.
- Guessing what the caller meant. An unset option is an error, never a default.

---

## 1. Naming and repository identity

| Item | Value |
|---|---|
| Repository / library | `libaoahid` |
| C ABI prefix | `aoahid_` |
| Macro / enum prefix | `AOAHID_` |
| C++ namespace | `aoa`, with `aoa::hid` and `aoa::profiles` |
| Public header | `include/aoahid.h` (C), `include/aoahid.hpp` (optional C++ wrapper, header-only) |
| License | MIT for the library. libusb is LGPL-2.1; the default build links it **dynamically**. A statically linked artifact is produced separately and carries the LGPL relinking notice. |
| Minimum standards | C99 for consumers, C++20 for the implementation, CMake 3.20 |

No public symbol may contain `default`, `preset`, `standard`, `typical`, or `auto_config`. CI enforces this with a lint over the exported symbol list (§13.4). The word *auto* is permitted only for the behavior layer of §8, which automates protocol obligations, never parameter choices.

---

## 2. Layering

```
+--------------------------------------------------------------+
| L4  bindings/   Python (ctypes), C# (P/Invoke), Rust (FFI)    |
+--------------------------------------------------------------+
| L3  api/        stable C ABI, handle validation, error detail |
+--------------------------------------------------------------+
| L2  profiles/   descriptor + layout + state machine per       |
|                 profile; all Android-specific automation      |
+--------------------------------------------------------------+
| L1  hid/        HID item writer, descriptor builder,          |
|                 report layout, static validator. Pure.        |
+--------------------------------------------------------------+
| L0  transport/  libusb, AOA requests 51-57, transfer pool,    |
|                 event pump, lifecycle                         |
+--------------------------------------------------------------+
```

Rules, per *guide §22*:

- `libusb.h` is included by L0 only. It never appears in a public header, and no libusb type crosses the C ABI.
- L0 never interprets HID content. It sees an AOA HID ID, a byte span, and a length.
- L1 and L2 perform no I/O, spawn no threads, and allocate nothing after construction.
- L2 never mentions libusb.
- A registered node owns its descriptor bytes, its AOA HID ID, its accepted report-length set, and its capability manifest. A serializer for profile A must be impossible to call with a node of profile B; the C ABI enforces this with a `kind` tag checked on every call, and the C++ wrapper enforces it with distinct types.

---

## 3. Object model

```
aoahid_spec             an immutable, validated profile definition.
                        Built once, owned by the caller, registerable on
                        any number of devices and contexts (§3.4).

aoahid_context          one libusb context, one synchronization domain
   |
   +-- aoahid_device    one Android device, one USB handle, one AOA session.
   |      |             Opened, operated and closed entirely on its own.
   |      +-- aoahid_node   one AOA HID ID = one logical HID = one live
   |      +-- aoahid_node   instance of one aoahid_spec. Registered and
   |      +-- aoahid_node   unregistered independently, at any time.
   |
   +-- aoahid_device    completely independent of the one above: its own
   |      +-- aoahid_node   pool, latch, IDs, state machines and lifetime
   |
   +-- (graveyard)      devices closed but not yet drained (§9.3)
```

There is no object above a device that owns several devices' behavior. Everything the library does is done to one device, or to one node of one device. Simultaneity across devices is produced by the caller issuing the same calls to each of them, and §3.5 explains why that is exactly equivalent to a broadcast without needing one.

### 3.1 Context

- Owns exactly one `libusb_context`. libusb officially supports sharing a context across devices and threads *(guide §30)*, so one context serving many phones is the normal configuration and one event pump advances all of them.
- Is the **synchronization domain**. See §5.
- Owns the log sink, the event mode, and the graveyard.

### 3.2 Device

- One `libusb_device_handle`, one AOA session, one transfer pool, one sticky-error latch.
- Startup mode is chosen by the caller: **Mode A** (normal USB mode, requests 54–57 on the OEM VID/PID, no re-enumeration) or **Mode B** (`SEND_STRING` → `START` → re-enumerate as 18D1:2D0x → reopen) *(guide §4)*. Mode A is what the guide recommends for HID-only operation; the library implements both and recommends neither by defaulting, since there is no default.
- The pool is per device rather than per node: one pool means one free list, one cache-resident set of buffers, and fair reuse across profiles. Per-node reservations (§7.4) prevent a chatty touchscreen from starving a keyboard.

### 3.3 Node

- One AOA HID ID, one descriptor, one report layout, one profile state machine.
- AOA HID IDs are `uint16_t` on the wire and are allocated by the library, monotonically, **never reused within one device session**, because unregistering is asynchronous: the kernel moves the object to `dead_hid_list` and a worker destroys it later, with no notification, so re-registering the same ID can briefly expose two nodes *(guide §5.1)*. The caller may read the assigned ID for log correlation but cannot choose it.
- An AOA HID ID is not a HID Report ID. The two are separate identifier spaces *(guide §3.1)* and the API keeps the names distinct everywhere.
- A node holds all *mutable* state: the profile state machine, the pending report, the dirty flag, the reserved slots. It holds no descriptor bytes of its own; it points at a spec.

### 3.4 Spec — one HID definition, many devices

Building a descriptor means running the item writer, computing the layout and running the full static validator. None of that depends on which phone the descriptor is going to, so doing it once per phone is wasted work and an opportunity for two phones to end up with subtly different descriptors.

`aoahid_spec` is therefore a separate, caller-owned object:

- Built once from an options struct, validated once, then **immutable**.
- Contains the descriptor bytes, the `ReportLayout` for every report ID, and the capability manifest.
- Contains **no** mutable state, no device pointer, no context pointer.
- Reference-counted, and safe to read from any thread precisely because nothing in it changes. It is the one object in the library that is not confined to a synchronization domain.
- Registerable on any number of devices, in any number of contexts, simultaneously.

`aoahid_node_open(device, spec, node_options)` binds a spec to a device. Any number of phones receiving the same keyboard definition run the builder and validator once, transfer identical descriptor bytes, and are guaranteed byte-identical because they literally share the bytes.

### 3.5 Per-device control, and why that is all you need

Every operation in the library acts on exactly one device, or on one node of one device. There is no batch call, no broadcast handle and no fan-out object, because none of them would be able to do anything the caller cannot do with a loop, and each of them would take away control the caller might want.

What is per-device and independently available at any time, while other devices keep running untouched:

| Operation | Granularity |
|---|---|
| Enumerate and probe | per candidate device |
| Open a session, in Mode A or Mode B | per device, with its own options |
| Register a node from a spec | per device, at any time after open |
| Every profile operation — keys, buttons, axes, contacts, pen state | per node |
| Submit | per node |
| Read the latched error and the link state | per device |
| Unregister a single node, leaving the rest running | per node |
| Register a replacement node on a live device | per device |
| Close the session | per device |
| Reopen after an unplug | per device |

A device that is unplugged, errors out, or is deliberately closed affects nothing else. Its pool, its error latch, its HID IDs, its node state machines and its lifetime are entirely its own, and no other device's timing changes because of it.

**Identical input produces identical behavior.** A node's wire output is a pure function of its spec and the sequence of calls made against it. Two devices holding the same spec, given the same call sequence, emit byte-identical reports — identical because they share the spec's descriptor bytes (§3.4) and because no per-device state feeds back into serialization. So a caller who wants N phones to do the same thing at the same time writes the loop:

```c
for (size_t i = 0; i < device_count; ++i)
    aoahid_keyboard_key_down(keyboard_node[i], AOAHID_USAGE_KEY_A);
for (size_t i = 0; i < device_count; ++i)
    aoahid_node_submit(keyboard_node[i]);
```

Submitting in a second pass, after every state machine has been updated, keeps one device's submit out of the next device's critical path and is the same ordering a built-in broadcast would have had to use. It is written out here rather than hidden inside the library so that the caller can also choose not to do it — skip a device, send a different key to one of them, or handle one device's `BUSY` without stalling the others.

A caller who wants a fan-out helper is welcome to write a ten-line one. The library does not, because a helper that decides what to do when device three fails mid-fan-out has made a policy choice on the caller's behalf, and §4 forbids that.

---

## 4. Option model: no defaults, no presets

This is the single most invasive rule in the design and it shapes every struct.

### 4.1 Mechanics

Every options struct is laid out as:

```c
typedef struct {
    uint32_t struct_size;      /* sizeof(this struct) as the caller saw it */
    uint32_t reserved;         /* must be 0 */
    ...                        /* fields, none optional-by-omission */
} aoahid_xxx_options;
```

- `struct_size` gives forward ABI compatibility: a newer library reading an older caller's struct sees a smaller size, knows which fields exist, and rejects the call if a field it now requires is absent. A mismatched or implausible `struct_size` is `AOAHID_ERR_PARAM`.
- The caller zero-initializes the struct and fills **every** field. The library validates every field.
- Where zero is not a meaningful value (widths, ranges, counts, timeouts), zero means "not set" and produces `AOAHID_ERR_UNSET_FIELD`, not a default.
- Where zero **is** meaningful (`logical_minimum`, a reserved slot count, a button index), the field is paired with an explicit `has_*` or `enable_*` flag, so intent is always encoded rather than inferred. A flag must be exactly 0 or 1.
- There is no `aoahid_*_options_init()`, no `_default()`, no `_preset()`, no profile template. Documentation carries fully worked examples instead, in `docs/EXAMPLES.md`, as prose the caller copies and edits rather than as code the caller calls.

### 4.2 Diagnosing an unset field

Rejecting a struct with a bare error code would make this model hostile. Every validation failure records the offending field:

```c
typedef struct {
    int32_t     code;            /* AOAHID_ERR_* */
    const char *field;           /* "touchscreen.contact.pressure.logical_maximum", or NULL */
    const char *reason;          /* static string, never NULL */
    int32_t     libusb_status;   /* raw libusb code, or 0 */
    int32_t     aoa_request;     /* 51..57, or 0 */
    uint16_t    hid_id;
    uint16_t    report_id;
    uint32_t    offset;
    uint32_t    length;
} aoahid_error_detail;

AOAHID_API const aoahid_error_detail *aoahid_last_error(void); /* thread-local */
```

The detail is thread-local and valid until the next failing call on the same thread. Strings are static; nothing is freed.

### 4.3 What the caller must specify

Non-exhaustive, to show the scale of the rule:

- Context: event mode, log level, log sink.
- Device: startup mode, every Mode-B string that will be sent, control timeout, send timeout, descriptor fragment size, pool slot count, maximum report bytes, close drain budget, whether send-time validation runs.
- Every profile: every usage, every logical minimum and maximum, every report size in bits, every report count, every physical range and unit if declared at all, every report ID, and every enable flag for every optional field.

Bit widths are never inferred from a range, and ranges are never inferred from a bit width. Both are stated and the validator proves they agree (§6.3).

---

## 5. Threading and the event model

### 5.1 Domain rule

**A context is a synchronization domain.** Everything under one context — its devices, nodes, pools, latches and state machines — is driven by one thread at a time. The library takes no lock on the send path in this mode.

Stated plainly: a synchronization domain is the region of the library that only one thread may be inside at any moment. The library buys its latency by having no locks on the send path, and the price of that is this rule. It is not a limit on how many phones can be driven — one thread can drive any number of devices in one context — only on how many threads may touch the same context. A caller who wants real parallelism creates one context per thread, and libusb, which is thread-safe and supports multiple contexts, keeps them apart. The one object exempt from this rule is `aoahid_spec` (§3.4), because it is immutable and therefore has nothing to race on.

Two ways to use multiple threads:

1. **One context per thread.** Threads never meet, there is zero contention, and each thread has its own event pump. This is the lowest-latency multi-device configuration and is what the predecessor did implicitly by giving each device its own libusb context.
2. **One context, one driving thread, many devices.** One pump advances every device. This is the lowest-overhead configuration for many phones and the one the guide describes first *(guide §30)*.

Mixing — calling into one context from two threads without external synchronization — is undefined and is documented as such.

### 5.2 Event mode is a required choice

`aoahid_context_options.event_mode` must be one of:

- `AOAHID_EVENT_CALLER_POLL` — the library starts no thread. Completions are reaped inside `aoahid_context_poll()` and, opportunistically, at the end of each submit. Zero threads, zero locks, deterministic jitter. This is what the predecessor did and it is the fastest path.
- `AOAHID_EVENT_INTERNAL_THREAD` — the context starts one event thread. libusb permits submitting from one thread while another handles events, but *our* shared state (free list, inflight counter, error latch, node state) then needs protection, so this mode compiles in a per-device spinlock around pool and latch operations. The cost is tens of nanoseconds per submit and it is documented in the header at the point of the enum.

The mode is fixed at context creation so the hot path has no mode branch.

### 5.3 Pump fairness

With several devices on one context, `libusb_handle_events_timeout_completed()` advances all of them, so a device that is not being flushed still makes progress as long as *some* device is driven. If no device is driven, nothing progresses; the caller must still call `aoahid_context_poll()` *(guide §30)*. The header says so where `poll` is declared.

---

## 6. L1 — HID core

### 6.1 Item writer

A sequential writer over a caller-supplied fixed buffer with sticky failure, carried over from the predecessor. It emits HID short items *(HID 1.11 §6.2.2.2)* in the smallest encoding that survives the parser's sign extension: a field is unsigned when both Logical Minimum and Logical Maximum are non-negative, so with a minimum of 0 the value 65535 fits in `0x26 FF FF` and a four-byte item is not required *(guide §1, §10.3)*. The predecessor's rule of "smallest size that stays positive when read as signed" is retained but is now applied per item using the *declared* minimum, not globally.

Long items are not emitted. Nothing in the profile catalog needs them.

### 6.2 Descriptor builder

Profiles do not write raw bytes. They describe their collections declaratively and the builder emits items and, in the same pass, produces the `ReportLayout` that the serializer uses. Descriptor and layout are generated together so they cannot disagree — the predecessor's most valuable structural decision, and it is kept and generalized.

`ReportLayout` records, per report ID: total wire length, whether an ID prefix is present, and for every field its byte offset, bit offset, bit width, signedness, logical range, and endianness. Serialization is then straight stores with no HID knowledge at runtime.

### 6.3 Static validator

Runs on every generated descriptor, including caller-supplied raw ones, before registration *(guide §25)*:

| Check | Failure |
|---|---|
| Descriptor length outside 1–65535 | `OVERFLOW` |
| Descriptor length above the caller's stated portability policy | `OVERFLOW`, with a message saying this is a policy, not an AOA wire limit |
| Report ID 0 declared, or first ID declared after a Main item, or ID and non-ID layouts mixed | `PARAM` |
| Logical minimum/maximum disagrees with serializer signedness | `PARAM` |
| Report Size × Report Count disagrees with the emitted report bits | `INTERNAL` |
| A field spans more than four bytes, or a 32-bit field does not start on a byte boundary | `PARAM` |
| Wire length ≠ ceil(data bits / 8), or non-zero trailing pad bits | `PARAM` |
| A top-level collection is not an Application collection, or one report crosses two TLCs | `PARAM` |
| Collection nesting unbalanced, or global state stack corrupt | `INTERNAL` |
| Profile requires Output or a Feature response | `UNSUPPORTED` |
| Field count in one report type + ID exceeds the caller's stated kernel-field policy | `OVERFLOW` |

Explicit Constant padding is emitted for readability, and the validator also accepts specification-compliant implicit zero padding in raw descriptors *(guide §25)*.

Descriptor byte-level limits are kept in four separate, separately-named policy fields so they are never conflated *(guide §6, §44)*: AOA wire limit (1–65535), Linux UAPI/transport policy (commonly 4096), Android EP0 data buffer, and libusb/OS control buffer minus the 8-byte setup packet.

### 6.4 Raw escape hatch

`aoahid_node_open_raw()` takes caller-supplied descriptor bytes plus an explicit table of accepted report IDs and lengths, runs the same validator, and refuses if the caller has not set `acknowledges_no_android_support = 1` and `requires_output = 0` and `requires_feature_response = 0`. The capability manifest for a raw node reports `android_status = unknown`.

---

## 7. L0 — transport

### 7.1 Discovery

`GET_PROTOCOL` (request 51, IN, two little-endian bytes) classifies every candidate *(guide §2)*:

| Result | Classification |
|---|---|
| STALL / `LIBUSB_ERROR_PIPE` | `NOT_AOA` — an ordinary negative probe, never a fatal I/O error |
| Exactly two bytes, value 0 | `NOT_AOA` |
| Short transfer | `NOT_AOA`, candidate excluded |
| Value 1 | `ERR_VERSION` — AOA 1.0, no HID |
| Value 2 | usable |
| Value > 2 | usable only when the caller set `accept_future_protocol_versions = 1`, and logged |

Enumeration reports bus, port path, address, physical VID/PID, serial where readable, and product string, in stable bus/port order so a device identifier survives a reboot of the phone.

### 7.2 Registration

Per *guide §5.1*:

- `REGISTER_HID` (54) carries the HID ID in `wValue` and the total descriptor length in `wIndex`.
- `SET_HID_REPORT_DESC` (56) fragments must arrive strictly ascending with `wIndex` exactly equal to the bytes already accepted. Duplicates, gaps, rewinds and reordering are rejected by the gadget, so the sender never retries a fragment out of order; a failed fragment aborts the whole registration.
- Fragment size is a required caller field. 64 bytes is a conservative implementation policy, not a protocol constant, and the header says so.
- After the final fragment the kernel registers the HID **asynchronously** and there is no ready callback. The first `SEND_HID_EVENT` may therefore STALL. Only the first event of a node is eligible for a bounded retry with backoff, with the attempt count and backoff supplied by the caller. Any later STALL is an error *(guide §5.1, §26)*.
- A successful descriptor transfer does not prove `hid_parse_report()` succeeded. Diagnostics must distinguish "device removed" from "descriptor rejected" and say when they cannot.

### 7.3 Send path

One report per control transfer, never fragmented, because request 57 has no offset or sequence field *(guide §6)*.

```
[ setup: bmRequestType=0x40, bRequest=57, wValue=HID ID, wIndex=0, wLength=N ]
[ report: N bytes                                                            ]
```

Latency techniques, all carried from the predecessor unless marked new, each of which must appear in the code with a comment saying why:

1. **One allocation per object.** A device allocates once at open; every hot-path structure is a fixed-size member.
2. **Fixed transfer pool with a LIFO free stack.** O(1) acquire and release, and the most recently freed slot is the cache-warmest.
3. **Zero copy.** The serializer writes directly into the transfer's payload region, which sits contiguously behind the setup packet in the same buffer, so the report is never copied between the state machine and libusb.
4. **Pre-filled setup packets (new).** `bmRequestType`, `bRequest` and `wIndex` are constant for the life of the pool and `wValue` is constant per node, so they are written once at pool init. Only `wLength` changes per submit. The predecessor rebuilt the whole setup packet on every send.
5. **Dirty-state suppression.** A state update that changes nothing leaves the node clean and produces no USB traffic at all.
6. **Reap after submit, never before.** Recycling completed transfers is bookkeeping that nothing is waiting for, so it happens after the report is on the wire.
7. **Non-blocking reap.** `libusb_handle_events_timeout_completed()` with a zero timeval.
8. **Errors are latched, not returned inline.** Transmission is fire-and-forget; a failure of an earlier report surfaces at a later submit or poll. `NO_DEVICE` is sticky and permanent, everything else is reported once and cleared, and the pending state is retained so the next submit retries it.
9. **No stdio, no exceptions, no RTTI, no dynamic dispatch on the hot path.** Every hot function is `noexcept`. libusb's own logging is set to `LIBUSB_LOG_LEVEL_NONE` so the library never writes to a stream the host did not ask for.
10. **Precomputed layouts.** No HID parsing at send time; field offsets and widths are constants by then.
11. **Endian-explicit stores.** `memcpy` on little-endian hosts, shift-and-store elsewhere; never a struct memory image on the wire *(guide §24.2)*.
12. **Descriptors are built once, not once per device.** A spec (§3.4) is immutable, so registering it on N phones runs the item writer and the validator once and transfers the same bytes N times, instead of doing N independent builds that could differ.
13. **Interface claiming is platform-conditional.** On Windows the backend routes device-recipient control transfers through an interface handle, so interface 0 is claimed; on Linux EP0 is reachable without claiming and claiming would take the interface from a kernel driver that owns it. Claim only what was explicitly claimed and release only that *(guide §27.2, §31)*.

On pool exhaustion, submit returns `AOAHID_ERR_BUSY` without blocking, after one opportunistic reap. Blocking is available only through an explicit blocking submit variant with a caller-supplied deadline. Stale movement may be coalesced by the state machine; key, button and contact-lifecycle transitions are never dropped *(guide §29.1)*.

### 7.4 Reservations

Each node declares `reserved_slots` (0 is a legitimate value). Reserved slots are only available to that node, so a high-rate touchscreen cannot starve a keyboard on the same device. The sum of reservations must be less than the pool size, leaving a shared remainder; violation is `AOAHID_ERR_PARAM`.

### 7.5 Completion handling

Per *guide §29.2*. `actual_length` for a control transfer is the data-stage length and excludes the 8-byte setup packet, so it is compared against `N`:

| Status | Action |
|---|---|
| `COMPLETED`, `actual_length == N` | success |
| `COMPLETED`, `actual_length != N` | `SHORT_TRANSFER` |
| `CANCELLED` | normal terminal state during close |
| `NO_DEVICE` | sticky fatal, nothing is ever submitted again |
| `STALL` | bounded retry only for a node's first report; otherwise `STALL` |
| `TIMED_OUT` | `TIMEOUT` |
| `ERROR`, `OVERFLOW` | `IO` |

No user callback runs inside the libusb callback. Results are latched or queued.

---

## 8. L2 — profiles and the automatic behavior layer

This is what makes the library "easy" without making it opinionated. **Parameters are never chosen for the caller; protocol obligations always are.** Everything in this section is an obligation imposed by Android, the Linux HID/Input stack, or the HID specification — never a stylistic choice.

### 8.1 Cross-profile automation

- **Report ID prefixing.** If a node's descriptor uses Report IDs, every report carries the one-byte prefix; if it does not, no zero byte is prepended *(guide §3.1)*. The caller never writes it.
- **Neutral state on close.** Before a node is unregistered, the library emits the profile's neutral report, because a HID device that disappears mid-input leaves Android believing the last state is still current: keys stay down, buttons stay held, fingers stay on the screen.
- **Freeze on loss.** Once `NO_DEVICE` is sticky, state machines stop accepting input and every call returns `NO_DEVICE` rather than accumulating state that will never be sent.
- **Send-time validation** *(guide §26)*, on when the caller sets `validate_reports = 1`: node/profile identity, prefix and value correctness, exact report length for the selected ID, every field inside its logical range with matching signedness, lifecycle correctness, and no fragmentation.

### 8.2 Touchscreen and touchpad

The predecessor's finger state machine is the reference implementation of this section and is generalized, not replaced.

- **Three-phase contact lifecycle: `None → Down → Up → None`.** The `Up` frame is mandatory and is the phase a naive implementation forgets. A contact cannot simply vanish from the report: Contact Count is the number of *valid* slots, so a contact that disappears is never observed to go up. It must appear once more with Tip Switch cleared, still counted, and only then be dropped *(guide §17.4)*.
- **Contact Count is never 0.** A frame with zero contacts makes the kernel reuse the previous count and misread the frame, so when nothing is left there is simply nothing to send — the previous frame already carried the lift.
- **Coordinates are retained across the lift.** The release frame reports the contact where it was let go, not at the origin.
- **Contact ID is stable for the life of a contact** and is reported with the final X/Y and `Tip Switch = 0` on release *(guide §1, §17.4)*.
- **Pressure is clamped to at least 1 while the tip is down**, when pressure is declared: zero pressure with the tip down reads as hovering and produces no touch events at all.
- **Slot compaction.** Only valid contacts occupy leading blocks, contiguously; the remainder of the fixed report is zeroed. A fixed-length report with compacted contacts is the layout the generic multitouch path handles most predictably.
- **Scan Time**, when the caller enables it, is maintained by the library at the declared resolution and unit, because a monotonically advancing counter is the point of the field and a caller-supplied one is a bug farm. Behavior differs across Android releases *(guide §17.5.1)*, which the header records.
- **Multi-packet frames**, when the caller enables them, split a frame with more contacts than one report can carry, carrying the total count in the first packet.
- **Explicit lift on close**, so no finger is left down when the node disappears.

Configurable and *never* chosen by the library: resolution, contact count 1–16 *(the 16 figure is an Android `MotionEvent` application policy, not a HID or kernel limit — guide §6)*, protocol form (one-contact MT, fixed MT, or target-specific pure single touch), pressure presence and range, width and height presence and range, azimuth, orientation, contact-count-maximum feature declaration, Report IDs, and every logical range.

`PureSingleTouchTargetSpecific` requires the caller to set `target_verified = 1`, because pure ST is not guaranteed to be classified DIRECT *(guide §24)*.

Touchpad is a separate profile with the same contact engine and a different collection strategy, because classification, not geometry, is what separates it from a touchscreen *(guide §18)*. Buttons that do not belong to the pointer collection go into a separate node, since extra buttons can change classification *(guide §1)*.

### 8.3 Keyboard and keypad

- **Slot management.** The caller presses and releases usages; the library maintains the key array or bitmap.
- **Automatic roll-over overflow.** In a 6KRO layout, a seventh simultaneous key fills all six slots with `ErrorRollOver` (0x01) as the HID specification requires, and recovers automatically when the count drops back. Callers should not have to know this exists.
- **Modifier separation.** Usages 0xE0–0xE7 are routed to the modifier byte automatically and never into the array.
- **Release-all on close**, because a stuck modifier on Android is invisible and miserable to debug.
- **No LED, no Output.** Not exposed, so it cannot be attempted *(guide §13.2)*.

Configurable: rollover form (6KRO, N-key with a caller-set bitmap size, or a caller-defined array length), the declared usage range, Report IDs, and whether a Consumer report shares the node.

The library does not translate text into keystrokes in its core. A string-to-usage helper is a separate, optional module under `contrib/`, is explicitly documented as US-layout-only and dependent on the target's KCM and IME, and is not part of the stable ABI, because Android key mapping depends on layout, KCM and IME and cannot be made portable *(guide §13.2)*.

### 8.4 Mouse, pointing stick, trackball

- **Delta splitting.** A relative movement larger than the declared logical range is split across consecutive reports automatically. Saturating instead would silently lose distance, and a caller who chose 8-bit deltas should not have to implement this.
- **Deltas are consumed on send.** After a report is submitted the accumulated delta resets to zero, so a repeated flush does not move the pointer again. This is the most common bug in hand-written relative-pointer code.
- **Buttons are level, not edge.** Every report carries current button state, releases are explicit, and all buttons are released on close *(guide §14.3)*.
- Wheel and pan follow the same splitting and consumption rules.

Configurable: delta bit width and signedness, button count, wheel and pan presence and ranges, resolution multiplier declaration, Report IDs.

### 8.5 Consumer control and system control

- **Automatic release.** A usage-selector field must return to 0 to release; the library exposes explicit press and release, and a tap helper that emits both.
- **Usage allow-list.** Only usages in the caller's declared list are accepted, because only a subset survives the path from `hid-input.c` to a Linux key code and then through the target `.kl` to an Android keycode *(guide §15.2, §24)*. The library never ships a list of its own.
- The header records that the system or a `MediaSession` may consume these events before an application sees them.

### 8.6 Gamepad, joystick, D-pad

- **Hat null state.** "Centered" is encoded as a value outside the logical range, as HID requires; the library computes it from the declared range so the caller cannot get it wrong.
- **Neutral on close.** Buttons cleared, hat null, sticks centered at the midpoint of their declared range, triggers at their minimum.
- **Axis validation** against declared range and signedness, with no silent wrapping.
- Trigger axes require the caller to state the Usage, the expected Linux code and the expected Android axis explicitly *(guide §24)*, because this mapping is where portability is actually lost.

Configurable: the full axis list with per-axis usage, bit width, signedness and range; button count and usage base; hat presence; whether the D-pad is a hat or buttons; Report IDs. Simulation Controls usages are permitted inside a Generic Desktop Game Pad or Joystick top-level collection, which is the form with the broadest support *(guide §11, page 0x02)*.

### 8.7 Pen and stylus

- **In Range and Tip are constrained together.** Tip down implies in range; hover is in-range with tip clear; departure is a single report with both clear. Illegal combinations are rejected rather than transmitted.
- **Eraser and tip are mutually exclusive** and the library enforces it.
- **Automatic departure on close.**
- Direct (on-screen) and indirect (tablet) modes change the collection and property strategy, not just the coordinate space, and are separate profile modes *(guide §19, §24)*.

Configurable: coordinate ranges, pressure presence and range, tilt, twist, barrel buttons, eraser, hover reporting, Report IDs.

### 8.8 Conditional profiles

Implemented, with `android_status = conditional` in the manifest and a header comment explaining the condition: barcode/MSR keyboard wedge *(guide §11, pages 0x8C/0x8E)*, camera control for the small supported key subset *(page 0x90)*, telephony for the small supported subset *(page 0x0B)*, and battery telemetry *(guide §20.2)*.

### 8.9 Capability manifest

Every node exposes *(guide §24.1)*:

```
input_supported             always 1
output_supported            always 0
feature_transport_supported always 0
android_status              portable | conditional | custom_system_only | unsupported | unknown
report_ids[]                declared IDs and their exact wire lengths
descriptor_bytes            length
```

Being able to generate a descriptor is not `portable`. The README's support table is generated from these manifests so documentation cannot drift from behavior.

---

## 9. Lifecycle

### 9.1 Open

1. Enumerate, probe `GET_PROTOCOL`, select by serial or bus/port path.
2. Mode A: proceed. Mode B: send the caller's strings, optionally `SET_AUDIO_MODE` **before** `START`, `START`, discard the handle, wait for re-enumeration within the caller's timeout, reopen *(guide §4.2)*.
3. Build and validate every node descriptor **before** any of them is registered, so a bad descriptor never leaves a half-configured device behind.
4. Allocate the pool. Register nodes in order, transferring each descriptor completely before that node's first report.

### 9.2 Run

Submit, poll, read latched errors. Nothing else.

### 9.3 Close

Ordered per *guide §31*:

1. Set the closing flag; stop accepting new submissions.
2. Emit each node's neutral report if the device is still present.
3. `libusb_cancel_transfer()` on every in-flight transfer.
4. Handle events until every transfer has reached a terminal callback.
5. `UNREGISTER_HID` for each node if the device is still present. A successful control transfer means the request was accepted and routing stopped — not that the input node has been destroyed. Failures are recorded and teardown continues.
6. Free only transfers and buffers whose callbacks have finished.
7. Release only interfaces that were explicitly claimed; close the handle; exit the context only with its last owner.

If the drain budget expires with transfers still in flight, `aoahid_device_close()` returns `AOAHID_CLOSE_PENDING` and the **entire** device object — buffers, transfers, callback state, handle, and its reference to the context — moves to the context graveyard. Nothing is freed. Freeing the object while leaking only the pool, as the predecessor did, is forbidden: a late callback writes into freed memory *(guide §31)*.

`aoahid_context_poll()` continues to drain the graveyard. `aoahid_context_destroy()` returns `AOAHID_CLOSE_PENDING` while the graveyard is non-empty; `aoahid_context_destroy_blocking(ctx, timeout_ms)` waits. A context that cannot be drained is a wedged backend and is reported as such rather than papered over.

---

## 10. Error model

```c
AOAHID_OK, ERR_PARAM, ERR_UNSET_FIELD, ERR_UNSUPPORTED, ERR_NOT_AOA, ERR_VERSION,
ERR_ACCESS, ERR_BUSY, ERR_NO_DEVICE, ERR_STALL, ERR_TIMEOUT, ERR_SHORT_TRANSFER,
ERR_DESCRIPTOR_REJECTED, ERR_IO, ERR_OVERFLOW, CLOSE_PENDING, ERR_INTERNAL
```

Matches *guide §32* with `ERR_UNSET_FIELD` added for §4. `NOT_AOA` is a negative probe result, not a failure. `VERSION` means a valid AOA response below the HID requirement.

### 10.1 Logging

A caller-supplied sink with a level, per context. Diagnostics carry bus and port, physical VID/PID, AOA protocol version, startup mode, AOA HID ID, Report ID, request number, offset and length, and the raw libusb status *(guide §32)*.

**Report payloads are never logged.** They can contain keystrokes, passwords and medical data. Payload logging requires both a compile-time flag and a runtime flag, and emits a warning line when enabled.

---

## 11. Multi-device and multi-node

- Many devices per context, or one context per thread: both are first-class (§5.1).
- Many nodes per device, each with its own HID ID and profile *(guide §24.3)*. A typical set is keyboard, mouse, consumer, gamepad, touchscreen and pen simultaneously; the IDs carry no Android meaning and exist to route inside the accessory implementation.
- Stable ID allocation order across connections makes host logs comparable between runs.
- Per-device state is fully independent: no global mutable state exists anywhere in the library, so nothing is shared between devices except the libusb context they were asked to share and the immutable specs they were asked to register.

### 11.1 Running N phones with the same HID set

End to end, for N phones each receiving an identical keyboard, mouse and touchscreen set, where N is whatever the host's USB bus and file-descriptor budget allow:

1. Build three specs — keyboard, mouse, touchscreen — once (§3.4). Descriptor generation and validation run three times in total, not three times per phone.
2. Open the devices. Each `aoahid_device_open()` is a separate call with its own options, so one phone can be in Mode A while another is in Mode B, with different timeouts and a different pool size, if that is what the caller wants. Put them all in one context, or split them across one context per thread if they are to be driven in parallel.
3. Register nodes. Each device gets its own node per spec, with its own AOA HID IDs, which need not match between devices and mean nothing outside their own device.
4. Drive them. Update every node, then submit every node (§3.5). Identical call sequences against a shared spec produce byte-identical reports, so N phones do the same thing at the same time without the library having a concept of "at the same time".
5. Handle each device on its own terms. One phone returning `BUSY` does not stop the others. One phone unplugged goes sticky `NO_DEVICE` and can be closed and reopened while the rest keep running, and rejoins by registering fresh nodes against the same unchanged specs.

Every step above is also available at a finer grain: add an eleventh phone mid-session, unregister just the touchscreen node on phone four, close phone seven while phones one through six are mid-drag. Nothing in the library couples them.

### 11.2 Hot plug and reconnection

- A device that disappears sets its own sticky `NO_DEVICE`. Its node state machines freeze rather than accumulating input that will never be sent, and every call against it returns `NO_DEVICE` immediately, so a caller looping over devices is not slowed down by a dead one.
- Closing and reopening is a per-device operation with no global effect. AOA HID IDs restart from the beginning of the allocation order for a new session, since the previous session's kernel-side nodes are destroyed with the gadget.
- Specs survive everything. A reconnecting phone registers the same immutable spec it had before; nothing is rebuilt, revalidated or re-derived.
- Discovery may be re-run at any time while other devices are open, because probing a candidate touches nothing that belongs to an already-open session.

### 11.2 No count limits

The library imposes no maximum on the number of contexts, devices per context, nodes per device, or specs. These are bounded by the host's USB bus, file descriptors and memory, and by nothing the library invented. Every such collection grows dynamically; none is a fixed-size array sized by a compile-time constant.

The only counts with a stated ceiling are the ones the protocol or the platform imposes and that the guide documents: the AOA HID ID space of `uint16_t`, the descriptor wire length of 1–65535 bytes, the report length that must fit one control transfer, and the 16-pointer `MotionEvent` policy for touch contacts *(guide §6)*. Each of those is annotated with its origin so that no reader mistakes it for a library limitation.

---

## 12. Repository layout

```
libaoahid/
  CMakeLists.txt
  CMakePresets.json
  LICENSE
  README.md                     generated support table, quick start
  CHANGELOG.md                  Keep a Changelog format
  CONTRIBUTING.md
  SECURITY.md
  THIRD_PARTY_NOTICES.md
  .clang-format
  .clang-tidy
  .gitignore
  include/
    aoahid.h                    stable C ABI, the only required header
    aoahid.hpp                  optional header-only C++ wrapper
  src/
    transport/                  usb_device, aoa_requests, transfer_pool, event_pump, discovery
    hid/                        item_writer, descriptor_builder, report_layout, validator, usages
    profiles/                   keyboard, mouse, consumer, gamepad, touchscreen, touchpad, pen,
                                barcode, camera, telephony, battery, raw
    api/                        c_api, handle_table, error_detail, logging
  bindings/
    python/                     ctypes wrapper, tests, pyproject.toml
    csharp/                     P/Invoke wrapper, csproj
    rust/                       -sys crate plus safe wrapper
  examples/
    c/, python/, csharp/, rust/
  tests/
    unit/                       descriptor golden files, layout, state machines, validator
    fuzz/                       descriptor builder, report serializer, raw validator
    integration/                mock transport, lifecycle, teardown races
    hidtools/                   Linux uhid/hid-tools parse checks
  tools/
    device-check/               on-device verification scripts: getevent, dumpsys input, test app
    symbol-lint/                exported-symbol policy check
  docs/
    AOA_HID_GUIDE.md            the normative guide, verbatim
    DESIGN.md                   this document
    API.md, PROFILES.md, EXAMPLES.md, LATENCY.md, LIMITS.md, TARGET_MATRIX.md, PORTING.md
  udev/
    51-aoahid.rules
  cmake/
    toolchains/, aoahid-config.cmake.in
  .github/
    workflows/                  ci.yml, release.yml, docs.yml
    ISSUE_TEMPLATE/
```

---

## 13. Build, CI, release

### 13.1 Build

CMake ≥ 3.20, targets `aoahid::aoahid`. Options: `AOAHID_BUILD_SHARED`, `AOAHID_BUILD_STATIC`, `AOAHID_BUILD_TESTS`, `AOAHID_BUILD_EXAMPLES`, `AOAHID_BUILD_FUZZ`, `AOAHID_SANITIZE`, `AOAHID_BUNDLE_LIBUSB`, `AOAHID_ALLOW_PAYLOAD_LOGGING`. Exceptions and RTTI are disabled. Warnings are errors. Exported symbols are restricted by a version script on ELF and `__declspec` on Windows; nothing but the documented ABI is visible.

Installs a CMake package config and a pkg-config file.

### 13.2 Targets

Linux x86_64 and aarch64 (glibc and musl), Windows x86_64 and aarch64, macOS arm64 and x86_64.

### 13.3 CI on every push and pull request

Build matrix; unit, integration and fuzz-smoke tests; ASan+UBSan on Linux and macOS; TSan on the internal-thread event mode; `clang-format --dry-run --Werror`; `clang-tidy`; the symbol lint of §13.4; the hid-tools descriptor parse job on Linux; a documentation job that regenerates the README support table from the capability manifests and fails if it differs from what is committed.

### 13.4 Symbol and policy lint

Fails the build when an exported symbol contains `default`, `preset`, `standard` or `typical`; when a public struct lacks `struct_size`; when a public header includes `libusb.h`; or when a profile factory can succeed with a zero-filled options struct.

### 13.5 Release

A pushed `v*` tag builds every target, runs the full gate, and publishes a GitHub Release containing per-platform shared and static archives, headers, the CMake package config, an SPDX SBOM, SHA-256 checksums, and the changelog section for that version. Python wheels and a NuGet package are published from the same workflow, gated on a repository variable so packaging problems cannot block a library release. Doxygen output is published to GitHub Pages by a separate workflow.

### 13.6 Release gate

Per *guide §40*, and the release workflow refuses the tag unless all hold: every profile descriptor passes static parse and semantic tests; setup-packet and short-transfer tests pass for requests 51–57, and 58 with its pre-`START` ordering if the audio option is built; post-registration race, STALL retry, unregister and re-registration tests pass; every asynchronous cancel callback drains with no use-after-free or data race under ASan and TSan; the public API and documentation state that Output and Feature transport are unsupported; the target matrix is current; and source URLs and the verification date in the guide are current.

The phrase "Android supported" does not appear in release notes for anything not verified at the `getevent`, `dumpsys input` and application-API layers on real hardware *(guide §37)*.

---

## 14. Testing

- **Golden descriptors.** Every profile, over a spread of option combinations, byte-compared against committed expected output, so an unintended descriptor change is always visible in review.
- **Layout property tests.** For every generated layout, every field's declared bit offset and width match where the serializer actually writes.
- **State machine tests.** The obligations of §8 are each a named test: the mandatory `Up` frame, never emitting Contact Count 0, coordinate retention across a lift, contact ID stability, roll-over overflow and recovery, delta splitting and consumption, hat null value, In Range and Tip constraints, neutral state on close.
- **Mock transport** replacing libusb, verifying exact request numbers, `wValue`, `wIndex`, fragment offsets, ordering, and the first-report retry.
- **Fuzzing** of the descriptor builder, the report serializer and the raw validator.
- **Sanitizers**, including a teardown race suite that cancels transfers under load.
- **hid-tools** parse of every generated descriptor on Linux CI.
- **On-device**, scripted but manual: four-level verification per *guide §37* — kernel device, Android classification, event stream, application API — recorded in `docs/TARGET_MATRIX.md`.

---

## 15. Code and comment policy

The predecessor's comment style is the standard: prose in natural English that explains **why**, not what.

- Every file opens with a comment stating its responsibility and its boundary.
- Every non-obvious decision carries a comment explaining the reason, and cites the guide section where the reason comes from.
- Anything that exists only because of Android or Linux kernel behavior says so explicitly, so a future reader does not "simplify" it away. The `Up` phase, the never-zero contact count, the pressure floor of 1, the platform-conditional interface claim, the first-report retry and the graveyard all fall in this class.
- Comments never restate the code. `i++ // increment i` is a review rejection.
- No commented-out code, no TODO without an issue number.
- American English, present tense, complete sentences.
- Public headers document every function's ownership, blocking behavior, thread-safety domain, and every error code it can return.

---

## 16. Summary of decisions taken on the caller's behalf

Only these, and each is a protocol or platform obligation rather than a parameter choice:

1. AOA HID IDs are allocated by the library and never reused within a session (asynchronous unregistration).
2. Report ID prefixes are inserted or omitted according to the descriptor.
3. Contact, key, button, hat, axis and pen state machines maintain the transitions Android requires, including neutral state at close.
4. Descriptor fragments are sent strictly ascending with exact offsets.
5. The first report after registration may be retried within the caller's stated bounds.
6. Reports are never fragmented across AOA requests.
7. Interface 0 is claimed on Windows only.
8. Nothing is freed before its libusb callback has run.

Everything else — every range, every width, every count, every timeout, every usage, every mode — is the caller's, and is required.
