# Limits and Policy Boundaries

## Why the limits are separated

A USB specification limit, a constant in one Linux revision, an Android application policy, and a library allocation policy are different kinds of evidence. This document keeps them separate. A value observed in the specified Linux source is not promoted to a USB requirement, and a library safety bound is not described as an Android or kernel maximum.

The evidence labels and source register are defined in `FACT_AUDIT.md`. All cited sources were retrieved on 2026-08-27. No limit in this document has been validated against a physical Android target.

## USB HID 1.11 constraints

| Subject | Constraint | Evidence and source | Library consequence |
|---|---|---|---|
| Short-item data size | 0, 1, 2, or 4 bytes | **[USB HID 1.11 requirement]** §§5.3 and 6.2.2.2, [HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf) | The item writer does not invent a 3-byte short item. |
| Declared Report ID | Nonzero eight-bit value; zero is reserved | **[USB HID 1.11 requirement]** §6.2.2.7 | An enabled Report ID is 1 through 255. A descriptor with no Report ID item is a separate valid form and has no prefix byte. |
| Report identity | Report type plus Report ID | **[USB HID 1.11 requirement]** §§6.2.2.7 and 8 | General validation keys Input, Output, and Feature layouts separately. The public AOA profile serializer is Input-only. |
| First Report ID placement | Before the first Input, Output, or Feature item belonging to an identified report | **[USB HID 1.11 requirement]** §6.2.2.7 | An Application Collection may precede the Report ID. |
| Field byte span | No individual field spans more than four bytes | **[USB HID 1.11 requirement]** §8.4 | Apply the field-span formula below to each Report Size element, not to the entire report. |
| 32-bit alignment | A 32-bit field begins on a byte boundary | **[USB HID 1.11 requirement]** §8.4 | Reject a 32-bit field whose bit offset is not divisible by eight. |
| Short-item integer range | Determined by the item's signed or unsigned interpretation and 1-, 2-, or 4-byte payload | **[USB HID 1.11 requirement]** §§5.8 and 6.2.2.7 | A positive Logical or Physical Minimum must still survive signed parsing. A nonnegative Maximum follows an unsigned Minimum. |
| Unit Exponent | Signed four-bit value, -8 through 7 | **[USB HID 1.11 requirement]** §6.2.2.7 | Emit the low-nibble form; do not use Linux's legacy full-byte compatibility form as the format contract. |
| Defined Global tags | Tags 0 through 11 are defined; 12 through 15 are reserved | **[USB HID 1.11 requirement]** §6.2.2.7 | Raw validation accepts Physical/Unit metadata and rejects only reserved Global tags. |
| Local Usage encoding | One-/two-byte values are IDs on the current Usage Page; four-byte values embed page and ID; an extended Minimum requires an extended Maximum | **[USB HID 1.11 requirement]** §6.2.2.8; **[HUT 1.7 definition]** §3.1 | Strict raw rejects extended-Minimum/short-Maximum pairs and keeps specification semantics separate from exact-kernel expansion accounting. |
| Unit metadata | A non-None Unit requires declared Logical and Physical extents plus Unit Exponent | **[HUT 1.7 definition]** §3.3 | Strict raw state tracks all declarations through Global Push/Pop before a data Main item. |
| Hat Null | At least one representable encoding outside the logical range, with Null State set | **[USB HID 1.11 requirement]** §6.2.2.5; **[HUT 1.7 definition]** §4 | Reject a hat whose logical range consumes its entire encoded domain. |
| Report length | HID §8.4 does not impose a four-byte whole-report maximum | **[USB HID 1.11 requirement]** §8.4 | A report can contain many fields. Transport and target buffer policies are checked separately. |

### Field-span calculation

For a field with bit offset `o` and width `w`:

```text
bytes_touched = ceil(((o mod 8) + w) / 8)
```

The field is valid only when `bytes_touched <= 4`. If `w == 32`, `o mod 8` must also be zero.

| Example | Bytes touched | Result |
|---|---:|---|
| 32 bits at bit offset 0 | 4 | Accepted |
| 32 bits at bit offset 1 | 5 | Rejected |
| 31 bits at bit offset 1 | 4 | Accepted if its range fits |
| Six 8-bit Array elements | 1 per field; 6-byte report | Accepted by this HID §8.4 check |

The calculation is performed with checked arithmetic before adding a field to a report layout.

### Representable domains

For bit width `w`, where `1 <= w <= 32`:

