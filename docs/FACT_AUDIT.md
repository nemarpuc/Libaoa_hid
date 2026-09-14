# HID Fact Audit

## Status and scope

This document records the source audit used for the HID descriptor and Input-report layer of `libaoahid`. It covers HID item encoding, Report IDs, top-level collections and reports, keyboard rollover, pointer deltas, Consumer and System controls, gamepad axes and hats, multitouch, touchpads, and pens.

The AOA transport decision is intentionally kept separate. The current runtime
is Mode A-only, while acceptance of requests 54-57 before `ACCESSORY_START`
remains target-conditional. The official AOA page, exact kernel/common
revisions, evidence labels, and conflict resolution are recorded in
`SOURCE_CONFLICTS.md` T-07. Selecting Mode A does not upgrade any descriptor or
profile row from **[Unverified on hardware]**.

No physical Android device was used for this audit. Descriptor acceptance, Linux event creation, Android classification, and application-visible events remain **unverified on hardware** unless a row in `TARGET_MATRIX.md` later records all four observations. Source inspection is not a substitute for a target test.

The audited project inputs were `AOA_HID_GUIDE.md`, `DESIGN.md`, and the
supplied implementation-prompt document preserved as `IMPLEMENTATION_PROMPTS.md`
(see `INPUT_PROVENANCE.md` for its original filename and checksum). A
statement below that corrects one of those inputs is a record of the
discrepancy, not an assertion that every other statement in the input was
audited.

## Evidence labels

Every technical conclusion in this document uses one of these labels.

| Label | Meaning |
|---|---|
| **[USB HID 1.11 requirement]** | Normative or directly defined behavior in USB Device Class Definition for Human Interface Devices 1.11. |
| **[HUT 1.7 definition]** | A Usage Page, Usage ID, Usage type, or Usage meaning in HID Usage Tables 1.7. HUT does not establish operating-system support. |
| **[Specified Linux implementation observation]** | Behavior observed in the exact named Android common Linux revision. An unqualified use in this document means commit `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`; observations explicitly naming another revision apply only to that revision. It is not a USB requirement. |
| **[Specified libusb implementation observation]** | Behavior observed in libusb v1.0.30 commit `87a55632db62c9bdc58cd31d3ccfa673f1bb017f`. It is not a timing guarantee and is not generalized to another libusb revision or operating-system backend. |
| **[Specified Windows implementation observation]** | Windows-header integration behavior established by the exact named libusb public header and an official Microsoft Learn page. It is a build-time platform observation, not a USB, AOA, HID, runtime, or latency requirement. |
| **[Android platform documentation]** | Behavior stated by an official Android input document. It is not a USB HID requirement. |
| **[Guide policy]** | A deliberate library rule selected for safety, determinism, or the intended Android profile. It is not attributed to USB HID or to every Linux target. |
| **[Project policy]** | A current API or implementation choice made by this repository. A numeric policy is not promoted to a USB, AOA, libusb, Linux, or Android requirement. |
| **[Unverified on hardware]** | No physical target evidence has been recorded. |

## Primary-source register

All sources in this table were retrieved on **2026-08-27**.

| Source | Revision or version | Sections or symbols read | Official URL | Retrieved |
|---|---|---|---|---|
| USB Device Class Definition for Human Interface Devices | Version 1.11, 2001-05-27 | §§5.3, 5.6, 5.8, 5.10, 6.2.2.2, 6.2.2.4-6.2.2.9, 8.2, 8.3, 8.4; Appendices B.1, C, D, and E.6 | [USB HID 1.11 PDF](https://www.usb.org/sites/default/files/hid1_11.pdf) | 2026-08-27 |
| HID Usage Tables | Version 1.7, 2026-01-26 | §3.1; §3.3; §3.4 and Tables 3.2-3.3; §3.4.4; Generic Desktop Table 4.1 and §4.5; Telephony table and §§14.2-14.3; Consumer table and §§15.7, 15.9, 15.16; Camera Table 35.1 and §35.1; §§5, 9, 10, 12, and 16 | [HID Usage Tables 1.7 PDF](https://www.usb.org/sites/default/files/hut1_7.pdf) | 2026-08-27 |
| USB 2.0 Specification | Revision 2.0 | §§9.2.6.1 and 9.2.6.4, device request-processing timing | [official USB-IF specification page](https://www.usb.org/document-library/usb-20-specification) | 2026-08-27 |
| USB 3.2 Specification | Revision 1.1, June 2022 | §§9.2.6.1 and 9.2.6.4, device request-processing timing | [official USB-IF specification page](https://www.usb.org/document-library/usb-32-revision-11-june-2022) | 2026-08-27 |
| libusb synchronous I/O | libusb 1.0.30, generated 2026-05-18 | `libusb_control_transfer`, timeout units and zero semantics | [official API](https://libusb.sourceforge.io/api-1.0/group__libusb__syncio.html) | 2026-08-27 |
| libusb transfer structure | libusb 1.0.30, generated 2026-05-18 | `libusb_transfer::timeout`, timeout units and zero semantics | [official API](https://libusb.sourceforge.io/api-1.0/structlibusb__transfer.html) | 2026-08-27 |
| libusb asynchronous core and Linux/Windows backends | v1.0.30 commit `87a55632db62c9bdc58cd31d3ccfa673f1bb017f` | `libusb_alloc_transfer`, `libusb_submit_transfer`, `usbi_signal_transfer_completion`, `handle_event_trigger`; Linux `op_init`, `submit_control_transfer`, `handle_control_completion`, `reap_for_handle`, `op_handle_events`, and udev/netlink monitor startup; Windows `windows_init`, `windows_iocp_thread`, `windows_open`, `windows_submit_transfer`, `winusbx_submit_control_transfer`, `windows_handle_transfer_completion`, and `windows_transfer_priv` | [immutable `io.c`](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/io.c), [Linux usbfs](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/linux_usbfs.c), [Linux monitor selection](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/linux_usbfs.h), [udev monitor](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/linux_udev.c), [netlink monitor](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/linux_netlink.c), [Windows common](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/windows_common.c), [Windows private types](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/windows_common.h), [WinUSB-like backend](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/windows_winusb.c) | 2026-08-27 |
| libusb public header | v1.0.30 commit `87a55632db62c9bdc58cd31d3ccfa673f1bb017f` | `_MSC_VER` preamble; `_WIN32`/`__CYGWIN__` block that includes `<windows.h>`; `LIBUSB_API_VERSION` (`0x0100010C`) | [immutable `libusb.h`](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/libusb.h) | 2026-08-27 |
| Microsoft Windows-header integration guidance | Microsoft Learn, page last updated 2021-03-22 | `XMMax` template, Remarks: `std::max` conflict and defining `NOMINMAX` before Windows headers | [XMMax template](https://learn.microsoft.com/en-us/windows/win32/dxmath/xmmax-template) | 2026-08-27 |
| Linux common source tree | Commit `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, 2020-09-01 | Exact audited tree | [commit](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/) | 2026-08-27 |
| Linux HID core | Same commit | `fetch_item`, `hid_open_report`, `hid_register_report`, `hid_register_field`, `hid_add_usage`, `hid_add_field`, `open_collection`, `close_collection`, `item_udata`, `item_sdata`, `hid_parser_main`, `hid_parser_global`, `hid_parser_local`, `hid_input_field`, `hid_field_extract` | [`drivers/hid/hid-core.c`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-core.c) | 2026-08-27 |
| Linux generic HID input mapping | Same commit | `hidinput_calc_abs_res`, `hidinput_configure_usage`, `hidinput_hid_event`, `hid_hat_to_axis`, `hidinput_setup_battery`, `hidinput_scale_battery_capacity`, `hidinput_update_battery` | [`drivers/hid/hid-input.c`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-input.c) | 2026-08-27 |
| Linux HID multitouch | Same commit | `mt_classes`, `mt_input_mapping`, `mt_touch_input_mapping`, `mt_touch_event`, `mt_process_slot`, `mt_process_mt_event`, `mt_sync_frame`, `mt_touch_report`, `mt_post_parse_default_settings`, `mt_touch_input_configured`, `mt_probe`, and the generic entries in `mt_devices` | [`drivers/hid/hid-multitouch.c`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-multitouch.c) | 2026-08-27 |
| Linux HID definitions | Same commit | `HID_MAX_USAGES`, `HID_MAX_FIELDS`, `HID_MAX_IDS`, `HID_MAX_BUFFER_SIZE`, `HID_GLOBAL_STACK_SIZE` | [`include/linux/hid.h`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/include/linux/hid.h) | 2026-08-27 |
| Android 17 ACK HID input/core | `android17-6.18` commit `f67745b7d96806e622db56f4be97af16d6e99850` | `hid_hat_to_axis`, `hidinput_usages_priorities`, `hidinput_configure_usage`, `hidinput_hid_event`, `hid_process_report`, `__hid_insert_field_entry`, `hid_report_process_ordering` | [`drivers/hid/hid-input.c`](https://android.googlesource.com/kernel/common/+/f67745b7d96806e622db56f4be97af16d6e99850/drivers/hid/hid-input.c), [`drivers/hid/hid-core.c`](https://android.googlesource.com/kernel/common/+/f67745b7d96806e622db56f4be97af16d6e99850/drivers/hid/hid-core.c) | 2026-08-27 |
| Android 17 Compatibility Definition | Android 17 | §7.2.6.1, Game Controller Support: Game Pad Application Collection, Button Page A/B/X/Y mappings, and Hat Switch metadata/direction text | [Android 17 CDD](https://source.android.com/docs/compatibility/17/android-17-cdd) | 2026-08-27 |
| Android game-controller input documentation | Page updated 2026-02-26 | Controller buttons, axes, and motion ranges | [Controller input](https://developer.android.com/games/sdk/game-controller/controller-input) | 2026-08-27 |
| Android touch-device documentation | Page updated 2026-07-13 | "Touch device driver requirements" and "Hovering versus touching tools" | [Touch devices](https://source.android.com/docs/core/interaction/input/touch-devices) | 2026-08-27 |

## Audit findings and required resolutions

### A-01 - Placement of the first Report ID

**Input discrepancy:** `AOA_HID_GUIDE.md` §§25 and 36 and `DESIGN.md` §6.3 say that the first Report ID cannot follow "a Main item." A `Collection` item is itself a Main item, so that wording rejects the normal sequence `Collection (Application)`, `Report ID`, `Input`.

**[USB HID 1.11 requirement]** Section 6.2.2.7 requires the first Report ID to precede the first **Input, Output, or Feature** item whose data belongs to an identified report. It does not require the Report ID to precede `Collection`.

**Resolution:** A parser or validator tracks data Main items (`Input`, `Output`, and `Feature`), not every Main item. `Collection (Application) -> Report ID -> Input` is accepted. If any Report ID is used, every data report carries an ID prefix; an ID-less data report is not mixed into the same descriptor.

### A-02 - Report ID zero and an internal HID 1.11 conflict

**[USB HID 1.11 requirement]** Section 6.2.2.7 reserves Report ID zero and says it should not be used.

**Conflicting source text:** HID 1.11 §8.5 contains an illustrative example with `Report ID (00)`. This conflicts with §6.2.2.7.

**[Specified Linux implementation observation]** At the audited commit, `hid_parser_global` rejects Report ID zero.

**Resolution:** `libaoahid` rejects an explicitly declared ID of zero. The normative Global-item definition and the specified target parser take precedence over the contradictory illustrative example. Absence of a Report ID item remains valid and does not add a zero byte to an Input report.

### A-03 - Report identity is not an ID alone

**[USB HID 1.11 requirement]** Input, Output, and Feature are distinct report types. A Report ID selects a report within a type.

**[Specified Linux implementation observation]** `hid_register_report` selects `device->report_enum + type` and then indexes `report_id_hash[id]`. The implementation identity is therefore `(report type, Report ID)`.

**Resolution:** General descriptor validation keys reports by `(type, id)`. The current AOA transport surface is Input-only, so an Input serializer may use an Input-specific layout and must not imply that an Output or Feature layout with the same numeric ID is the same report.

### A-03a - Top-level collection ownership of a report

**[USB HID 1.11 requirement]** Sections 6.2.2.5 and 6.2.2.6 define Collection and End Collection Main items. Section 8.4 requires every top-level collection to be an Application Collection and says that reports may not span more than one top-level collection. **[HUT 1.7 definition]** §3.4.3.2 describes an Application Collection as a device or functional subset; a numeric Usage on that collection does not by itself promise a particular Android mapping.

**[Specified Linux implementation observation]** `hid_add_field` obtains the current Application Collection and passes it to `hid_register_report`. `hid_register_report` returns the existing report object for the same `(type, id)`, whose single `application` member cannot represent two top-level Application Collections.

**Resolution:** Each generated logical HID uses one Application Collection. The raw validator permits multiple Application Collections only when an Input report does not cross between them; one `(Input, Report ID)` belongs to one top-level collection. This enforces **[USB HID 1.11 requirement]** §8.4. It does not claim that HUT guarantees Android classification from the top-level Usage alone.

### A-03b - Required descriptor items must be present, not defaulted

**Input omission:** `DESIGN.md` §6.3 lists many raw-descriptor validation
checks but does not say that the validator proves the presence of every item
needed to describe control data. An intermediate implementation consequently
treated absent Logical extents as the zero-initialized pair `(0, 0)` and could
reuse the Collection Usage after local state had already been cleared.

**[USB HID 1.11 requirement]** Section 6.2.2.2 lists Input (or Output or
Feature), Usage, Usage Page, Logical Minimum, Logical Maximum, Report Size, and
Report Count as required to describe a control's data. Section 6.2.2.6 also
requires a Usage to be associated with every Collection. Local items reset
after every Main item, so a Collection's Usage is not the later Input field's
Usage.

**Resolution:** the raw validator tracks declaration presence separately from
numeric values. Each non-Constant Input must have current Usage Page state, an
Input-local Usage, and explicitly declared Logical Minimum and Maximum; every
Collection must consume an associated local Usage. Constant padding remains
permitted without a control Usage, matching the specification's own descriptor
examples. **[USB HID 1.11 requirement]** Retrieved 2026-08-27.

### A-03c - Delimiter aliases have two explicit exclusions

**Input omission:** neither supplied design document calls out the restrictions
on HID Local Delimiter sets, and an intermediate raw validator mirrored the
audited Linux parser's branch accounting without enforcing the USB descriptor
rule itself.

**[USB HID 1.11 requirement]** Section 6.2.2.8 defines a Delimiter as the open
or close of a set of alternative Usages. The alternatives are bracketed by
Delimiter items, and Delimiters cannot define Usages for Application
Collections or Array items.

**[Specified Linux implementation observation]** At common-kernel commit
`35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, `hid_parser_local` rejects nested
and unmatched closing delimiters but `hid_parser_main` clears the Local state
after a Main item. That implementation behavior does not replace the HID
descriptor requirement.

**Resolution:** the raw validator requires a delimiter set to close before its
Main item and rejects any delimited Usage attached to an Application Collection
or non-Constant Array Input. Valid Variable-field aliases remain accepted, and
only the first alternative branch contributes to the named target's capacity
requirement. Retrieved 2026-08-27.

### A-03d - Generated Collection balance is checked in both passes

**Input requirement not met by the intermediate implementation:** `DESIGN.md`
§6.3 requires Collection nesting validation, but the initial generated-profile
builder only relied on each profile emitter to call the correct number of
`End Collection` items.

**[USB HID 1.11 requirement]** Sections 6.2.2.4 and 6.2.2.6 define Collection
and End Collection as paired Main items; §8.4 also requires each top-level
collection to be an Application Collection.

**Resolution:** the generated builder now tracks nesting, rejects an unmatched
End Collection, and marks an unfinished open Collection as failure. Both the
exact-size counting pass and byte-emission pass call this final check, so a
future profile edit cannot silently ship an unbalanced generated descriptor.
Retrieved 2026-08-27.

### A-03e - The complete Global tag set is explicit

**[USB HID 1.11 requirement]** Section 6.2.2.7 defines Global tags 0 through
11, including Physical Minimum, Physical Maximum, Unit Exponent, and Unit, and
reserves tags 12 through 15. **[Specified Linux implementation observation]**
the audited `hid_parser_global` accepts the twelve defined tags and rejects its
default case.

**Resolution:** the raw validator accepts every defined Global tag and rejects
Global tags 12 through 15. It separately rejects HID-reserved Main tags 13-15,
reserved Local tag 6 and tags 11-15, and reserved short-item type 3; defined
Designator and String Local tags remain accepted. The selected Linux
revision ignores those latter categories, but that parser tolerance is not
promoted into the strict raw contract. Retrieved 2026-08-27.

### A-03f - Four parser quantities remain caller-selected target policies

**[Specified Linux implementation observation]** in the exact common-kernel
revision, `HID_GLOBAL_STACK_SIZE` is 4; Report Size declarations above 256 are
rejected immediately; `hid_add_field` rejects accumulated report data above
65,528 bits; and Local Usage storage is updated and bounded whenever a Usage is
parsed, even if no later Main item consumes it. These values and timing rules
are not USB HID or universal Linux requirements.

**Resolution:** generated and raw Specs record maximum Global Push depth,
Report Size declaration, accumulated report data bits per report identity, and
Local Usage demand at every state change. Device options provide four
additional separately named target policies alongside the existing
per-report-field policy, all checked before AOA request 54. Values 4, 256,
65,528, and 12,288 are examples only for commit
`35556bed836f8dc07ac55f69c8d17dce3e7f0e25`. Retrieved 2026-08-27.

### A-03g - Exact parser edge cases are rejected before registration

**[Specified Linux implementation observation]** `fetch_item` treats any
tag-15 prefix as long and `hid_open_report` rejects long format;
`hid_open_report` also rejects a Local delimiter left open at EOF. In
`hid_parser_local`, a four-byte Usage Maximum whose raw item value is
`UINT32_MAX` can reach an inclusive loop whose unsigned iterator wraps. Whether
it reaches that bound depends on the preceding Local Usage count and the
parser's capacity-truncation branch; wrapping is not inevitable for every
descriptor whose completed Usage is `0xffffffff`.

**Resolution:** the short-item-only raw subset rejects all tag-15 prefixes,
includes delimiter depth in final balance, and conservatively returns
`AOAHID_ERR_UNSUPPORTED` whenever the raw four-byte Usage Maximum item equals
`UINT32_MAX`. The last rule deliberately rejects more than the exact target is
proved to reject; it is **[Guide policy]**, not a HUT definition or universal
Linux behavior. Retrieved 2026-08-27.

### A-03h - Raw logical signedness differs from the generated C option type

**[USB HID 1.11 requirement]** Section 6.2.2.7 selects Maximum signedness from
the Minimum. **[Specified Linux implementation observation]**
`hid_parser_global` preserves an unsigned Maximum bit pattern in `s32` storage,
and `hid_add_field` performs its signed-or-unsigned order check before ignoring
padding with no Usage.

**Resolution:** raw parsing preserves the 32-bit pattern: Minimum 0 plus
four-byte Maximum `0xffffffff` is unsigned 4,294,967,295. Range order applies
to Constant as well as data Inputs. Generated public extrema remain signed
`int32_t` caller values and are not silently reinterpreted. Retrieved
2026-08-27.

### A-03i - Unit metadata is mandatory in strict raw descriptors

**[HUT 1.7 definition]** Section 3.3 requires Logical Minimum, Logical
Maximum, Physical Minimum, Physical Maximum, and Unit Exponent whenever a
non-None Unit qualifies a data Main item. **[USB HID 1.11 requirement]**
Section 6.2.2.7 makes those declarations Global state and defines Unit Exponent
as a signed four-bit code. **[Specified Linux implementation observation]**
the audited parser neither checks presence nor rejects legacy full-byte
sign-extension.

**Resolution:** strict raw validation tracks declaration presence through
Push/Pop and enforces it for every Input, including Constant padding. Unit zero
removes the requirement. Exponent codes must have numeric value 0 through 15;
target-tolerated `0xfc` is rejected. Retrieved 2026-08-27.

### A-03j - Usage ranges are interpreted by item size before target page concatenation

**[USB HID 1.11 requirement]** Section 6.2.2.8 says Local items are unsigned,
defines one- and two-byte Usage, Usage Minimum, and Usage Maximum items as IDs
on the current Usage Page, and defines four-byte forms as extended Usages whose
upper 16 bits are the page. An extended Usage Minimum requires an extended
Usage Maximum. HUT 1.7 §3.1 repeats the size-dependent interpretation for all
three Local items. Neither specification text defines a default Usage Minimum
or forbids a Usage Maximum without a preceding Minimum.

**[Specified Linux implementation observation]** At commit
`35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, `hid_parser_local` stores the raw
`item_udata()` Minimum, calculates and iterates the raw Minimum-to-Maximum
interval, and passes the **Maximum item's size** to `hid_add_usage`.
`complete_usage` appends the current page only when that size is one or two
bytes. Thus current page `0xffff` with short Minimum `0xfffe` and short Maximum
`0xffff` has two target entries; it does not iterate a completed
`0xfffffffe..0xffffffff` range. Conversely, short Minimum `1` followed by
extended Maximum `0x00010002` has an untruncated raw-domain demand of 65,538
entries even though the HID items denote page-1 endpoints. The selected parser
then clips that expansion to its finite `HID_MAX_USAGES` table and ignores the
excess rather than representing the complete interval. Because zero-initialized
Local state is cleared after each Main item, a standalone short Maximum `3` is
an exact-target `0..3` range; that behavior is not promoted to a HID default.

**Resolution:** strict raw validation rejects an extended Minimum followed by
a short Maximum as malformed under HID 1.11. It records the selected target's
raw expansion demand without a count-sized validation loop: the two examples
above record 2 and 65,538 respectively. The caller-selected
`linux_hid_usages_policy` then rejects a target-incompatible demand before AOA
request 54. These counts and the standalone-Maximum behavior are
exact-revision observations, not HUT limits. Retrieved 2026-08-27.

### A-04 - Short-item size and signedness

**Input ambiguity:** `DESIGN.md` §6.1 and Phase 2 of the prompt describe item size in terms of the field's declared signedness. That is correct for report-value serialization but incomplete for Global-item encoding: a Minimum item is parsed as signed even when the resulting report field is unsigned.

**[USB HID 1.11 requirement]** Sections 5.8, 6.2.2.2, and 6.2.2.7 define short-item payload sizes of 0, 1, 2, or 4 bytes. Logical Minimum and Physical Minimum are signed values. Logical Maximum is interpreted as signed when Logical Minimum is negative and as unsigned otherwise; Physical Maximum follows Physical Minimum in the same way.

**[Specified Linux implementation observation]** `hid_parser_global` reads each minimum with `item_sdata`. It chooses `item_sdata` or `item_udata` for the corresponding maximum based on the stored minimum.

**Resolution:** A positive minimum must still use a signed representation that preserves the value. For example, Logical Minimum 128 cannot be emitted as one byte `0x80`, because the parser reads it as -128; it needs the two-byte signed form. With Logical Minimum 0, Logical Maximum 65535 is correctly encoded as the two-byte item `26 FF FF`. Raw signed decoding performs explicit two's-complement arithmetic in a wider signed type rather than relying on implementation-defined unsigned-to-signed narrowing.

### A-05 - Unit Exponent encoding

**[USB HID 1.11 requirement]** Section 6.2.2.7 defines Unit Exponent as a signed four-bit value. The representable range is -8 through 7; exponent -4 is encoded in the low nibble as `0x0c`.

**[Specified Linux implementation observation]** `hid_parser_global` accepts both the specification form and a legacy full-byte sign-extended form such as `0xfc`.

**Resolution:** Generated descriptors use the specification nibble form and reject values outside -8 through 7. Linux compatibility acceptance is not used as the encoder contract.

### A-05a - Physical metadata is per field in the public contract

**[USB HID 1.11 requirement]** Section 6.2.2.7 defines Physical Minimum,
Physical Maximum, Unit Exponent, and Unit as Global items. Their values apply
to subsequent Main items. Physical extents `(0, 0)` restore the default
interpretation; Unit Exponent is a signed four-bit value from -8 through 7.

**[HUT 1.7 definition]** Section 3.3 requires Logical Minimum, Logical
Maximum, Physical Minimum, Physical Maximum, and Unit Exponent whenever a Unit
is declared for a Main item.

**[Specified Linux implementation observation]** At common-kernel commit
`35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, `hidinput_calc_abs_res` in
[`drivers/hid/hid-input.c`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-input.c)
uses logical and physical extents, Unit, and Unit Exponent. It returns zero for
unrecognized axis/unit combinations. This is an observation of that revision,
not a USB or Android requirement.

**Resolution:** An `aoahid_integer_field` may carry one explicit physical
declaration. Disabled metadata must be entirely zero. Enabled metadata requires
ordered physical extents, a nonzero Unit, and exponent -8 through 7. The
descriptor emits all four related Global items before the field Main item and
resets all four to zero immediately afterward. The reset prevents Global state
from leaking into a later field. Retrieved 2026-08-27.

### A-06 - A field may not span more than four bytes

**Input ambiguity:** "Report fields >4 bytes" has been read as a limit on the whole report in prior project text.

**[USB HID 1.11 requirement]** Section 8.4 limits each individual field to a span of at most four bytes and requires a 32-bit field to begin on a byte boundary. A report can be longer than four bytes, and `Report Count` creates repeated fields.

For a field of width `w` at report bit offset `o`, the number of wire bytes touched is:

```text
ceil(((o mod 8) + w) / 8)
```

The value must not exceed four. A 32-bit field additionally requires `o mod 8 == 0`.

**[Specified Linux implementation observation]** The audited `hid_parser_global` accepts Report Size values through 256, while `hid_field_extract` warns and truncates extraction widths greater than 32. Parser acceptance is not evidence of HID 1.11 conformance.

**Resolution:** The strict descriptor validator enforces HID §8.4 even where the audited Linux parser accepts a wider Report Size. The structural pass applies the same span check to generated Input and generated declarative Feature fields; Constant Feature metadata is not exempt from the HID report rule.

### A-07 - Keyboard is strictly a full-NKRO Variable bitmap

**[HUT 1.7 definition]** Keyboard/Keypad Usage `0x01` is `ErrorRollOver`, a value HUT §10 defines only for the Input (Array, Absolute) form USB HID 1.11 Appendix C describes for USB boot-compatible keyboards. HUT 1.7 §3.4.2.1 separately permits any Selector set, keyboards included, to be represented as `Array[1]`, `Array[n]`, or one Variable bit per selection. Modifier Usages `0xe0` through `0xe7` are always independent Variable bits regardless of the nonmodifier form.

**Resolution:** This library implements only the Variable-bitmap form: every declared nonmodifier Usage from `usage_minimum` through `usage_maximum` gets its own one-bit Variable field, so every simultaneously pressed key already fits in the same report with no shared slot to overflow. There is no Array Report Count, no key-selector packing, and no `ErrorRollOver` value to emit or recover from — the profile is full N-Key Rollover (NKRO) by construction, not by an overflow-avoidance policy layered on an Array. Because the descriptor never claims the USB HID 1.11 Appendix C boot-keyboard Array form, there is no conflict between that Appendix and HUT §3.4.2.1 to resolve here.

**[Specified Linux implementation observation]** and Android's input stack treat a one-bit Variable Keyboard/Keypad-page field the same way they treat any other Variable key field: each bit's `hidinput_configure_usage` mapping becomes one ordinary `EV_KEY` input event on press and release. No Array-specific discard or reassembly logic (such as the `hid_input_field` behavior that discards an Array report carrying `ErrorRollOver`) is reachable, because this profile never emits an Array Main item. Target behavior for a given Usage range remains **[Unverified on hardware]**.

### A-08 - Relative mouse motion

**[USB HID 1.11 requirement]** A bidirectional relative X or Y field needs a signed logical range containing both negative and positive values and the Relative Main-item flag. HID does not define a queueing or delta-fragmentation algorithm for an application library.

**Resolution:** Splitting an accumulated delta into in-range report fragments is **[Guide policy]**. Each fragment is a complete Input report and carries the current button state. A key, button release, or touch-lifecycle report is never discarded as a stale relative-motion optimization.

### A-09 - A sparse mixed-type allow-list is not one aligned Array

**Implementation discrepancy found during this audit:** the first generated Consumer/System implementation declared one Array with Usage Minimum `0`, Usage Maximum equal to the numerically greatest allow-listed Usage, and a wire value equal to the Usage ID. A sparse list such as `{0x00cd, 0x00e9}` therefore declared every intervening Usage even though the state API rejected those values. It also applied one Array interpretation to controls with different HUT types.

**[USB HID 1.11 requirement]** Section 6.2.2.5 defines an Array value as a selector into its declared Usage set; a wire value is not universally a Usage ID. **[Specified Linux implementation observation]** `hid_input_field` indexes `field->usage[value - logical_minimum]` for Array data.

**[HUT 1.7 definition]** Section 3.4.2.1 permits a Selector set to use `Array[1]`, `Array[n]`, or one Variable bit per selection. It does not turn OOC, MC, OSC, RTC, LC, or DV Usages into Selectors.

**Implemented resolution:** every allow-listed Usage is now emitted as its own one-bit Variable field. The field's Usage is the exact sparse Usage ID, and its `instance` is the allow-list index used by the serializer. No interval between sparse IDs is declared. Duplicate IDs are rejected. The state surface intentionally permits one active control at a time; release clears every field. This descriptor rule is **[Guide policy]**, while the Selector bitmap form is **[HUT 1.7 definition]**.

### A-10 - Usage type determines the press/release field semantics

**[HUT 1.7 definition]** Table 3.2 and §§3.4.1.2-3.4.1.5 distinguish these one-bit encodings:

| Public semantic | Input Main-item form | HUT operation |
|---|---|---|
| `AOAHID_USAGE_ON_OFF_TOGGLE` | Variable, Relative, Preferred; logical `0..1` | `0 -> 1` toggles; `1 -> 0` makes no change |
| `AOAHID_USAGE_ON_OFF_MAINTAINED` | Variable, Absolute, No Preferred; logical `0..1` | `1` asserts On and `0` asserts Off |
| `AOAHID_USAGE_MOMENTARY` | Variable, Absolute, Preferred; logical `0..1` | `1` asserts and `0` deasserts |
| `AOAHID_USAGE_ONE_SHOT` | Variable, Relative, Preferred; logical `0..1` | `0 -> 1` triggers once; `1 -> 0` is required to re-arm |
| `AOAHID_USAGE_RETRIGGER` | Variable, Absolute, Preferred; logical `0..1` | repeats while `1` remains asserted |
| `AOAHID_USAGE_SELECTOR_BITMAP` | one-bit Variable Selector form from §3.4.2.1 | the bit selects that exact Usage |

HID 1.11 §6.2.2.4 supplies the Input flag bit definitions, including Relative and No Preferred. **[Guide policy]** The public options require one explicit `usage_semantics[i]` for every `allowed_usages[i]`; the factory no longer infers one tap model from a Usage Page.

**[HUT 1.7 definition]** The audited numeric examples are:

| Page and Usage | ID | Default HUT type | Official table/section |
|---|---:|---|---|
| Consumer Play | `0x0c/0x00b0` | OOC | Consumer table, §15.7 |
| Consumer Play/Pause | `0x0c/0x00cd` | OSC | Consumer table, §15.7 |
| Consumer Volume | `0x0c/0x00e0` | LC | Consumer table, §15.9 |
| Consumer Mute | `0x0c/0x00e2` | OOC | Consumer table, §15.9 |
| Consumer Volume Increment | `0x0c/0x00e9` | RTC | Consumer table, §15.9 |
| Consumer AC New | `0x0c/0x0201` | Sel | Consumer table, §15.16 |
| Consumer AC Pan | `0x0c/0x0238` | LC | Consumer table, §15.16 |
| System Power Down | `0x01/0x81` | OSC | Generic Desktop Table 4.1, §4.5 |
| System Menu Right | `0x01/0x8a` | RTC | Generic Desktop Table 4.1, §4.5 |
| System Function Shift | `0x01/0x97` | MC | Generic Desktop Table 4.1, §4.5 |
| System Function Shift Lock | `0x01/0x98` | OOC | Generic Desktop Table 4.1, §4.5 |
| Telephony Hook Switch | `0x0b/0x20` | OOC; §14.3 specifies a maintained bit | Telephony table, §14.3 |
| Telephony Flash | `0x0b/0x21` | MC | Telephony table, §14.3 |
| Telephony Redial | `0x0b/0x24` | OSC | Telephony table, §14.3 |
| Telephony Phone Key 0 | `0x0b/0xb0` | Sel | Telephony table, §14.2 |
| Camera Auto-focus / Shutter | `0x90/0x20`, `0x90/0x21` | OSC | Table 35.1, §35.1 |

**[HUT 1.7 definition]** Section 3.4.4 says Usage Types are recommended guidance and that the actual Main-item flags and ranges determine the control interpretation. The generic factories therefore record the caller's exact intended encoding rather than claiming that a hard-coded table covers every allowed alternate type. The camera factory is intentionally narrower: it accepts only the two Table 35.1 IDs and OSC semantics.

**[Guide policy]** LC, two-direction OOC, DV, and NAry are named explicitly in the enum but return `AOAHID_ERR_UNSUPPORTED`; a press/release API has no numeric value, On-versus-Off direction, or nested collection state with which to represent them. This specifically prevents Consumer Volume and AC Pan from masquerading as taps. Expected Linux event type/code strings remain mandatory target evidence, not HUT semantics. Android delivery or system interception remains **[Unverified on hardware]**.

**[HUT 1.7 definition]** Camera Control Table 35.1 contains Undefined,
reserved ranges, and only the two OSC controls; it contains no Camera-page
Application Collection Usage. HUT therefore does not establish a unique
Camera TLC for these fields. The camera-key factory's Consumer Control
Application Collection is **[Guide policy]**, not an Android or HUT
classification guarantee.

### A-11 - Hat neutral encoding depends on the representable domain

**Input discrepancy:** The prompt says that the hat Null value is computed from the declared logical range. The range alone is insufficient.

**[HUT 1.7 definition]** Generic Desktop `Hat switch (0x39)` has a Null state.

**[USB HID 1.11 requirement]** Section 6.2.2.5 defines Null State as an encoded value outside the Logical Minimum through Logical Maximum range and requires the Null State Main-item flag.

**Resolution:** Hat construction needs Logical Minimum, Logical Maximum, bit width, and signedness. Select a representable value outside the range. For an unsigned four-bit range 0 through 7, `8` is a valid Null value. A four-bit range covering all encodings, whether unsigned 0 through 15 or signed -8 through 7, has no Null encoding and is rejected.

**[Specified Linux implementation observation]** `hidinput_hid_event` computes a direction with `(value - min) * 8 / (max - min + 1) + 1`; a result outside 0 through 8 maps to neutral `(0, 0)`. Hat processing occurs before the generic Null-state discard path. The descriptor still carries the Null flag for specification portability.

### A-11a - The Android 17 portable-candidate gamepad form is narrower than a generic HUT gamepad

**[Android platform documentation]** Android 17 CDD §7.2.6.1 names Generic
Desktop / Game Pad as the Application Collection, assigns Button Page Usage IDs
`1`, `2`, `4`, and `5` to A, B, X, and Y, and requires the canonical Hat Switch
metadata: Logical `0..7`, Physical `0..315`, Unit Degrees, and Report Size four.
HUT's Gamepad Usage alone does not establish this Android contract.

**Resolution:** a generated manifest is a portable candidate only for the Game
Pad Application Collection with the canonical Hat and a contiguous Button Page
range beginning at `1` whose count is at least `5`; that range necessarily
contains the CDD's A/B/X/Y Usage IDs. Contiguity is a **[Guide policy]** imposed
by this generated descriptor form, not CDD wording. Raw D-pad, no-D-pad,
Joystick, and other button ranges remain conditional. Every form remains
**[Unverified on hardware]**.

**Source conflict:** the same CDD subsection says Hat values increase clockwise
from Up, but its prose for value `1` says Up and Left. At Android 17 ACK commit
`f67745b7d96806e622db56f4be97af16d6e99850`, `hid_hat_to_axis` maps direction
index `2`-the result for a Logical `0..7` wire value of `1`-to `(1,-1)`, or
Up-right. **[Specified Linux implementation observation]**

**Resolution:** the canonical generated mapping uses value `1` for Up-right,
following the CDD's clockwise rule and the pinned ACK implementation; the
contradictory Up-left example is not copied. This is **[Guide policy]**, not a
universal HUT direction-number rule, and is separately recorded in
`SOURCE_CONFLICTS.md` A-05.

### A-12 - Gamepad neutral values are semantic inputs

**Input discrepancy:** `DESIGN.md` §8.6 says every stick closes at the midpoint of its declared range. No audited USB or HUT text establishes an arbitrary raw-range midpoint as the semantic center.

**[HUT 1.7 definition]** Simulation `Throttle (0x00bb)`, `Accelerator (0x00c4)`, and `Brake (0x00c5)` are zero-to-maximum controls. Generic Desktop `Z (0x32)` and `Rz (0x35)` do not become trigger controls merely from their names or ranges.

**Resolution:** Every gamepad axis carries an explicit neutral value. A signed stick profile can contractually use zero; Simulation trigger-like axes can use their declared minimum. The library does not infer stick center or trigger semantics from arithmetic alone.

### A-13 - Contact Count zero is valid in a continuation packet

**Input discrepancy:** `DESIGN.md` §8.2 and Phase 4 of the prompt say Contact Count is never zero. That rule conflicts with the requested multi-packet behavior.

**[HUT 1.7 definition]** Digitizers `Contact Count (0x54)` reports the number of contacts currently detected and reported. HUT does not prohibit zero.

**[Specified Linux implementation observation]** `mt_touch_report` explicitly documents that subsequent packets in a multi-packet frame carry zero Contact Count. A nonzero value starts a frame by setting the expected total.

**Resolution:** In multi-packet mode, the first packet carries the frame's total record count and continuation packets carry zero. In the fixed single-packet profile, no isolated empty frame is sent after the final explicit Up record. This latter behavior is **[Guide policy]**, not a universal ban on Contact Count zero.

### A-14 - One-contact multitouch has a target-specific count nuance

**[Specified Linux implementation observation]** In the audited generic `HID_ANY_ID` vendor/product path, `mt_probe` sets `serial_maybe`; `mt_input_configured` then calls `mt_post_parse_default_settings`. That function removes `MT_QUIRK_CONTACT_CNT_ACCURATE` when the multitouch Usage list is singular. The default class otherwise includes `MT_QUIRK_ALWAYS_VALID | MT_QUIRK_CONTACT_CNT_ACCURATE` and does not include `MT_QUIRK_NOT_SEEN_MEANS_UP`.

**Resolution:** The "reuse previous expected count" description does not apply unchanged to the singular one-contact path. It also does not make an empty report a portable lift. The one-contact MT profile uses the same Contact ID and sends one explicit report with Tip Switch clear before removing the contact.

### A-14a - Touchpad buttons and an idle zero-count report

**Input ambiguity:** `DESIGN.md` §8.2 says that buttons which do not belong to the pointer collection go in a separate node. It does not say that every physical touchpad button is forbidden in a Touch Pad Application Collection. The generated touchpad descriptor places its Button Page fields outside the Finger collections and inside the Touch Pad Application Collection.

**[Specified Linux implementation observation]** In the audited generic multitouch path, `mt_touch_input_mapping` counts Button Page fields and maps them to `BTN_MOUSE` codes. `mt_touch_input_configured` marks a pointer device with one such button as a buttonpad. For an idle default-class frame, `num_received` and `num_expected` are both zero. A Contact Count of zero leaves `num_expected` unchanged; `mt_process_slot` skips the zero-filled contact records under `MT_QUIRK_CONTACT_CNT_ACCURATE`; `mt_process_mt_event` emits the mapped button value; and the final `num_received >= num_expected` check calls `mt_sync_frame`, which calls `input_sync`. This establishes the button press/release processing path in commit `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`; it is not a USB HID rule or an Android compatibility guarantee.

**[Specified Linux implementation observation]** The same source has separate logic for a Win8 PTP first packet whose Contact Count is zero and whose only change is a button event. That path uses a Scan Time change to distinguish the report from a continuation packet. The generated conditional Android touchpad profile does not depend on Win8 grouping or on a Feature response.

**Resolution:** The conditional touchpad factory may include caller-selected Button Page fields. A button-only press or release when no contact frame is in progress carries Contact Count zero. Button mutation is rejected while a multi-packet frame is between packets, so that such a report cannot be inserted where zero means continuation. Closing a previously reported pressed button sends the same zero-count button-release form after any current frame has completed. Touchscreen buttons remain rejected by the generated portable-candidate factory. Android classification, click handling, and application visibility remain **[Unverified on hardware]**.

### A-14b - Azimuth full-turn endpoint differs from its sample domain

**[HUT 1.7 definition]** The Digitizers Usage table assigns Azimuth Usage
`0x3f`. Section 16.3.3 defines it as counter-clockwise rotation around the Z
axis through a full circular range. HUT does not state the exact Linux event
conversion.

**[Specified Linux implementation observation]** At common-kernel commit
`35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, `mt_input_mapping` dispatches
multitouch fields to `mt_touch_input_mapping`. The `HID_DG_AZIMUTH` branch
states that Azimuth uses `[0, logical_maximum)`, configures
`ABS_MT_ORIENTATION` from that maximum, and stores the field. `mt_touch_event`
defers input handling to report processing; `mt_process_slot` converts the
counter-clockwise sample to the kernel's clockwise orientation convention.
Source:
[`drivers/hid/hid-multitouch.c`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-multitouch.c),
symbols `mt_input_mapping`, `mt_touch_input_mapping`, `mt_touch_event`, and
`mt_process_slot`; retrieved 2026-08-27.

**Resolution:** Enabled touch Azimuth requires Logical Minimum zero and a
positive Logical Maximum. The maximum represents the full-turn endpoint but is
not a valid sample in the audited target policy; the public state API accepts
only `[0, logical_maximum)`. This half-open restriction is a
**[Specified Linux implementation observation]**, not a general HUT rule or an
Android compatibility claim.

### A-15 - Touch release needs an explicit Up record

**[HUT 1.7 definition]** Digitizers §16.6 states that Contact ID remains the same from touch-down through the contact's up notification.

**[Specified Linux implementation observation]** In the audited default class, `mt_process_slot` calls `input_mt_report_slot_state` with the state derived from Tip Switch; the class does not have `MT_QUIRK_NOT_SEEN_MEANS_UP`.

**Resolution:** The library state machine is **[Guide policy]** `None -> Down -> Up -> None`. The Up report repeats the same Contact ID, clears Tip Switch, and remains part of the frame's reported records. Only a successfully completed final packet advances Up to None.

**[Specified Linux implementation observation]** `mt_process_slot` emits X/Y and geometry only inside `if (active)`. Therefore retaining final X/Y in an inactive Up record is not required by this Linux implementation. Retention can remain a shared-profile or diagnostics policy, but it must not be labeled a Linux or HID requirement.

### A-16 - Pressure zero means hover/noncontact, not "no events"

**Input discrepancy:** `DESIGN.md` §8.2 and Phase 4 say pressure zero with Tip set produces no touch events at all. The official Android text does not support that absolute claim.

**[Android platform documentation]** "Touch device driver requirements" says that when `ABS_PRESSURE` or `ABS_MT_PRESSURE` is present, pressure is nonzero while touching and zero otherwise. "Hovering versus touching tools" uses zero pressure to distinguish hovering when the tool is in range.

**Resolution:** If pressure is declared, its logical range must represent zero and at least one positive value. During Tip=1, the portable policy emits at least 1; hover and lift emit 0. If pressure is not measured, omit the field rather than inventing a value. The literal floor of 1 is valid only after the descriptor validator proves that 0 and 1 are representable.

### A-17 - Scan Time is fixed to 100 microseconds for this target profile

**[HUT 1.7 definition]** Digitizers §16.5 defines a default Scan Time unit of 100 microseconds and requires one value for the contacts in the same frame.

**[Specified Linux implementation observation]** `mt_compute_timestamp` multiplies the Scan Time delta by 100 without consulting the HID Unit item.

**Resolution:** The portable profile accepts Scan Time only when the caller
explicitly selects the 100-microsecond counter contract and declares Logical
Minimum `0`. At the first activity frame after inactivity, the state machine
captures `std::chrono::steady_clock` as its epoch and emits `0`. Each later
frame derives elapsed 100-microsecond ticks modulo
`logical_maximum + 1`; every continuation packet reuses the value chosen for
that frame. When successful completion leaves both contacts and touchpad
buttons inactive, it clears the epoch so the next activity frame starts at
`0`. The clock choice, modulo, completion boundary, and inactive reset are
**[Guide policy]** implementation mechanics, not HUT requirements. Any other
unit is target-specific and requires a separate target audit.
Application-visible timestamp behavior is **[Unverified on hardware]**.

### A-18 - Pen tool switching is an implementation-sensitive state machine

**[HUT 1.7 definition]** Digitizers defines `In Range (0x32)`, `Invert (0x3c)`, `Tip Switch (0x42)`, and `Eraser (0x45)`. HUT does not state that Tip implies In Range or that Tip and eraser states are universally mutually exclusive.

**[Specified Linux implementation observation]** `hidinput_configure_usage` maps both Tip Switch and Eraser to `BTN_TOUCH`; order can make two independent Variable fields cancel each other. `hidinput_hid_event` uses Invert to choose `BTN_TOOL_PEN` versus `BTN_TOOL_RUBBER` when In Range becomes active, and releases both tools when In Range becomes inactive.

At commit `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`,
`hid_report_raw_event` visits `report->field[]` in descriptor order and
`hid_input_field` visits Variable usages in field order. The Invert branch in
`hidinput_hid_event` updates `HID_QUIRK_INVERT`, and the later In Range branch
reads that state to choose `BTN_TOOL_PEN` or `BTN_TOOL_RUBBER`.
**[Specified Linux implementation observation]**

Android 17 ACK commit
`f67745b7d96806e622db56f4be97af16d6e99850` adds an explicit
`hidinput_usages_priorities` order of Eraser, Invert, Tip Switch, Tip Pressure,
then In Range; `hid_report_process_ordering` and `hid_process_report` apply
those priorities rather than relying only on descriptor order.
**[Specified Linux implementation observation]** This is an observation of the
named revision, not an Android-wide ordering guarantee.

**Resolution:** The generic-Linux profile uses one Tip Switch for contact and
Invert for tool selection. When eraser-end selection is enabled, its Variable
fields are emitted in the order Invert, Tip Switch, In Range. That ordering is
**[Guide policy]** chosen to preserve the legacy descriptor-order path while
also agreeing with the pinned Android 17 priority relation. Switching pen end
while in range is serialized as departure (`InRange=0`, `Tip=0`), change
Invert, then re-entry. `Tip => InRange` and mutually exclusive tool states are
**[Guide policy]** invariants. A descriptor using a separate Eraser contact
field is target-specific until tested. These source observations do not promote
either pen profile beyond **[Unverified on hardware]**.

### A-19 - X Tilt and Y Tilt are independently defined

**[HUT 1.7 definition]** Digitizers defines `X Tilt (0x3d)` and `Y Tilt (0x3e)` as separate Usages.

**Resolution:** Requiring both together can be a library profile policy, but the audited specifications do not make the pair indivisible. `Twist (0x41)` also remains target-specific for Android until hardware evidence exists.

### A-20 - The prompt's list of "only ceilings" is incomplete

**Input discrepancy:** Prompt 0 lists only the AOA HID-ID space, descriptor length, report control-transfer length, and a 16-pointer policy as allowable ceilings.

**[USB HID 1.11 requirement]** Report ID is an eight-bit nonzero value when declared, and §8.4 imposes the individual-field constraints described in A-06.

**[Specified Linux implementation observation]** The audited revision has `HID_MAX_FIELDS = 256`, `HID_MAX_IDS = 256`, `HID_MAX_USAGES = 12288`, and `HID_MAX_BUFFER_SIZE = 8192`. `hid-multitouch.c` uses a default maximum contact count of 10 and accepts a Contact Count Maximum logical maximum only through 250 in its fallback path.

**Resolution:** Documentation separates protocol/specification limits, exact-revision implementation limits, and library policy limits. It does not claim that any one list is the only set of ceilings. See `LIMITS.md`.

### A-21 - Battery Strength is metadata and has a zero-value target caveat

**[HUT 1.7 definition]** Generic Device Controls Page `0x06` defines `Background/Nonuser Controls (0x01)` as an Application Collection Usage in §9.1 and `Battery Strength (0x20)` as a Dynamic Value in §9.2. Logical Minimum and Logical Maximum define the proportion-of-life range; a Null value means unknown battery status.

**[Specified Linux implementation observation]** When the audited kernel is built with `CONFIG_HID_BATTERY_STRENGTH`, `hidinput_configure_usage` handles Generic Device Controls Battery Strength by calling `hidinput_setup_battery`, assigning `EV_PWR`, and excluding it from ordinary input-event mapping. `hidinput_scale_battery_capacity` maps the declared logical range to 0 through 100. `hidinput_update_battery` ignores a value of zero and every value outside the logical range.

**Source/implementation difference:** HUT permits zero to be a valid capacity when zero lies inside the declared range. The audited Linux implementation nevertheless ignores raw zero. An out-of-range Null value is also ignored, which is compatible with using it to mean "unknown," but no ordinary input event is emitted.

**Resolution:** The Battery profile remains conditional. It uses Generic Device Controls `0x06/0x01` with one Variable Battery Strength `0x20` field. When unknown-state support is enabled, the field width and signedness must leave a representable value outside the logical range and the descriptor sets Null State. The library does not claim that raw zero reaches Linux as 0%, that a `power_supply` is associated with the Android `InputDevice`, or that an application can observe the value. Those paths are **[Unverified on hardware]**.

### A-22 - Host tuning fallbacks are project policy, not protocol limits

**Primary-source finding:** `libusb_control_transfer` and
`libusb_transfer::timeout` define timeout units as milliseconds and define zero
as unlimited/no timeout. They do not recommend one universal positive timeout.
USB 2.0 Revision 2.0 and USB 3.2 Revision 1.1 §§9.2.6.1 and 9.2.6.4 specify
device request-processing timing ceilings for defined request stages; those
ceilings are not a prediction of host scheduling, libusb backend latency, or an
AOA vendor-request completion time.

**Resolution:** the public API interprets zero in eight named Device tuning
fields as 500 ms control/send, 64-byte descriptor fragments, 8 pool slots,
1024-byte maximum report buffers, 1000 ms close drain, 20 total first-report
attempts, and 1000-microsecond retry backoff. Node options zero/zero means no
reservation. Each number is **[Project policy]**, selected for bounded behavior
and consistency with this repository's worked examples and tests; none is
called a USB/AOA/libusb recommendation. The implementation normalizes a local
copy and does not mutate the caller's structure. An explicit nonzero value is
preserved.

A timeout is a failure deadline: a successful backend operation returns on
completion and does not wait for the budget to expire. Product and exact-target
choices remain mandatory. This source audit does not predict OS-specific
latency or establish physical-device behavior; both remain **[Unverified on
hardware]**. See `SOURCE_CONFLICTS.md` T-23 and `LATENCY.md`.

### A-23 - The deterministic hot-path result stops at the libusb boundary

**[Specified libusb implementation observation]** The common asynchronous path
at v1.0.30 commit `87a55632db62c9bdc58cd31d3ccfa673f1bb017f`
allocates each `libusb_transfer` with `calloc` in `libusb_alloc_transfer` and
initializes a per-transfer mutex. `libusb_submit_transfer` takes the Context's
`flying_transfers_lock` and the transfer mutex around list/timer bookkeeping
and backend submission. Reusing libaoahid's transfer pool therefore avoids
re-running `libusb_alloc_transfer` for each report, but does not make submission
free of libusb locks.

**[Specified libusb implementation observation]** In Linux usbfs,
`submit_control_transfer` performs a fresh `calloc` of one `usbfs_urb` for each
control submission and calls `USBDEVFS_SUBMITURB`. The event path takes
`open_devs_lock`, calls `USBDEVFS_REAPURBNDELAY` in `reap_for_handle`, takes the
transfer mutex in `handle_control_completion`, and frees that URB before core
completion handling. On a non-Android Linux host build with device discovery
enabled, `op_init` also starts the build-selected udev or netlink hotplug
monitor for the first Context; both monitor implementations create a pthread.
This monitor is distinct from libaoahid's optional transfer-event thread. The
source has a separate Android-application compile-time branch that starts
neither monitor, so the host observation is not generalized to that build.

**[Specified libusb implementation observation]** The Windows backend creates
one I/O completion port and one `windows_iocp_thread` for every initialized
libusb Context. `windows_open` opens the device with `FILE_FLAG_OVERLAPPED` and
associates the handle with that port. `windows_submit_transfer` takes the
device-handle mutex to maintain `active_transfers`. In the WinUSB-like control
path, `winusbx_submit_control_transfer` passes the `OVERLAPPED` embedded in the
preallocated `windows_transfer_priv` to `ControlTransfer`; an immediate success
is posted to the same completion port, while `ERROR_IO_PENDING` completes there
later. The IOCP thread takes the Context and device-handle locks, removes the
matching active transfer, and calls `usbi_signal_transfer_completion`; a later
libusb event-handler pass runs `windows_handle_transfer_completion` and the
library callback. No explicit per-submit `calloc` occurs in
`winusbx_submit_control_transfer`, but that source-level observation does not
establish that WinUSB, the kernel, or the system allocator performs no hidden
allocation.

**Resolution:** the prewarmed fake-backend test proves only its stated
libaoahid C++ allocation, synchronization-selection, and operation-count
invariants. It does not prove that a real Linux or Windows path is allocation-,
lock-, system-call-, or thread-free. Caller-poll mode creates no libaoahid event
thread, but it does not suppress the backend threads above. Exact source control
flow also provides no duration for a lock, allocation, ioctl, IOCP handoff,
scheduler delay, USB transaction, Android gadget processing, or input dispatch;
fixed OS- or architecture-specific latency values therefore remain
**[Unverified on hardware]**. `LATENCY.md` defines the measurement boundary and
the only conditional caller-poll gap bound. Retrieved 2026-08-27.

### A-24 - Real libusb imports Windows `min`/`max` macros at the public-header boundary

**[Specified Windows implementation observation]** In libusb v1.0.30 commit
`87a55632db62c9bdc58cd31d3ccfa673f1bb017f`, the public `libusb/libusb.h`
includes `<windows.h>` when `_WIN32` or `__CYGWIN__` is defined. Microsoft
Learn's `XMMax` Remarks state that Windows headers conflict with `std::max` and
that `NOMINMAX` must be defined before including those headers. The production
transport uses both `std::min(...)` and the `max` member of
`std::numeric_limits<T>`; the
project's fake libusb header does not import `<windows.h>`, so a fake-backend
Windows build did not exercise this production-header boundary.

**Resolution:** `src/transport/libusb_include.hpp` defines `NOMINMAX` before
including either supported libusb header spelling. A Windows-only compile
regression uses a minimal fixture that exposes the same `min` and `max` macro
names unless `NOMINMAX` is already defined, then compiles both `std::min(...)`
and the `max` member of `std::numeric_limits<T>`. This preprocessor guard adds no runtime
branch, allocation, synchronization, or latency. It establishes build-header
compatibility only; hosted MSVC/ARM64 release jobs still require an actual CI
run, and all physical-device claims remain **[Unverified on hardware]**.
Retrieved 2026-08-27.

## Usage-value audit

Every numeric Usage below was checked in HUT 1.7. This table establishes identifiers only; it does not establish Linux or Android support.

| Page | Usage | ID | Evidence |
|---|---|---:|---|
| Generic Desktop `0x01` | Pointer | `0x01` | **[HUT 1.7 definition]** §4 |
| Generic Desktop `0x01` | Mouse | `0x02` | **[HUT 1.7 definition]** §4 |
| Generic Desktop `0x01` | Joystick | `0x04` | **[HUT 1.7 definition]** §4 |
| Generic Desktop `0x01` | Gamepad | `0x05` | **[HUT 1.7 definition]** §4 |
| Generic Desktop `0x01` | Keyboard | `0x06` | **[HUT 1.7 definition]** §4 |
| Generic Desktop `0x01` | X, Y, Z | `0x30`, `0x31`, `0x32` | **[HUT 1.7 definition]** §4 |
| Generic Desktop `0x01` | Rx, Ry, Rz | `0x33`, `0x34`, `0x35` | **[HUT 1.7 definition]** §4 |
| Generic Desktop `0x01` | Slider, Dial, Wheel, Hat switch | `0x36`, `0x37`, `0x38`, `0x39` | **[HUT 1.7 definition]** §4 |
| Generic Desktop `0x01` | System Control | `0x80` | **[HUT 1.7 definition]** §4 |
| Generic Desktop `0x01` | System Power Down, System Menu Right | `0x81`, `0x8a` | **[HUT 1.7 definition]** §§4, 4.5 |
| Generic Desktop `0x01` | D-pad Up, Down, Right, Left | `0x90`, `0x91`, `0x92`, `0x93` | **[HUT 1.7 definition]** §4 |
| Generic Desktop `0x01` | System Function Shift, System Function Shift Lock | `0x97`, `0x98` | **[HUT 1.7 definition]** §§4, 4.5 |
| Simulation Controls `0x02` | Rudder, Throttle | `0xba`, `0xbb` | **[HUT 1.7 definition]** §5 |
| Simulation Controls `0x02` | Accelerator, Brake, Steering | `0xc4`, `0xc5`, `0xc8` | **[HUT 1.7 definition]** §5 |
| Generic Device Controls `0x06` | Background/Nonuser Controls | `0x01` | **[HUT 1.7 definition]** §9.1 |
| Generic Device Controls `0x06` | Battery Strength | `0x20` | **[HUT 1.7 definition]** §9.2; Dynamic Value |
| Keyboard/Keypad `0x07` | ErrorRollOver | `0x01` | **[HUT 1.7 definition]** §10 |
| Keyboard/Keypad `0x07` | Keyboard a and A through Keyboard Application | `0x04` through `0x65` | **[HUT 1.7 definition]** §10 |
| Keyboard/Keypad `0x07` | Left Control through Right GUI | `0xe0` through `0xe7` | **[HUT 1.7 definition]** §10 |
| Button `0x09` | Button selectors | `0x01` through `0xffff` | **[HUT 1.7 definition]** §12 |
| Telephony `0x0b` | Phone | `0x01` | **[HUT 1.7 definition]** §14 |
| Telephony `0x0b` | Hook Switch, Flash, Redial | `0x20`, `0x21`, `0x24` | **[HUT 1.7 definition]** §§14, 14.3 |
| Telephony `0x0b` | Phone Key 0 | `0xb0` | **[HUT 1.7 definition]** §§14, 14.2 |
| Consumer `0x0c` | Consumer Control | `0x01` | **[HUT 1.7 definition]** §15 |
| Consumer `0x0c` | Play, Enter Disc, Play/Pause | `0x00b0`, `0x00bb`, `0x00cd` | **[HUT 1.7 definition]** §§15, 15.7 |
| Consumer `0x0c` | Volume, Mute, Volume Increment | `0x00e0`, `0x00e2`, `0x00e9` | **[HUT 1.7 definition]** §§15, 15.9 |
| Consumer `0x0c` | AC New | `0x0201` | **[HUT 1.7 definition]** §§15, 15.16 |
| Consumer `0x0c` | AC Pan | `0x0238` | **[HUT 1.7 definition]** §15 |
| Digitizers `0x0d` | Digitizer, Pen, Touch Screen, Touch Pad | `0x01`, `0x02`, `0x04`, `0x05` | **[HUT 1.7 definition]** §16 |
| Digitizers `0x0d` | Stylus, Finger | `0x20`, `0x22` | **[HUT 1.7 definition]** §16 |
| Digitizers `0x0d` | Tip Pressure, In Range, Invert | `0x30`, `0x32`, `0x3c` | **[HUT 1.7 definition]** §16 |
| Digitizers `0x0d` | X Tilt, Y Tilt, Azimuth, Twist | `0x3d`, `0x3e`, `0x3f`, `0x41` | **[HUT 1.7 definition]** §16 |
| Digitizers `0x0d` | Tip Switch, Barrel Switch, Eraser, Touch Valid | `0x42`, `0x44`, `0x45`, `0x47` | **[HUT 1.7 definition]** §16 |
| Digitizers `0x0d` | Width, Height | `0x48`, `0x49` | **[HUT 1.7 definition]** §16 |
| Digitizers `0x0d` | Contact Identifier, Contact Count, Contact Count Maximum, Scan Time | `0x51`, `0x54`, `0x55`, `0x56` | **[HUT 1.7 definition]** §16 |
| Digitizers `0x0d` | Secondary Barrel Switch | `0x5a` | **[HUT 1.7 definition]** §16 |
| Camera Control `0x90` | Camera Auto-focus, Camera Shutter | `0x20`, `0x21` | **[HUT 1.7 definition]** Table 35.1, §35.1 |

No mismatch was found in this audited subset of numeric Usage IDs. That result does not validate unlisted constants.

## Facts not established by this audit

The official sources above do **not** establish any of the following:

- that any generated profile is accepted by a particular Android phone, OEM kernel, or Android release;
- that source-level Linux event mappings survive the target key layout, input-device configuration, Android policy, focus, IME, or media-session layers;
- that an arbitrary gamepad axis midpoint is its semantic neutral value;
- that an isolated Contact Count zero report releases all contacts;
- that retaining X/Y on an inactive touch Up record is required by USB HID or by the audited Linux revision;
- that Tip Switch implies In Range, or that Tip and eraser are mutually exclusive, outside this library's profile contract;
- that a Consumer Usage is application-visible merely because `hid-input.c` maps it;
- that Generic Device Controls Battery Strength becomes Android `InputDevice` battery metadata merely because the audited Linux source can register a `power_supply`;
- that a parser accepting a field wider than 32 bits makes that field HID 1.11 conformant;
- that a current or future Linux kernel behaves like commit `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`.

These questions remain **[Unverified on hardware]** and are release blockers for any corresponding "Android supported" claim.