```text
unsigned: 0 .. 2^w - 1
signed:   -2^(w-1) .. 2^(w-1) - 1
```

A logical range must fit the selected domain. Signed serialization uses two's complement. Bit width is explicit; it is not inferred from the range.

## Specified Linux revision limits

This section is **[Specified Linux implementation observation]** only. It describes Linux common commit [`35556bed836f8dc07ac55f69c8d17dce3e7f0e25`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/), dated 2020-09-01 and inspected on 2026-08-27. It does not describe every Android kernel or current upstream Linux.

| Symbol or path | Value or behavior | Exact source | Scope and consequence |
|---|---:|---|---|
| `HID_MAX_IDS` | 256 | [`include/linux/hid.h`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/include/linux/hid.h), symbol `HID_MAX_IDS`; `hid_parser_global` | Each report type has an array indexed 0 through 255. Explicit Report ID zero is rejected; index zero represents an ID-less report internally. |
| `HID_MAX_FIELDS` | 256 | `include/linux/hid.h`, symbol `HID_MAX_FIELDS`; [`hid-core.c`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-core.c), `hid_register_field` | Limit is per `struct hid_report`, therefore per report type and Report ID, not per device descriptor as a whole. |
| `HID_MAX_USAGES` | 12288 | `include/linux/hid.h`, symbol `HID_MAX_USAGES`; `hid-core.c`, `hid_parser_global` | The parser rejects a Report Count above this value and bounds local Usage storage. This is not a practical profile recommendation. |
| `HID_MAX_BUFFER_SIZE` | 8192 bytes | `include/linux/hid.h`, symbol `HID_MAX_BUFFER_SIZE` | A Linux HID helper buffer bound. It is not the AOA descriptor limit, not a promise about Android EP0, and not a portable report-size guarantee. |
| Global Push depth | 4 | `include/linux/hid.h`, symbol `HID_GLOBAL_STACK_SIZE`; `hid-core.c`, `hid_parser_global` | The fifth nested Push is rejected. The Spec records depth and registration applies the caller's exact-target policy. |
| Report Size parser check | At most 256 bits per declaration | `hid-core.c`, `hid_parser_global` | The check happens when declared, even if overwritten before a Main item. The parser can still accept more than HID §8.4 permits for a used field; strict profiles retain the specification limit. |
| Report data size | At most 65,528 bits per report identity | `hid-core.c`, `hid_add_field`; `(HID_MAX_BUFFER_SIZE - 1) << 3` | Data bits exclude the possible Report ID byte. This is independent of AOA, target EP0, and host control-buffer policies. |
| Global reserved tags | Rejected | `hid-core.c`, `hid_parser_global` default case | This matches HID 1.11 tags 12-15. The strict raw subset separately rejects reserved Main/Local tags and type 3 by policy even though this kernel ignores those categories. |
| Long-item recognition | Every prefix with tag 15 is long; long format rejected | `hid-core.c`, `fetch_item` and `hid_open_report` | Raw validation checks the high nibble, not only canonical prefix `0xfe`. |
| Local Usage timing | `hid_add_usage` runs on each parsed Usage; capacity is not sampled only at Main items | `hid-core.c`, `hid_parser_local` and `hid_add_usage` | Requirements are updated after every Usage/range, including trailing Local state. |
| Usage-range page composition | Range arithmetic uses raw Minimum/Maximum values; `hid_add_usage` appends the current page only when the Maximum item size is at most two bytes | `hid-core.c`, `hid_parser_local`, `hid_add_usage`, `complete_usage` | Short page `FFFF`, range `FFFE..FFFF` requires 2 entries. Short Minimum 1 plus extended Maximum `0001:0002` has untruncated demand 65,538; the target clips table population at `HID_MAX_USAGES`, so the library records the demand for pre-registration policy rejection. |
| Standalone Usage Maximum | Zeroed Local state makes a standalone short Maximum `n` expand raw `0..n` | `hid-core.c`, `hid_open_report`, `hid_parser_main`, `hid_parser_local` | HID 1.11 does not define a default Minimum or expressly forbid standalone Maximum; acceptance is recorded only as an exact-revision observation. |
| Raw extended Maximum wrap case | A raw four-byte Maximum `UINT32_MAX` can wrap the inclusive iterator for some Local state; the capacity branch can rewrite the bound first | `hid-core.c`, `hid_parser_local` | The validated raw subset conservatively rejects every such raw Maximum; it does not claim the target inevitably wraps. |
| EOF delimiter state | Nonzero depth rejected | `hid-core.c`, `hid_open_report` | Final raw balance includes Local delimiters. |
| Input extraction width | Widths above 32 warn and are capped to 32 | `hid-core.c`, `hid_field_extract` | Acceptance by the parser does not make a wide Input field safe or conformant. |
| Multitouch fallback contact count | Default 10 | [`hid-multitouch.c`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-multitouch.c), `MT_DEFAULT_MAXCONTACT` and `mt_input_configured` | Used when no other source establishes a maximum. It is not a HUT maximum. |
| Multitouch Contact Count Maximum fallback ceiling | 250 | `hid-multitouch.c`, `MT_MAX_MAXCONTACT` and Contact Count Maximum handling | A Feature field's Logical Maximum is used as fallback only when it is at most 250. It is an exact-revision driver rule. |
| Default multitouch count handling | Accurate-count quirk enabled | `hid-multitouch.c`, `mt_classes` | Default class includes `MT_QUIRK_CONTACT_CNT_ACCURATE`; continuation behavior depends on the frame state. |
| Singular multitouch Usage list | Accurate-count quirk removed | `hid-multitouch.c`, `mt_post_parse_default_settings` | One-contact MT does not follow every multi-contact count branch unchanged. Explicit Tip=0 release remains the library contract. |
| Scan Time conversion | Delta multiplied by 100 | `hid-multitouch.c`, `mt_compute_timestamp` | The portable target profile accepts only a 100-microsecond Scan Time contract. |
| Battery support build option | Conditional on `CONFIG_HID_BATTERY_STRENGTH` | [`hid-input.c`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-input.c), `hidinput_setup_battery` and its compile-time branch | Without this kernel option, the audited helper is a no-op. This is not controllable by the host library. |
| Battery raw zero | Ignored | `hid-input.c`, `hidinput_update_battery` | A logical range containing zero does not prove that raw zero is published as 0% on this revision. |

Before changing a limit based on another kernel, record the exact revision and inspect the corresponding symbols again.

The public device options therefore require five independent exact-target
policies: `linux_hid_fields_per_report_policy`, `linux_hid_usages_policy`,
`linux_hid_global_stack_depth_policy`, `linux_hid_report_size_bits_policy`, and
`linux_hid_report_data_bits_policy`. They are caller-selected policies, not
defaults and not claims about Linux or Android generally. Generated and raw
Specs retain target-neutral maxima; registration compares them before request
54. For raw descriptors, Local Usage ranges, Report Counts, report bits, and
stack depth are summarized with checked arithmetic and are never expanded into
count-sized validation loops.

## Library policy limits

These rows are **[Guide policy]**. They are deliberate API or implementation bounds and are not attributed to USB HID, HUT, Linux as a whole, or Android as a whole.

| Policy | Current bound or rule | Reason and required wording |
|---|---|---|
| Touch contacts | 1 through 16 | Application-oriented policy aligned with the project's selected `MotionEvent` scope. It is not a HID or kernel maximum and remains **[Unverified on hardware]**. |
| Contacts per report | 1 through configured maximum | Without multi-packet mode, every configured contact fits one report. With multi-packet mode, first packet carries total count and continuations carry zero. |
| Touch lifecycle automation | `None -> Down -> Up -> None`; Contact ID remains occupied through successful final-packet completion | Contact Count is derived from emitted records, including Tip=0 Up records. A state mutation that would cross an in-flight or multi-packet boundary returns `AOAHID_ERR_BUSY`. Scan Time, when enabled, is derived once per frame from the explicit 100-microsecond counter contract and restarts after inactivity. **[Guide policy]** over the facts in `FACT_AUDIT.md` A-13 through A-17 and `SOURCE_CONFLICTS.md` T-18. |
| Touch pressure | Range includes 0 and at least 1 | Allows zero for hover/lift and positive pressure for contact. The serializer floors Tip=1 pressure to 1. |
| Pen pressure | Range includes 0 and at least 1 | Same contact/hover distinction. Omit pressure when it is not declared. |
| Pen Away/tool transition | Away wire fields In Range, Tip, pressure, Invert, and barrels are zero; an in-range pen/eraser change sends departure before re-entry | The caller still supplies and validates In Range, eraser selection, X/Y, and optional fields. X/Y, tilt, and Twist are not assigned a universal Away value. **[Guide policy]** informed by the exact implementation observation in `FACT_AUDIT.md` A-18. |
| Battery Strength | Ordered nonnegative range; explicit width 1 through 32 | HUT defines the range as a proportion. The exact values are caller supplied; Android visibility is conditional and unverified. |
| Battery known/unknown distinction | Known values include an in-range zero; unknown is optional and requires an out-of-range representable encoding plus Null State | `has_value = 1` preserves zero as known report data. `has_value = 0` is accepted only when Null support was declared and emits the deterministic out-of-range value. The audited kernel's raw-zero suppression remains a target observation, not library-side conversion. |
| Canonical Android Hat field | Logical `0..7`, Physical `0..315`, Unit Degrees `0x14`, exponent `0`, width 4; deterministic no-direction Null `15` | The metadata follows the Android contract recorded in `PROFILES.md`; choosing `15` among valid out-of-range encodings is **[Guide policy]**. Boolean adjacent pairs derive diagonals; Up+Down or Left+Right is rejected without changing state. |
| Raw or absent D-pad | Raw form has four independent one-bit OOC fields; absent form has no direction field | Raw simultaneous adjacent or opposite bits are preserved, not converted to an angle; absent form invents no direction. Both forms remain conditional and **[Unverified on hardware]**. |
| Controller axes | Explicit in-range sample and explicit neutral per axis | The state machine never infers an arithmetic midpoint, trigger role, deadzone, or Android mapping. Close restores the declared neutral. |
| Generated scalar field width | 1 through 32 | Matches strict HID §8.4 handling and the serializer's 32-bit value surface. |
| Keyboard full-NKRO bitmap lifecycle | Modifiers are separate; every declared nonmodifier Usage is its own one-bit Variable field | No Array, no slot count, and no ErrorRollOver overflow encoding exist to hit; every simultaneously pressed key within the declared range is reported at once. Modern Linux/Android input subsystems dispatch each bit as an ordinary `EV_KEY` event. Barcode wedge uses the selected keyboard state machine but no character/layout inference. **[HUT 1.7 definition]** §3.4.2.1 plus **[Guide policy]** edge handling; see `FACT_AUDIT.md` A-07. |
| Relative pointer backlog | One signed 64-bit pending total each for X, Y, Wheel, and AC Pan | Each report emits an independent in-range fragment; an accepted terminal completion consumes only that submitted fragment. Checked addition reports overflow instead of wrapping. Axis domains remain explicit product choices. **[Guide policy]** over `FACT_AUDIT.md` A-08. |
| Consumer/System allow-list | Nonempty, unique, and caller supplied; one explicit semantic and expected target event per Usage | The library does not infer a HUT type or claim that every Usage reaches Android. |
| Consumer/System wire fields | One exact one-bit Variable field per sparse Usage | Avoids declaring unlisted IDs and gives OOC/MC/OSC/RTC/Selector entries their selected flags. |
| Key-like semantic scope | Selector bitmap, OOC toggle/maintained, MC, OSC, and RTC only | Consumer, System, Camera, and Telephony share an accepted `1 -> 0` edge guard. Two-direction OOC, LC, DV, and NAry need value/direction/collection APIs and return `AOAHID_ERR_UNSUPPORTED`; no repeat or RTC cadence timer is inferred. |
| Declarative Feature metadata | A generated descriptor may contain a Constant Feature item such as Contact Count Maximum | Metadata does not add an Output/Feature transport operation. `feature_transport_supported` remains zero. |
| Raw descriptor reports | Input-only accepted-report table with exact ID and wire length | AOA has no library Output/Feature response path. The raw escape hatch validates layout/bytes but synthesizes no semantic state, neutral, transition, repeat, or timer and promises no Android semantic support. |
| Report/TLC ownership | One report cannot span more than one top-level Application Collection | **[USB HID 1.11 requirement]** §8.4. The audited Linux report object also stores one application. This does not promise Android classification. |
| Raw required-item presence | Every Collection has an associated Usage; every non-Constant Input has Usage Page, Usage, Logical Minimum, Logical Maximum, Report Size, and Report Count state | **[USB HID 1.11 requirement]** §§6.2.2.2 and 6.2.2.6. Zero-initialized parser values do not substitute for declarations. |
| Raw Local Delimiters | Sets close before the Main item; no Delimiter Usage aliases on an Application Collection or Array item | **[USB HID 1.11 requirement]** §6.2.2.8. Balanced aliases remain available for Variable controls. |
| Raw Unit metadata | A nonzero Unit requires Logical and Physical extents plus a four-bit Unit Exponent declaration | **[HUT 1.7 definition]** §3.3 and **[USB HID 1.11 requirement]** §6.2.2.7. Legacy target tolerance is not accepted as strict encoding. |
| Raw long items | Every tag-15 prefix rejected | The validated raw subset handles HID 1.11 short items only. This is a library restriction, not a claim that HID long items do not exist. |
| Raw reserved items | Main tags 13-15, Local tag 6 and tags 11-15, and short-item type 3 are rejected | HID 1.11 marks them reserved; defined Designator/String Local tags remain accepted. The named Linux target ignores reserved categories, but parser tolerance is not the strict contract. |
| Raw four-byte `UINT32_MAX` Usage Maximum | Rejected as unsupported | Conservatively prevents a state-dependent wrap in the audited target's inclusive raw expansion loop; this is an exact-revision compatibility restriction. |
| Raw computed wire length | Must fit the `uint32_t` accepted-report length field | Checked after bit-to-byte rounding; an overflow is reported instead of truncating the API comparison. |
| Dynamic object counts | No project-wide fixed maximum for contexts, devices, nodes, or specs | Allocation and host resources can still fail. "No fixed project maximum" is not "unlimited." |

## Transport limits are independent

Descriptor length, one-control-transfer report length, target EP0 storage, host backend buffers, and asynchronous transfer allocation belong to the AOA/libusb transport audit. They must not be derived from `HID_MAX_BUFFER_SIZE`, the HID four-byte field rule, or the 16-contact policy.

This document intentionally does not assign an unverified portable number to
the product- or target-specific layers. The descriptor/EP0/control-buffer and
exact-target HID-parser policies remain explicit, separately named inputs. An
error must identify the policy layer that rejected the value instead of calling
every rejection an AOA or HID limit.

The following are host-library tuning fallbacks, not portable transport maxima.
They apply only when the corresponding public field is zero and can be replaced
by an explicit nonzero value.

| Tuning field | Zero-value fallback | Scope |
|---|---:|---|
| `control_timeout_ms` | 500 ms | Synchronous control-transfer failure deadline. |
| `send_timeout_ms` | 500 ms | Asynchronous request-57 failure deadline. |
| `descriptor_fragment_bytes` | 64 bytes | Request-56 fragment payload; descriptor order/offset rules still apply. |
| `transfer_pool_slots` | 8 | Per-Device pool capacity. |
| `maximum_report_bytes` | 1024 bytes | Payload allocation and public report-length policy per pool slot. |
| `close_drain_timeout_ms` | 1000 ms | Bounded Device close/drain budget. |
| `first_report_attempts` | 20 total | One initial attempt plus at most 19 STALL-race retries. |
| `first_report_backoff_us` | 1000 microseconds | Delay between first-report retry attempts. |
| Node reservation `0/0` | no reservation | Use shared pool capacity without reserving a slot for that Node. |

Every row is **[Project policy]**. Neither the USB specifications nor libusb
defines these numbers as universal recommendations. libusb gives zero its own
unlimited/no-timeout meaning, but the public `aoahid_device_options` boundary
normalizes zero to the bounded values above before calling libusb. Timeout is a
deadline for declaring failure; successful completion returns when the backend
reports it and does not wait out the remaining budget. See
`SOURCE_CONFLICTS.md` T-23 for the primary-source distinction.

The effective fallback still must fit every explicit cross-layer policy. A
caller that selects a target EP0 or host-control-buffer policy below the
1024-byte report fallback, for example, must explicitly select a compatible
smaller `maximum_report_bytes`; validation never widens a target policy.

No fallback is provided for product geometry, Usage sets, field widths, Report
IDs, descriptor/EP0/control-buffer policies, or the five exact-target Linux HID
parser policies. A 1024-byte report allocation fallback, for example, is not a
statement that Android, AOA, USB HID, or a particular kernel accepts reports of
that size.

## Rules for changing a limit

Any change requires all of the following:

1. name the exact normative section or implementation symbol;
2. record the source URL, revision/version, and retrieval date;
3. label it as a specification requirement, exact-revision observation, Android documentation, or project policy;
4. explain whether the new value changes descriptor encoding, report serialization, transport allocation, or only validation;
5. add boundary tests for one below, at, and one above the accepted value where arithmetic permits; and
6. leave hardware status unverified until the corresponding `TARGET_MATRIX.md` evidence exists.

There is no authoritative "only ceilings" list spanning all of USB HID, Linux HID, Android input, AOA, libusb, and this library. Each layer is reviewed independently.
