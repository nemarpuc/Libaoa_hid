# Source conflicts and implementation decisions

This is the decision memo requested before implementation. It records facts in
the supplied `AOA_HID_GUIDE.md`, `DESIGN.md`, or implementation prompt that did
not survive a fresh check against an official primary source. The supplied
documents are preserved unchanged so each decision can be reviewed separately.

All sources were retrieved on **2026-08-27**. No training-data recollection,
blog, forum, vendor sample, or AI summary is evidence in this memo.

## Evidence labels

| Label | Meaning |
|---|---|
| **[AOA requirement]** | Official Android Open Accessory 1.0 or 2.0 page. |
| **[libusb contract]** | Official libusb 1.0.30 API documentation. |
| **[implementation observation]** | An exact immutable AOSP, kernel, or libusb source revision; not a timeless requirement. |
| **[Windows requirement]** | Microsoft Learn; never generalized to Android. |
| **[Android API contract]** | Public developer.android.com API documentation. |
| **[GitHub Actions contract]** | Current official GitHub Actions or REST API documentation; a workflow-platform fact, not a protocol requirement. |
| **[CMake contract]** | Current official CMake documentation for a generator or build-system behavior. |
| **[compiler sanitizer contract]** | Versioned official GCC or Clang documentation for sanitizer instrumentation, compatibility, run-time behavior, limitations, or overhead. It is not a performance result for this library. |
| **[repository observation]** | An exact observation of this source tree, generated build metadata, binary, or deterministic test; not an external protocol or platform guarantee. |
| **[pkg-config contract]** | The official freedesktop.org pkg-config manual or guide; build metadata behavior, not a protocol requirement. |
| **[SPDX 2.3 contract]** | The official SPDX 2.3 specification for document, package, file, checksum, and relationship fields. |
| **[HID 1.11 requirement]** | Device Class Definition for HID 1.11 descriptor and report rule. |
| **[HUT 1.7 definition]** | A Usage ID, Usage Type, or Usage meaning in the official HID Usage Tables 1.7; not an OS-support claim. |
| **[project policy]** | A conservative library decision, explicitly not a protocol limit. |
| **[unverified on hardware]** | No physical target passed all four target-matrix layers. |

## Primary-source register

| Source | Revision / page version | Section or symbol read | Official URL |
|---|---|---|---|
| Android Open Accessory 1.0 | Page updated 2024-08-26 UTC | "Attempt to start in accessory mode"; requests 51-53 and strings | [AOA 1.0](https://source.android.com/docs/core/interaction/accessories/aoa) |
| Android Open Accessory 2.0 | Page updated 2024-08-26 UTC | "Detect AOAv2 support", "Audio support", "HID support", app-less connection | [AOA 2.0](https://source.android.com/docs/core/interaction/accessories/aoa2) |
| USB 2.0 Specification | Revision 2.0; retrieved 2026-08-27 | §§9.2.6.1 and 9.2.6.4, device request-processing timing | [official USB-IF specification page](https://www.usb.org/document-library/usb-20-specification) |
| USB 3.2 Specification | Revision 1.1, June 2022; retrieved 2026-08-27 | §§9.2.6.1 and 9.2.6.4, device request-processing timing | [official USB-IF specification page](https://www.usb.org/document-library/usb-32-revision-11-june-2022) |
| Android common accessory function | `0e3db17d01c94263b629b089c51c7d0988308232` | `f_accessory.c`: `acc_ctrlrequest`, `start_requested`, and the sibling request 53-57 branches | [`f_accessory.c`](https://android.googlesource.com/kernel/common/+/0e3db17d01c94263b629b089c51c7d0988308232/drivers/usb/gadget/function/f_accessory.c) |
| Pixel 2 / wahoo accessory function | `594d847d09a14b0b2e5db288024a0f2c4f64ec9a` | `acc_ctrlrequest`, descriptor/event completions, HID worker, disconnect cleanup | [`f_accessory.c`](https://android.googlesource.com/kernel/msm/+/594d847d09a14b0b2e5db288024a0f2c4f64ec9a/drivers/usb/gadget/function/f_accessory.c) |
| Pixel 2 / wahoo gadget setup | Same commit | `android_setup`, `android_disconnect` | [`configfs.c`](https://android.googlesource.com/kernel/msm/+/594d847d09a14b0b2e5db288024a0f2c4f64ec9a/drivers/usb/gadget/configfs.c) |
| 2024 common-kernel AOA patch | `9b03ed2feb9c4cc3f15d44eda080caabb3a6b843` | Commit message, `composite_setup`, `__acc_req_match`, `__acc_setup`, `__composite_disconnect` | [immutable patch](https://android.googlesource.com/kernel/common/+/9b03ed2feb9c4cc3f15d44eda080caabb3a6b843%5E%21/) |
| libusb asynchronous API | libusb 1.0.30, generated 2026-05-18; source commit `87a55632db62c9bdc58cd31d3ccfa673f1bb017f`; retrieved 2026-08-27 | "Asynchronous transfers", `libusb_submit_transfer`, control `actual_length`, cancellation, timeouts, event handling, length limits, `LIBUSB_TRANSFER_OVERFLOW` | [official API](https://libusb.sourceforge.io/api-1.0/group__libusb__asyncio.html) |
| libusb synchronous I/O | libusb 1.0.30, generated 2026-05-18; retrieved 2026-08-27 | `libusb_control_transfer`, timeout units and zero semantics | [official API](https://libusb.sourceforge.io/api-1.0/group__libusb__syncio.html) |
| libusb transfer structure | libusb 1.0.30, generated 2026-05-18; retrieved 2026-08-27 | `libusb_transfer::timeout`, timeout units and zero semantics | [official API](https://libusb.sourceforge.io/api-1.0/structlibusb__transfer.html) |
| libusb multi-threaded event handling | libusb 1.0.30, generated 2026-05-18; retrieved 2026-08-27 | Opening example; "Using an event handling thread"; "libusb_handle_events() from multiple threads"; "Closing remarks" on long timeouts and avoiding short-timeout polling | [official API](https://libusb.sourceforge.io/api-1.0/libusb_mtasync.html) |
| libusb polling and event interruption | libusb 1.0.30, generated 2026-05-18; retrieved 2026-08-27 | "Polling and timing"; `libusb_handle_events_timeout_completed`; `libusb_interrupt_event_handler` since 1.0.21 | [official API](https://libusb.sourceforge.io/api-1.0/group__libusb__poll.html) |
| libusb library initialization | libusb 1.0.30, generated 2026-05-18; retrieved 2026-08-27 | `libusb_context`, `libusb_init_option`, `libusb_init_context`, `libusb_set_option`, `LIBUSB_OPTION_LOG_LEVEL`, `LIBUSB_OPTION_LOG_CB`, `LIBUSB_LOG_LEVEL_NONE`, `libusb_set_log_cb` | [official API](https://libusb.sourceforge.io/api-1.0/group__libusb__lib.html) |
| libusb miscellaneous/version API | libusb 1.0.30, generated 2026-05-18; retrieved 2026-08-27 | "Miscellaneous"; `LIBUSB_API_VERSION`, `libusb_get_version`, `libusb_version` | [official API](https://libusb.sourceforge.io/api-1.0/group__libusb__misc.html) |
| libusb device enumeration | libusb 1.0.30, generated 2026-05-18 | `libusb_get_port_number`, `libusb_get_port_numbers` | [official API](https://libusb.sourceforge.io/api-1.0/group__libusb__dev.html) |
| libusb source | v1.0.30 commit `87a55632db62c9bdc58cd31d3ccfa673f1bb017f`; retrieved 2026-08-27 | `auto_claim`, `get_valid_interface`, `libusb_init_context`, `libusb_set_option`, `log_v`, `log_str`; `libusb_alloc_transfer`, `libusb_submit_transfer`, `usbi_signal_transfer_completion`, `handle_event_trigger`; Linux `op_init`, `submit_control_transfer`, `handle_control_completion`, `reap_for_handle`, `op_handle_events`, and udev/netlink monitor startup; Windows `windows_init`, `windows_iocp_thread`, `windows_open`, `windows_submit_transfer`, `winusbx_submit_control_transfer`, `windows_handle_transfer_completion`, and `windows_transfer_priv` | [source tree](https://github.com/libusb/libusb/tree/87a55632db62c9bdc58cd31d3ccfa673f1bb017f), [immutable `io.c`](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/io.c), [Linux usbfs](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/linux_usbfs.c), [Linux monitor selection](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/linux_usbfs.h), [udev monitor](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/linux_udev.c), [netlink monitor](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/linux_netlink.c), [Windows common](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/windows_common.c), [Windows private types](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/windows_common.h), [WinUSB-like backend](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/windows_winusb.c) |
| libusb initialization/logging source | v1.0.30 commit `87a55632db62c9bdc58cd31d3ccfa673f1bb017f`; retrieved 2026-08-27 | `libusb_init_context`, `libusb_set_option`, `libusb_set_log_cb_internal`, `log_v`, `log_str` in `libusb/core.c` | [immutable `core.c`](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/core.c) |
| libusb event-interrupt source | v1.0.30 commit `87a55632db62c9bdc58cd31d3ccfa673f1bb017f`; retrieved 2026-08-27 | `libusb_interrupt_event_handler`, `handle_event_trigger`, `handle_events`, `libusb_handle_events_timeout_completed` in `libusb/io.c`; declaration and `LIBUSB_API_VERSION` in `libusb/libusb.h` | [immutable `io.c`](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/io.c), [immutable `libusb.h`](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/libusb.h) |
| vcpkg libusb port | vcpkg commit `ddd0023b0eee70986e42ed49d9d4afb8098f212e`; retrieved 2026-08-27 | `ports/libusb/vcpkg.json` version and dependency metadata | [immutable port manifest](https://github.com/microsoft/vcpkg/blob/ddd0023b0eee70986e42ed49d9d4afb8098f212e/ports/libusb/vcpkg.json) |
| WinUSB control transfer | Page updated 2022-10-14 | `WinUsb_ControlTransfer` parameters and limits | [Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winusb/nf-winusb-winusb_controltransfer) |
| WinUSB architecture | Page updated 2024-09-20 | Composite PDO and interface handles | [Microsoft Learn](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb-architecture) |
| AOSP native input main | `4f463a6b1de9198963dc6aff74154a504ba3f8f6` | `MAX_POINTERS`, `syncTouch`, `computeDeviceType`, EventHub classification, IDC lookup | [immutable tree](https://android.googlesource.com/platform/frameworks/native/+/4f463a6b1de9198963dc6aff74154a504ba3f8f6/) |
| AOSP native input Android 16 | `android-16.0.0_r4`, commit `fcbde2bcff56ca1d7bec9cabf8bdca5e288bf103` | Same symbols for release comparison | [immutable tree](https://android.googlesource.com/platform/frameworks/native/+/fcbde2bcff56ca1d7bec9cabf8bdca5e288bf103/) |
| AOSP native input Android 10 | `android-10.0.0_r47`, commit `013eb744f289c70e6d3fe180500d9a414ac55539` | `getInputDeviceConfigurationFilePathByDeviceIdentifier` | [immutable tree](https://android.googlesource.com/platform/frameworks/native/+/013eb744f289c70e6d3fe180500d9a414ac55539/) |
| AOSP native input Android 8 | `android-8.0.0_r1`, commit `7d951867416234a6f3228eb9d888009184b1d0b7` | IDC configuration and configuration-file lookup | [immutable tree](https://android.googlesource.com/platform/frameworks/native/+/7d951867416234a6f3228eb9d888009184b1d0b7/) |
| Android touch documentation | Page updated 2026-07-13 | Driver requirements; hovering versus touching | [official documentation](https://source.android.com/docs/core/interaction/input/touch-devices) |
| Android `MotionEvent` | Current on retrieval date | `getPointerCount()` | [API reference](https://developer.android.com/reference/android/view/MotionEvent#getPointerCount()) |
| Android `Build.VERSION_CODES_FULL` | Page updated 2026-08-14 UTC; retrieved 2026-08-27 | `BAKLAVA`, `BAKLAVA_1`, `CINNAMON_BUN`, `CINNAMON_BUN_1`, `CINNAMON_BUN_2` | [API reference](https://developer.android.com/reference/android/os/Build.VERSION_CODES_FULL) |
| Android `HidManager` / `HidDevice` | Pages updated 2026-08-03 UTC; retrieved 2026-08-27 | Exact public method surfaces and `ACCESS_HID` permission requirements | [`HidManager`](https://developer.android.com/reference/android/hardware/hid/HidManager), [`HidDevice`](https://developer.android.com/reference/android/hardware/hid/HidDevice) |
| Android common Linux HID parser | `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`; retrieved 2026-08-27 | `fetch_item`, `hid_open_report`, `hid_parser_main`, `hid_parser_global`, `hid_parser_local`, `hid_add_usage`, `hid_add_field`, `hid_register_field`, `open_collection`, `close_collection`; `HID_MAX_FIELDS`, `HID_MAX_USAGES`, `HID_MAX_BUFFER_SIZE`, `HID_GLOBAL_STACK_SIZE` | [`hid-core.c`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-core.c), [`hid.h`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/include/linux/hid.h) |
| Android common Linux HID input | `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`; retrieved 2026-08-27 | `hid_report_raw_event`, `hid_input_field`; `hidinput_hid_event`, `hid_hat_to_axis` | [`hid-core.c`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-core.c), [`hid-input.c`](https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-input.c) |
| Android 17 ACK HID input/core | `android17-6.18` commit `f67745b7d96806e622db56f4be97af16d6e99850`; retrieved 2026-08-27 | `hid_hat_to_axis`, `hidinput_usages_priorities`, `hidinput_configure_usage`, `hidinput_hid_event`; `hid_process_report`, `__hid_insert_field_entry`, `hid_report_process_ordering` | [`hid-input.c`](https://android.googlesource.com/kernel/common/+/f67745b7d96806e622db56f4be97af16d6e99850/drivers/hid/hid-input.c), [`hid-core.c`](https://android.googlesource.com/kernel/common/+/f67745b7d96806e622db56f4be97af16d6e99850/drivers/hid/hid-core.c) |
| Android 17 Compatibility Definition | Android 17; retrieved 2026-08-27 | §7.2.6.1, Game Controller Support: Game Pad Application Collection, Button Page A/B/X/Y mappings, and canonical Hat Switch contract | [Android 17 CDD](https://source.android.com/docs/compatibility/17/android-17-cdd) |
| Android game-controller input documentation | Page updated 2026-02-26; retrieved 2026-08-27 | Controller buttons, axes, and motion ranges | [Controller input](https://developer.android.com/games/sdk/game-controller/controller-input) |
| GitHub-hosted runner images | Current "Available Images" table; retrieved 2026-08-27 | Exact configured labels `ubuntu-22.04`, `ubuntu-22.04-arm`, `windows-2025-vs2026`, and `windows-11-vs2026-arm`; the current GA x64 Windows label is `windows-2025-vs2026` | [official `actions/runner-images` available-image table](https://github.com/actions/runner-images#available-images) |
| GitHub workflow events | Current on retrieval date | `push.tags` triggering | [GitHub Actions documentation](https://docs.github.com/actions/using-workflows/events-that-trigger-workflows) |
| GitHub REST API versions | `2026-03-10`, current on retrieval date | "Specifying an API version" and "Supported API versions" | [GitHub REST API documentation](https://docs.github.com/en/rest/about-the-rest-api/api-versions) |
| GitHub CLI release/API commands | Current on retrieval date; retrieved 2026-08-27 | `gh release create`, `view`, `delete`; draft behavior; `--verify-tag`; `gh api --paginate --slurp` object/array wrapping | [`create`](https://cli.github.com/manual/gh_release_create), [`view`](https://cli.github.com/manual/gh_release_view), [`delete`](https://cli.github.com/manual/gh_release_delete), [`api`](https://cli.github.com/manual/gh_api) |
| GitHub REST releases | API `2026-03-10`; retrieved 2026-08-27 | List/Create/Get/Update/Delete release; numeric `release_id`; `draft`; `target_commitish` unused/ignored for an existing tag | [release endpoints](https://docs.github.com/en/rest/releases/releases) |
| GitHub REST release assets | API `2026-03-10`; retrieved 2026-08-27 | List/Upload/Get release asset; paginated array; response `id`, `name`, `state`, `size`, nullable `digest`; binary GET `200`/`302`; failed upload `starter` state | [asset endpoints](https://docs.github.com/en/rest/releases/assets) |
| GitHub REST OpenAPI release-asset schema | Commit `516b99455036689cd7d9cf6355d5708cc89f1bbc`, API `2026-03-10`; retrieved 2026-08-27 | `release-asset`: `digest` is required but nullable; release `upload_url` hypermedia relation | [immutable OpenAPI YAML](https://github.com/github/rest-api-description/blob/516b99455036689cd7d9cf6355d5708cc89f1bbc/descriptions/api.github.com/api.github.com.2026-03-10.yaml) |
| GitHub REST Git database | API `2026-03-10`; retrieved 2026-08-27 | Get a reference; annotated tag object versus lightweight reference | [references](https://docs.github.com/en/rest/git/refs), [tag objects](https://docs.github.com/en/rest/git/tags) |
| GitHub workflow-run API and contexts | API `2026-03-10`; retrieved 2026-08-27 | List runs response object (`total_count`, `workflow_runs`), `head_sha`; `github.run_id`, `github.run_attempt`, and `vars` contexts | [workflow runs](https://docs.github.com/en/rest/actions/workflow-runs), [contexts](https://docs.github.com/en/actions/reference/workflows-and-actions/contexts) |
| Pinned official workflow actions | Exact commits; retrieved 2026-08-27 | Each repository's `action.yml` at the workflow pin | [`checkout` `3d3c42e`](https://github.com/actions/checkout/commit/3d3c42e5aac5ba805825da76410c181273ba90b1), [`setup-python` `5fda3b9`](https://github.com/actions/setup-python/commit/5fda3b95a4ea91299a34e894583c3862153e4b97), [`setup-dotnet` `26b0ec1`](https://github.com/actions/setup-dotnet/commit/26b0ec14cb23fa6904739307f278c14f94c95bf1), [`setup-java` `dd06d9c`](https://github.com/actions/setup-java/commit/dd06d9cba3e5552c54d9f8ea23572deb30010f7c), [`setup-gradle` `9c97196`](https://github.com/gradle/actions/commit/9c971963bec38e04b3d30dcc455b5382be2fdbfb), [`upload-artifact` `043fb46`](https://github.com/actions/upload-artifact/commit/043fb46d1a93c77aae656e7c1c64a875d1fc6a0a), [`download-artifact` `3e5f45b`](https://github.com/actions/download-artifact/commit/3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c), [`upload-pages-artifact` `fc324d3`](https://github.com/actions/upload-pages-artifact/commit/fc324d3547104276b827a68afc52ff2a11cc49c9), [`deploy-pages` `cd2ce8f`](https://github.com/actions/deploy-pages/commit/cd2ce8fcbc39b97be8ca5fce6e763baed58fa128) |
| Pinned PyPA publication action | Commit `ed0c53931b1dc9bd32cbe73a98c7f6766f8a527e`; retrieved 2026-08-27 | `action.yml` at the exact v1.13.0 pin | [immutable action commit](https://github.com/pypa/gh-action-pypi-publish/commit/ed0c53931b1dc9bd32cbe73a98c7f6766f8a527e) |
| CMake Visual Studio generator | CMake 4.4.3 documentation; retrieved 2026-08-27 | "Visual Studio 18 2026"; added in 4.2; `-A x64` and `-A ARM64` | [official CMake 4.4 documentation](https://cmake.org/cmake/help/v4.4/generator/Visual%20Studio%2018%202026.html) |
| CMake installed-package and link interfaces | CMake 4.4.3 docs; implementation remains compatible with project minimum 3.20; retrieved 2026-08-27 | `find_package` components/config version selection; `INTERFACE_LINK_LIBRARIES`; `CMAKE_<LANG>_IMPLICIT_LINK_LIBRARIES`; `configure_file(@ONLY)` | [`find_package`](https://cmake.org/cmake/help/v4.4/command/find_package.html), [`INTERFACE_LINK_LIBRARIES`](https://cmake.org/cmake/help/v4.4/prop_tgt/INTERFACE_LINK_LIBRARIES.html), [implicit link libraries](https://cmake.org/cmake/help/v4.4/variable/CMAKE_LANG_IMPLICIT_LINK_LIBRARIES.html), [`configure_file`](https://cmake.org/cmake/help/v4.4/command/configure_file.html) |
| GCC ThreadSanitizer | GCC 13.3.0; retrieved 2026-08-27 | Program Instrumentation Options §3.12, `-fsanitize=thread`, `TSAN_OPTIONS`, sanitizer incompatibilities; Link Options §3.15, `-static-libtsan` | [instrumentation options](https://gcc.gnu.org/onlinedocs/gcc-13.3.0/gcc/Instrumentation-Options.html), [link options](https://gcc.gnu.org/onlinedocs/gcc-13.3.0/gcc/Link-Options.html) |
| GCC ThreadSanitizer condition-variable defect | GCC Bugzilla 101978, status NEW; reproduced with Ubuntu 22.04 GCC 11.4/libtsan0 and retrieved 2026-08-27 | `std::condition_variable` false-positive report in GCC 11 ThreadSanitizer | [GCC bug 101978](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=101978) |
| Clang ThreadSanitizer | Clang 22.1.0 stable documentation; retrieved 2026-08-27 | "Introduction", "Supported Platforms", "Usage", "Limitations", and "Security Considerations" | [versioned ThreadSanitizer guide](https://releases.llvm.org/22.1.0/tools/clang/docs/ThreadSanitizer.html) |
| Clang ThreadSanitizer run-time controls | Clang 24.0.0git documentation current on retrieval date; retrieved 2026-08-27 | "Limitations" on instrumenting all code; "Run-time Flags", including `halt_on_error` | [current ThreadSanitizer guide](https://clang.llvm.org/docs/ThreadSanitizer.html) |
| CMake target instrumentation options | CMake 3.20.6; retrieved 2026-08-27 | `target_compile_options`, `target_link_options` | [`target_compile_options`](https://cmake.org/cmake/help/v3.20/command/target_compile_options.html), [`target_link_options`](https://cmake.org/cmake/help/v3.20/command/target_link_options.html) |
| Apple dynamic-library export guidance | Archived page published 2012-07-23; retrieved 2026-08-27 | "Exporting Symbols"; leading-underscore Mach-O export-list spelling; `-exported_symbols_list` | [Dynamic Library Design Guidelines](https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/DynamicLibraries/100-Articles/DynamicLibraryDesignGuidelines.html) |
| Apple `ld64` source and manual | Commit `f60a74eaa2c99585de1dc0f2820e7a9f8aaf522c`; retrieved 2026-08-27 | `ld-classic.1` `-exported_symbols_list`; `Options.cpp`: `SetWithWildcards`, `kAllowWildcards`, `loadExportFile` | [immutable manual](https://github.com/apple-oss-distributions/ld64/blob/f60a74eaa2c99585de1dc0f2820e7a9f8aaf522c/doc/man/man1/ld-classic.1), [immutable `Options.cpp`](https://github.com/apple-oss-distributions/ld64/blob/f60a74eaa2c99585de1dc0f2820e7a9f8aaf522c/src/ld/Options.cpp) |
| Apple `cctools` `nm` source | Commit `e0d56624eca2a76c2ace4c21850df9e666de4ca5`; retrieved 2026-08-27 | `misc/nm.c`: `select_symbol`, `print_mach_symbols`, `N_EXT`, `N_PEXT`, `-g`, `-m`, and `-U` | [immutable `nm.c`](https://github.com/apple-oss-distributions/cctools/blob/e0d56624eca2a76c2ace4c21850df9e666de4ca5/misc/nm.c) |
| CMake compiler and PIE probes | CMake 3.20.6; retrieved 2026-08-27 | `CheckCSourceCompiles`, `CheckCXXSourceCompiles`, `CheckPIESupported`, `POSITION_INDEPENDENT_CODE`, policy `CMP0083` | [`CheckCSourceCompiles`](https://cmake.org/cmake/help/v3.20/module/CheckCSourceCompiles.html), [`CheckCXXSourceCompiles`](https://cmake.org/cmake/help/v3.20/module/CheckCXXSourceCompiles.html), [`CheckPIESupported`](https://cmake.org/cmake/help/v3.20/module/CheckPIESupported.html), [`POSITION_INDEPENDENT_CODE`](https://cmake.org/cmake/help/v3.20/prop_tgt/POSITION_INDEPENDENT_CODE.html), [`CMP0083`](https://cmake.org/cmake/help/v3.20/policy/CMP0083.html) |
| CMake package-helper licensing | CMake 4.4.3 docs/source; retrieved 2026-08-27 | Generated installed config/version purpose; module BSD-3-Clause header; CMake licensing page | [`CMakePackageConfigHelpers`](https://cmake.org/cmake/help/v4.4/module/CMakePackageConfigHelpers.html), [module source at tag `v4.4.3`](https://github.com/Kitware/CMake/blob/v4.4.3/Modules/CMakePackageConfigHelpers.cmake), [licensing](https://cmake.org/licensing/) |
| pkg-config metadata | `pkg-config` source commit `9294307b213c157db991d838654e7fef6842b1de`; retrieved 2026-08-27 | `pcfiledir`; `Requires.private`, `Libs.private`, `Cflags`, and static traversal | [manual](https://cgit.freedesktop.org/pkg-config/tree/pkg-config.1?id=9294307b213c157db991d838654e7fef6842b1de), [guide](https://cgit.freedesktop.org/pkg-config/tree/pkg-config-guide.html?id=9294307b213c157db991d838654e7fef6842b1de) |
| GNU `readelf` and Ubuntu 22.04 glibc | Current binutils docs and Jammy package page; retrieved 2026-08-27 | `--version-info`; Ubuntu 22.04 `libc6` 2.35 baseline | [`readelf`](https://sourceware.org/binutils/docs/binutils/readelf.html), [Ubuntu `libc6`](https://packages.ubuntu.com/jammy/libc6) |
| LLVM object inspection | LLVM 22.1.0 docs current on retrieval date; retrieved 2026-08-27 | `llvm-readobj --file-headers`, COFF archive/member headers | [`llvm-readobj`](https://llvm.org/docs/CommandGuide/llvm-readobj.html) |
| SPDX specification | SPDX 2.3; retrieved 2026-08-27 | Document creation; Package and File Information; checksums; `DESCRIBES`, `CONTAINS`, `DEPENDS_ON`, `BUILD_DEPENDENCY_OF` | [SPDX 2.3](https://spdx.github.io/spdx-spec/v2.3/) |
| vcpkg Windows libusb material | vcpkg `ddd0023b0eee70986e42ed49d9d4afb8098f212e`; libusb v1.0.30; retrieved 2026-08-27 | MIT `LICENSE.txt`; libusb `portfile.cmake`; `vcpkg_fixup_pkgconfig`; x64/ARM64 dynamic triplets; upstream `libusb-1.0.pc.in` | [vcpkg license](https://github.com/microsoft/vcpkg/blob/ddd0023b0eee70986e42ed49d9d4afb8098f212e/LICENSE.txt), [portfile](https://github.com/microsoft/vcpkg/blob/ddd0023b0eee70986e42ed49d9d4afb8098f212e/ports/libusb/portfile.cmake), [pkg-config fixup](https://github.com/microsoft/vcpkg/blob/ddd0023b0eee70986e42ed49d9d4afb8098f212e/scripts/cmake/vcpkg_fixup_pkgconfig.cmake), [upstream template](https://github.com/libusb/libusb/blob/v1.0.30/libusb-1.0.pc.in) |
| Python and NuGet publication | Current on retrieval date; retrieved 2026-08-27 | PyPI Trusted Publisher GitHub flow; `dotnet pack`; deterministic package build; `dotnet nuget push` API-key/source arguments | [PyPI Trusted Publishers](https://docs.pypi.org/trusted-publishers/using-a-publisher/), [PyPA GitHub publishing guide](https://packaging.python.org/guides/publishing-package-distribution-releases-using-github-actions-ci-cd-workflows/), [`dotnet pack`](https://learn.microsoft.com/en-us/dotnet/core/tools/dotnet-pack), [NuGet MSBuild targets](https://learn.microsoft.com/en-us/nuget/reference/msbuild-targets), [`dotnet nuget push`](https://learn.microsoft.com/en-us/dotnet/core/tools/dotnet-nuget-push) |
| Python build-tool artifacts | Official PyPI JSON API; retrieved 2026-08-27 | Exact wheel filenames and SHA-256 digests for the fully enumerated build closure | [`build` 1.5.0](https://pypi.org/pypi/build/1.5.0/json), [`packaging` 26.0](https://pypi.org/pypi/packaging/26.0/json), [`pyproject-hooks` 1.2.0](https://pypi.org/pypi/pyproject-hooks/1.2.0/json), [`setuptools` 84.0.0](https://pypi.org/pypi/setuptools/84.0.0/json) |
| HID class definition | Version 1.11; retrieved 2026-08-27 | §§6.2.2.2 and 6.2.2.6 required control/Collection items; §6.2.2.7 Global Items and physical/unit rules; §6.2.2.8 Local items and Delimiter exclusions; §8.4 report constraints; Appendix B.1 keyboard descriptor/report layout; Appendix C keyboard requirements; Appendix E.6 keyboard example | [official HID 1.11 PDF](https://www.usb.org/sites/default/files/hid1_11.pdf) |
| HID Usage Tables | Version 1.7, 2026-01-26; retrieved 2026-08-27 | §3.3; §3.4, Tables 3.2-3.3, §3.4.4; Generic Desktop Table 4.1 and §4.5; Digitizers §16.5 Scan Time; Telephony table and §§14.2-14.3; Consumer table and §§15.7, 15.9, 15.16; Camera Table 35.1 and §35.1 | [official HUT 1.7 PDF](https://www.usb.org/sites/default/files/hut1_7.pdf) |

HID, Usage, and Linux HID/multitouch conflicts are separately enumerated in
`FACT_AUDIT.md` with the HID 1.11 and HUT 1.7 sections.

## AOA and transport discrepancies

### T-01 - Windows interface 0 is not a requirement

**Input text:** `DESIGN.md` §7.3/§16 and Phase 3 require interface 0 on
Windows.

**Finding:** libusb v1.0.30 `get_valid_interface` selects a serviceable WinUSB
interface and `auto_claim` searches interfaces by backend capability. Microsoft
requires a valid WinUSB interface handle, not physical `bInterfaceNumber=0`.
This is **[implementation observation]** plus **[Windows requirement]**.

**Implemented resolution:** never hard-code interface 0. Device-recipient EP0
uses libusb backend routing unless the caller explicitly names an interface;
only an explicitly claimed interface is released. No Windows statement is
presented as an Android rule.

### T-02 - A shared pool cannot prefill `wValue`

**Input text:** the shared per-device pool pre-fills the whole request 57 setup.

**Finding:** AOA request 57 puts the per-node AOA HID ID in `wValue`. One slot
is reused by different nodes, so that value cannot be constant. The request
field is an **[AOA requirement]**; the shared-slot consequence is project
design.

**Implemented resolution:** prefill only `bmRequestType=0x40`, request 57, and
`wIndex=0`. Write `wValue` and `wLength` on every pool acquisition.
**[project policy]**

### T-03 - Timeout/cancel delivery is unknown, so blind retry is unsafe

**Input text:** retain pending data after an ordinary transfer error and retry
it on the next submit.

**Finding:** libusb cancellation is asynchronous, and a timed-out/cancelled
transfer may have partially or fully reached the device. Retrying a relative
delta or tap can double-apply it. **[libusb contract]**

**Implemented resolution:** only a node's first request 57 may retry, and only
after STALL during the documented registration race. Other errors are latched;
the exact relative component submitted is consumed even when delivery becomes
unknown, preventing an automatic duplicate.

`libusb_submit_transfer()` is a separate boundary: a negative return means the
submission itself failed, and v1.0.30 removes that transfer from its flying
list. No libusb callback follows. The library invokes its own terminal handler
exactly once to recover the pool, marks that synthetic completion as
not-accepted by its negative native status, and retains the state for an
explicit retry. This distinction is **[libusb contract]** plus an observation
of the same audited v1.0.30 `io.c` revision.

### T-04 - Neutral state must complete before cancellation/unregister

**Input text:** asynchronously send neutral, then cancel all in-flight requests.

**Finding:** this sequence can cancel the neutral transfer itself.

**Implemented resolution:** drain an existing node transfer, send and observe
completion of the neutral state while the device is present, then issue request
55. Cancel/drain is a device-teardown safety action, not a way to confirm
neutral delivery. **[libusb contract]**

### T-05 - HID ID reuse after reconnect is not unconditionally safe

**Input text:** reset the ID allocator on every reconnect.

**Finding:** wahoo disconnect moves HID objects to a dead list and schedules a
worker; the worker can register new objects before destroying old ones. Cleanup
is not a synchronous reuse barrier. **[implementation observation]**

**Implemented resolution:** AOA HID IDs increase monotonically for the context
lifetime within a physical-identity domain, including failed registrations and
devices reopened on the same bus/port path. Distinct known physical paths have
independent ID spaces, preserving device isolation. Devices for which libusb
cannot supply a path share one conservative unidentified domain rather than
being guessed distinct. Exhaustion returns an explicit error.

Neither AOA nor the audited wahoo revision provides a host-visible completion
barrier for the deferred HID destruction. Consequently, safety of resetting an
ID after destroying one Context and immediately creating another on the same
still-connected phone is **not established by an official source**. The
library makes no such guarantee; keep the Context across reopen operations or
record target evidence for a stronger quiescence policy. This unresolved
cross-Context boundary is **[implementation observation]** plus
**[project policy]**, not an AOA requirement.

### T-06 - Failed descriptor registration needs cleanup and retirement

**Finding:** after request 54, an incomplete wahoo HID can remain pending until
request 55 or disconnect. Fragment offsets must be exactly ascending; a failed
fragment cannot be retried out of order. **[implementation observation]**

**Implemented resolution:** any post-54 failure triggers best-effort request 55
and retires that ID. Fragments are sent once in ascending offsets.

### T-07 - Mode A before `START` is target-conditional

**Input ambiguity:** "HID needs no new interface" was treated as a universal
guarantee that requests 54-57 work in any current USB mode.

**Finding:** AOA 2.0 states that HID is carried entirely on EP0 and adds no
interface, but does not promise every OEM routes AOA requests before `START`.
At kernel/common commit `0e3db17d01c94263b629b089c51c7d0988308232`,
`f_accessory.c::acc_ctrlrequest` handles request 53 and requests 54-57 as sibling
branches and does not read `start_requested` before the HID branches. At commit
`9b03ed2feb9c4cc3f15d44eda080caabb3a6b843`, `composite_setup` explicitly routes
matched Accessory requests before the function is allocated to a configuration;
`__acc_req_match` includes requests 54-57, and `__acc_setup` again handles them
without a `start_requested` condition. The latter commit explicitly says that
the driver is not eligible for android-mainline or future Android 16+ branches.
The EP0 statement is an **[AOA requirement]**; both exact source readings are
**[implementation observation]**, not a portable guarantee.

**Implemented resolution:** the runtime is Mode A-only and sends requests
54-57 on the selected device's current EP0. It never sends requests 52, 53, or
58 and never waits for re-enumeration. This is **[project policy]** selected for
a smaller startup lifecycle, not proof that every target accepts pre-`START`
HID. `AOAHID_START_ACCESSORY_MODE` remains an ABI tombstone and returns
`AOAHID_ERR_UNSUPPORTED` before USB I/O. Mode A must still be recorded as
target-conditional in the hardware matrix, and every current row remains
**[unverified on hardware]**.

### T-08 - Omitting accessory strings has conflicting behavior

**Finding:** AOA 2.0 says omitting manufacturer/model prevents application
matching and an Accessory interface. The audited wahoo request 51 initializes
those fields to `"Android"`. The former is an **[AOA requirement]**; the latter
is an **[implementation observation]**, not the portable contract.

**Current resolution:** the string discrepancy remains a historical protocol
and implementation observation, but the library no longer sends request 52 or
implements Mode B. The retained public string structure is an ABI tombstone and
is not a startup input. **[project policy]**

### T-09 - USB success does not prove Android input delivery

**Finding:** wahoo ignores the return values from `hid_parse_report()` and
`hid_report_raw_event()`. A successful request 56/57 only proves EP0 acceptance.
**[implementation observation]**

**Implemented resolution:** USB status and Android evidence are kept separate.
No source-only profile is labelled hardware-verified.

### T-10 - 4088 versus 4096 is an official-source conflict

**Input text:** 4088 bytes was stated as a current Linux/Windows libusb hard
limit because 4096 included the setup packet.

**Finding:** libusb 1.0.30 API prose says 4096 total including the eight-byte
setup. The same revision's Linux and WinUSB backends compare the **payload** to
4096, and Microsoft documents a 4 KB WinUSB buffer excluding setup. Official
sources therefore conflict. The prose is a **[libusb contract]**, the backend
checks are **[implementation observation]**, and the Microsoft limit is a
**[Windows requirement]**.

**Implemented resolution:** no compiled 4088 constant. The caller supplies four
separate policies. 4088 can be selected as a conservative cross-version policy;
4096 payload is only a v1.0.30 Linux/WinUSB implementation observation.
**[project policy]**

### T-11 - Request 55 has an official-page comment typo

**Finding:** the AOA 2.0 request table defines request 55 as
`ACCESSORY_UNREGISTER_HID`, while one adjacent code comment says
`ACCESSORY_REGISTER_HID`. The numeric define, surrounding prose, and audited
wahoo switch all agree on unregister. The page evidence is an
**[AOA requirement]**; the wahoo switch is an **[implementation observation]**.

**Implemented resolution:** request 55 is unregister; the conflict is retained
here rather than silently corrected. **[project policy]**

### T-12 - Discovery probing can have target-side effects

**Finding:** the project input requires discovery to classify with request 51.
On wahoo, each request 51 also resets accessory strings/audio/start state. AOA
does not establish that re-probing an already-open session is side-effect-free.

**Resolution:** discovery retains the required official GET_PROTOCOL probe and
documents this implementation observation. Re-running discovery during a live
session remains **[unverified on hardware]** and is not advertised as harmless.

### T-13 - AOA does not define serial-based re-enumeration correlation

**Input text:** guide §28.2 permits correlation using bus/port path, serial,
connection time, and similar identity data.

**Finding:** AOA 1.0 "Attempt to start in accessory mode" requires waiting for
the device to re-introduce itself and then enumerating the new AOA VID/PID. It
does not say that the OEM USB serial survives the transition or that string ID
5 becomes the USB device descriptor's serial. libusb 1.0.30 documents its port
number chain as the path from the root and ties a port number to a physical port
subject to its stated OS/topology caveats. This is **[AOA requirement]** plus
**[libusb contract]**; using the path for correlation is **[project policy]**.

**Current resolution:** the library no longer sends request 53 or correlates a
re-enumerated device. The source conflict remains recorded so removal is not
later mistaken for a missing feature: serial continuity was never established
by AOA. The legacy Mode-B token returns `AOAHID_ERR_UNSUPPORTED` before USB I/O.
**[project policy]**

### T-14 - Payload logging is intentionally absent

**Input text:** `DESIGN.md` lists an `AOAHID_ALLOW_PAYLOAD_LOGGING` build option
and says payload output would additionally require a runtime flag.

**Finding:** no protocol or platform contract requires payload logging. Input
reports can contain keystrokes, credentials, and touch or medical data. A
compile option without a matching public runtime choice would also be a false
capability surface. This is a security/API design question rather than an
official-source conflict.

**Implemented resolution:** the unused build option was removed. Structured
metadata logging is implemented, but report bytes are never passed to a sink.
Adding payload logging remains a separately reviewable API change, not a hidden
compile-time behavior. **[project policy]**

### T-15 - Per-context log suppression has documented exceptions

**Input text:** `DESIGN.md` §7.5 says setting `LIBUSB_LOG_LEVEL_NONE` means
libusb never writes to an unsolicited stream.

**Finding:** libusb 1.0.30 "Library initialization/deinitialization" defines
`LIBUSB_LOG_LEVEL_NONE` as no messages and makes it the default, but the
`LIBUSB_OPTION_LOG_LEVEL` contract also says the option has no effect when
`LIBUSB_DEBUG` was set at initialization or when libusb was built with verbose
debug logging. `libusb_set_log_cb` redirects context messages, but its contract
says an `ENABLE_DEBUG_LOGGING` build never calls the per-context callback; a
system-logging build has separate callback restrictions. Absolute silence is
therefore not guaranteed for every externally supplied libusb build.
**[libusb contract]**

In v1.0.30 commit `87a55632db62c9bdc58cd31d3ccfa673f1bb017f`,
`log_v` calls `log_str` before the per-context handler. `log_str` uses the
process-global handler when one exists, otherwise a configured system facility
or `stderr`. A discard context callback therefore does not suppress that
primary output path when logging was forced on. **[implementation observation]**

**Implemented resolution:** `Runtime` creates its private context with
`libusb_init_context()` and supplies both `LIBUSB_OPTION_LOG_LEVEL =
LIBUSB_LOG_LEVEL_NONE` and a discard `LIBUSB_OPTION_LOG_CB` in the initialization
array. It does not install a process-global callback or mutate default-context
options, so it does not change another libusb user's logging. `NONE` is the
effective suppression under the ordinary contract; the discard callback only
handles the context callback channel and cannot override the documented global,
environment, or build behavior. Compilation requires libusb 1.0.30-or-later
headers and APIs in CMake, installed package metadata, and header/API checks.
Runtime acceptance is a separate T-16 policy: exactly the 1.0 version line with
micro version 30 or later. The pinned vcpkg baseline contains port version
1.0.30. **[project policy]**

### T-16 - A later major or minor version is not an established compatible runtime

**Input text:** the initial runtime check accepted any libusb major greater than
one or minor greater than zero as if a larger numeric version proved ABI and
behavioral compatibility.

**Finding:** libusb 1.0.30 "Miscellaneous" defines `LIBUSB_API_VERSION` for
compile-time API feature detection and `libusb_get_version()` as the running
library's major, minor, micro, nano, and release-candidate values. That page does
not state that a future 1.1 or 2.x runtime is ABI-compatible with the headers and
symbols used here. **[libusb contract]**

**Implemented resolution:** runtime validation accepts exactly the documented
1.0 version line with micro version 30 or later. A future major or minor line is
rejected until its official compatibility contract and target behavior are read
and recorded; numeric ordering alone is not used as evidence. **[project policy]**

### T-17 - AOA strings are validated before any USB side effect

**Input behavior:** string length was checked immediately before each request
52. A late invalid string could therefore be discovered only after earlier
identification strings had already changed target state. Current-USB-mode also
silently ignored strings and the audio option.

**Finding:** AOA 1.0 "Attempt to start in accessory mode" defines request 52 as
UTF-8, zero-terminated identification strings with a maximum size of 256 bytes.
AOA 2.0 "Audio support" requires request 58 before request 53. Neither page
defines partial string programming as a transaction or makes accessory strings
meaningful when no mode switch is requested. **[AOA requirement]**

**Current resolution:** removing Mode B removes every runtime path that reads or
sends these values, so partial request-52 programming is no longer possible.
The identification-string encoding, size, and audio-before-START facts remain
**[AOA requirement]** records for reviewing the removed behavior. The public
string and audio members remain unchanged in the ABI layout but do not cause
requests 52 or 58. **[project policy]**

### T-18 - Scan Time is elapsed time, not a frame counter

**Input behavior:** each new frame incremented Scan Time by one, which asserted
that every frame began exactly 100 microseconds after the previous frame.

**Finding:** HUT 1.7 §16.5 defines Scan Time as a relative timestamp whose base
is the first frame after inactivity and whose changes reflect digitizer scan
frequency. It is not an arbitrary frame sequence number. **[HUT 1.7 definition]**

**Implemented resolution:** when the caller enables the existing 100-microsecond
profile with Logical Minimum `0`, the first activity frame is zero. Later
frames encode `std::chrono::steady_clock` elapsed time in 100-microsecond units
modulo `logical_maximum + 1`. Every packet of one multi-packet frame keeps one
value. When successful completion leaves both contacts and touchpad buttons
inactive, it resets the inactivity epoch. The chosen host clock, modulo,
completion boundary, and reset rule are **[project policy]** implementing the
HUT semantic.

### T-19 - A Resolution Multiplier is not static coordinate resolution

**Input text:** `DESIGN.md` §8.4 and the supplied implementation prompt require
a caller-selected mouse Resolution Multiplier declaration as though it were
ordinary Input metadata.

**Finding:** HUT 1.7 §4.3.1 defines Generic Desktop Resolution Multiplier
Usage `0x48` as a control in the same Logical Collection as the affected
control. Its normative example declares the multiplier as a Feature Main item;
the host sets that Feature value to select a dynamic multiplier. HID 1.11
§6.2.2.7 separately defines static resolution from Logical Minimum/Maximum,
Physical Minimum/Maximum, Unit Exponent, and Unit. These are distinct
mechanisms. **[HUT 1.7 definition]** and **[HID 1.11 requirement]**

**Implemented resolution:** ordinary physical range and Unit metadata are
caller-selectable for integer fields. The dynamic Resolution Multiplier is not
exposed: AOA request 57 carries accessory-to-Android Input reports and provides
no host-to-accessory Feature transaction through which Android could set the
multiplier. A declarative control that cannot be serviced would advertise a
false capability. **[AOA requirement]** plus **[project policy]**

### T-20 - Keyboard and Consumer reports remain separate nodes

**Input conflict:** `DESIGN.md` §8.3 calls sharing a Consumer report with a
keyboard node configurable, while `DESIGN.md` §3.3 defines one node as one AOA
HID ID, one descriptor, one report layout, and one profile state machine. The
guide §24.3 presents Keyboard and Consumer Control as separate AOA HID IDs and
recommends one functional family per ID for Android classification and
diagnostics.

**Resolution:** the public API follows the object model and guide: Keyboard and
Consumer Control are separate immutable specs and separate nodes. A caller can
register both on one Android device. No official AOA, HID, or Android source
establishes a compatibility advantage for combining them, so the library does
not create a composite state machine by policy. **[project policy]**

### T-21 - Asynchronous overflow is not a generic I/O result

**[libusb contract]** The libusb 1.0.30 asynchronous-transfer status table
defines `LIBUSB_TRANSFER_OVERFLOW` as the device sending more data than was
requested. It is distinct from `LIBUSB_TRANSFER_ERROR`; even a
`LIBUSB_TRANSFER_COMPLETED` result does not by itself prove that the entire
requested length transferred.

**Implemented resolution:** asynchronous request-57 completion maps
`LIBUSB_TRANSFER_OVERFLOW` to `AOAHID_ERR_OVERFLOW`. A completed transfer whose
payload-relative `actual_length` differs from the requested report length maps
to `AOAHID_ERR_SHORT_TRANSFER`. The native transfer status and requested report
length remain in the retained diagnostic; libusb's asynchronous
`actual_length` is not exposed by the public detail record. This mapping is a
**[project policy]** preserving the official libusb distinction. Retrieved
2026-08-27.

### T-22 - A short request-51 reply is a failed AOA capability probe

**[AOA requirement]** The official AOA 1.0 "Attempt to start in accessory
mode" section defines request 51 data as a 16-bit little-endian protocol
version, and a supported device returns a nonzero version. A zero- or one-byte
successful control transfer therefore does not contain the required response.

**[libusb contract]** A synchronous control-transfer success value is the
number of payload bytes transferred. A short success is not a negative libusb
error.

**Implemented resolution:** a request-51 short reply returns
`AOAHID_ERR_NOT_AOA`, the same capability result as a probe STALL, while the
diagnostic retains request 51, the observed positive byte count as native
status, and expected length 2. This does not relabel short payload transfers for
requests 56 and 57; those continue to return `AOAHID_ERR_SHORT_TRANSFER`. The
removed runtime no longer sends requests 52 or 53, while requests 54 and 55
have no data stage under the AOA 2.0 definition.
Retrieved 2026-08-27.

### T-23 - Public zero-value tuning is not libusb's zero-timeout contract

**Input conflict:** the earlier option model rejected every zero-valued tuning
field, while the requested convenience change proposed fixed values and asked
for USB/libusb support for them. Treating any chosen millisecond value as a
libusb recommendation would overstate the primary source.

**[libusb contract]** `libusb_control_transfer` and
`libusb_transfer::timeout` define timeout units as milliseconds and state that
zero means unlimited/no timeout. The official API does not recommend a single
positive timeout for all devices, host controllers, operating systems, or AOA
vendor requests.

**USB source distinction:** USB 2.0 Revision 2.0 and USB 3.2 Revision 1.1
§§9.2.6.1 and 9.2.6.4 define device request-processing timing ceilings for
specified stages. They do not define a libusb host-side latency prediction or
make 500 ms a universal timeout for AOA requests 51 and 54-57.

**Implemented resolution:** at the public `aoahid_device_options` boundary,
zero selects fixed bounded values: 500 ms control timeout, 500 ms report-send
timeout, 64-byte descriptor fragments, 8 transfer-pool slots, 1024-byte maximum
report buffers, 1000 ms close drain, 20 total first-report attempts, and 1000
microseconds between retry attempts. `aoahid_node_options` zero/zero selects no
reservation. These values are **[project policy]**, reused from established
repository examples/tests where applicable, not USB/AOA requirements or
libusb recommendations.

The implementation copies the caller's structure, normalizes the copy before
validation, and leaves the caller's bytes unchanged. Explicit nonzero tuning
values retain their prior meaning. The affected zero values previously failed
validation, so the public API did not previously expose libusb's unlimited
timeout through these fields. A timeout is a failure deadline passed to the
backend: successful I/O completes immediately when reported and does not wait
out the remaining budget. Twenty first-report attempts include the initial
submission and permit at most 19 backoff intervals.

No product or exact-target contract is inferred. Event mode, startup mode,
interface/validation policy, descriptor/EP0/control-buffer policies, the five
target HID-parser policies, and every profile Usage, range, width, count, and
Report ID remain mandatory. No OS-specific latency or physical Android behavior
was established; those claims remain **[unverified on hardware]**. Retrieved
2026-08-27.

## HID Usage-semantic discrepancy

### H-01 - Consumer and System Usages are not uniformly Array selectors

**Input text:** `DESIGN.md` §8.5 and the supplied implementation prompt describe
Consumer/System state as one Usage selector that returns to zero. The initial
implementation consequently accepted any nonzero Usage and emitted one aligned
Array through the numerically greatest allow-listed ID.

**Finding:** **[HUT 1.7 definition]** Table 3.2 gives different field forms and
operations to OOC, MC, OSC, RTC, and LC. Section 3.4.2.1 permits a Selector to
be an Array or a Variable-bit bitmap, but it does not classify every Usage as a
Selector. The Consumer table identifies, among others, Play `0x00b0` as OOC,
Play/Pause `0x00cd` as OSC, Volume `0x00e0` as LC, Volume Increment `0x00e9`
as RTC, and AC New `0x0201` as Sel. Generic Desktop Table 4.1 similarly assigns
different types to System Power Down `0x81`, Menu Right `0x8a`, Function Shift
`0x97`, and Function Shift Lock `0x98`. Section 3.4.4 also says the Usage Type
is guidance and that the actual Main-item attributes determine interpretation.

**Implemented resolution:** the options now pair every allowed Usage with an
explicit `aoahid_usage_semantic`. Each sparse Usage becomes one 1-bit Variable
field with the exact Relative/Absolute and Preferred/No-Preferred form selected
by the caller. The library does not infer a HUT type from an arbitrary numeric
Usage. The camera factory is deliberately fixed to the two OSC entries in HUT
Table 35.1. LC, DV, NAry, and two-direction OOC return
`AOAHID_ERR_UNSUPPORTED` because the existing press/release API cannot carry
their numeric, collection, or direction state. This is **[project policy]**
grounded in the cited **[HUT 1.7 definition]**, not an Android compatibility
claim. All target mappings remain **[unverified on hardware]**.

### H-02 - Linux parser capacities are target policies, not HID requirements

**Input behavior:** raw-spec creation unconditionally rejected Report Count
above 12288 and the 257th Input Main item by compiling values from one Linux
revision into the reusable specification object.

**Finding:** at common-kernel commit
`35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, `HID_MAX_USAGES` is 12288 and
`HID_MAX_FIELDS` is 256. `hid_parser_global` applies the former to Report Count;
`hid_parser_local` bounds its local Usage table; and `hid_register_field`
applies the latter per report type and Report ID. These are
**[implementation observation]**, not HID 1.11 requirements and not facts
about every Android target.

**Implemented resolution:** descriptor construction records the greatest
declared Usage requirement and the greatest registered-field count for one
report. It does not apply the audited numeric values. Device options require
the caller's target-kernel policies, and node registration compares the
immutable descriptor requirements with those policies before request 54. The
documentation lists 12288 and 256 only as values for the named audited commit.
**[project policy]**

### H-03 - Zero-initialized parser state is not a declaration

**Input omission:** `DESIGN.md` §6.3 did not include presence checks for every
required control item. An intermediate raw validator therefore accepted a
non-Constant Input whose Logical Minimum or Logical Maximum was absent, because
its internal state began at zero. It could also accept an Input with no local
Usage after the Application Collection had consumed and cleared that Usage.

**Finding:** **[HID 1.11 requirement]** §6.2.2.2 explicitly lists Usage,
Usage Page, Logical Minimum, Logical Maximum, Report Size, and Report Count as
required items describing control data. Section 6.2.2.6 separately requires a
Usage associated with every Collection. The specification's parser model
clears Local state after a Main item.

**Implemented resolution:** the raw validator records whether the required
Global declarations occurred and requires a current local Usage on every
non-Constant Input. It also requires an associated local Usage on every
Collection. Constant padding is not mislabeled as control data and may omit a
new Usage, consistent with the specification examples. Retrieved 2026-08-27.

### H-04 - Target parser tolerance does not waive Delimiter exclusions

**Input omission:** the supplied documents do not state the HID Delimiter
restrictions. An intermediate raw validator followed the exact target parser's
branch counting and accepted a balanced alternative set before any Main item.

**Finding:** **[HID 1.11 requirement]** §6.2.2.8 says Delimiters bracket
alternative Usages and expressly forbids them for Usages defining Application
Collections or Array items. The audited Linux `hid_parser_main` clears Local
state after a Main item, even when a delimiter remains open; that is an
**[implementation observation]**, not permission to emit a nonconforming
descriptor.

**Implemented resolution:** raw validation requires the set to close before
the consuming Main item, rejects delimiter state on Application Collections
and data Array Inputs, and continues to accept balanced aliases on Variable
fields. Retrieved 2026-08-27.

### H-05 - Generated Collection balance was asserted but not proved

**Input text:** `DESIGN.md` §6.3 requires the static validator to reject
unbalanced Collection nesting. The intermediate generated-profile path built a
layout and descriptor together but did not itself count open Collections.

**Finding:** **[HID 1.11 requirement]** §§6.2.2.4 and 6.2.2.6 define the paired
Collection and End Collection Main items. Relying on the current emitters being
hand-reviewed did not implement the supplied design's stated proof.

**Implemented resolution:** the builder rejects unmatched closes and an open
Collection remaining at `finish()`. The check runs in both descriptor passes,
with regression tests for unmatched, unfinished, and balanced cases. Retrieved
2026-08-27.

### H-06 - Defined and reserved Global tags must not be conflated

**Input omission:** the supplied documents require checking the Global state
stack but do not enumerate the complete Global tag set. An intermediate
correction rejected every unhandled Global tag and therefore also rejected the
defined Physical Minimum, Physical Maximum, Unit Exponent, and Unit items.

**[HID 1.11 requirement]** Section 6.2.2.7 defines Global tags 0 through 11
and reserves tags 12 through 15. **[implementation observation]**
`hid_parser_global` in commit
`35556bed836f8dc07ac55f69c8d17dce3e7f0e25` accepts all twelve defined tags
and returns failure from its default branch for the four unknown tags.

**Implemented resolution:** strict raw validation explicitly handles tags 0
through 11 and rejects reserved Global tags 12 through 15. HID 1.11 §§6.2.2.2,
6.2.2.4, and 6.2.2.8 also mark short-item type 3, Main tags 13-15, and Local tag
6 plus tags 11-15 reserved, so the strict subset rejects them as **[project
policy]** while accepting the defined Designator and String Local tags.
The audited target ignores those latter categories; its tolerance is recorded
but not copied into the descriptor contract. Retrieved 2026-08-27.

### H-07 - Global stack depth is a target policy

**Input omission:** neither supplied document gives the selected target's
Global Push depth. **[implementation observation]** the exact
revision defines `HID_GLOBAL_STACK_SIZE` as 4 in `include/linux/hid.h`, and
`hid_parser_global` rejects the fifth nested Push.

**Implemented resolution:** descriptor construction records the greatest
balanced Global Push depth without compiling 4 into the reusable Spec. The
device requires `linux_hid_global_stack_depth_policy`; node registration
compares it before request 54. The example uses 4 only for the named revision.
**[project policy]** Retrieved 2026-08-27.

### H-08 - The selected parser limits report data to 65,528 bits

**Input text:** the guide correctly says AOA has no universal report-length
limit, but that does not answer the selected Linux parser question.

**[implementation observation]** `hid_add_field` rejects a
report whose accumulated data size exceeds
`(HID_MAX_BUFFER_SIZE - 1) << 3`; the same revision defines
`HID_MAX_BUFFER_SIZE` as 8192. The resulting bound is 65,528 data bits and
reserves one byte for a possible Report ID. It is not an AOA, USB HID, EP0, or
host-control-buffer requirement.

**Implemented resolution:** every Spec records the greatest accumulated data
bits for one `(report type, Report ID)`. The device requires
`linux_hid_report_data_bits_policy`, checked before request 54 and independently
from all byte-transport policies. The example value 65,528 is labelled for the
exact revision only. **[project policy]** Retrieved 2026-08-27.

### H-09 - An overwritten Report Size declaration can still fail parsing

**Input omission:** supplied validation rules constrain fields actually used by
a Main item but do not address a Global declaration that is overwritten first.

**[implementation observation]** `hid_parser_global` rejects
Report Size above 256 immediately. Thus `Report Size (257)`, followed by
`Report Size (8)` and an otherwise valid Input, fails before the overwrite.
This 256 value is not the HID 1.11 field-width rule.

**Implemented resolution:** Specs record the greatest Report Size declaration,
including overwritten declarations. The caller supplies
`linux_hid_report_size_bits_policy`; the example uses 256 for this exact
revision, while strict used fields remain limited by HID 1.11 §8.4.
**[project policy]** Retrieved 2026-08-27.

### H-10 - Local Usage capacity must be observed when state changes

**Implementation discrepancy found during final audit:** an intermediate raw
validator sampled Local Usage count only at a Main item. A complete valid
report followed by 12,289 zero-size Usage items therefore understated the
requirement as one.

**[implementation observation]** `hid_parser_local` calls
`hid_add_usage` for each accepted Usage immediately; `hid_add_usage` fails once
`usage_index` reaches `HID_MAX_USAGES`, even if no later Main item consumes the
Local state.

**Implemented resolution:** the target-neutral maximum is updated after every
accepted Usage and Usage range as well as every Report Count and Main item.
The caller's Usage policy is still applied only before request 54. Retrieved
2026-08-27.

### H-11 - Raw extended `Usage Maximum = UINT32_MAX` is conservatively excluded

**Input omission:** no supplied document covers unsigned wraparound in the
selected parser's inclusive Usage-range loop.

**[implementation observation]** in `hid_parser_local`, a four-byte Maximum
whose raw `item_udata()` value is `0xffffffff` can leave the selected inclusive
loop to increment its unsigned iterator from `UINT32_MAX` to zero. The
capacity branch can instead rewrite the bound first, so wrap is state-dependent,
not inevitable. A short Maximum `0xffff` on current page `0xffff` has a raw
loop bound of 65,535 and is a different case even though its completed Usage is
`0xffffffff`.

**Implemented resolution:** the validated raw subset returns
`AOAHID_ERR_UNSUPPORTED` whenever the raw four-byte Usage Maximum item equals
`UINT32_MAX`. Other large raw ranges are summarized with 64-bit checked
arithmetic and rejected later when they exceed the caller's target Usage
policy. The blanket exclusion is deliberately conservative **[project
policy]**, not a claim that the selected kernel always wraps. Retrieved
2026-08-27.

### H-12 - Every tag-15 prefix is long in the selected parser

**Input text:** the raw subset rejects HID long items, but checking only the
canonical `0xfe` prefix is incomplete for the selected implementation.

**[implementation observation]** `fetch_item` tests the
four-bit tag for `HID_ITEM_TAG_LONG`; any prefix whose high nibble is `0xf` is
therefore parsed as long. `hid_open_report` then rejects every non-short format.

**Implemented resolution:** raw validation rejects all tag-15 prefixes before
short-item size decoding, with regression coverage for a non-`0xfe` prefix.
Retrieved 2026-08-27.

### H-13 - An open Local delimiter at EOF is a parse failure

**Input omission:** the earlier delimiter correction checked state only when a
Main item consumed it. **[HID 1.11 requirement]** §6.2.2.8 defines paired open
and close delimiters around alternatives. **[implementation observation]**
`hid_open_report` separately rejects nonzero
`parser->local.delimiter_depth` at end of descriptor.

**Implemented resolution:** final raw balance validation includes Local
delimiter depth as well as Collection and Global-stack state. Retrieved
2026-08-27.

### H-14 - Logical Maximum signedness follows the Minimum

**Implementation discrepancy found during final audit:** one helper treated a
negative `int32_t` Maximum bit pattern as proof of a signed field and skipped
logical-order checks for Constant Input padding.

**[HID 1.11 requirement]** §6.2.2.7 interprets Logical Maximum as signed only
when Logical Minimum is negative. **[implementation observation]**
`hid_parser_global` follows that rule and `hid_add_field` checks
the range before its no-Usage padding return. Consequently Minimum 0 plus a
four-byte Maximum `0xffffffff` is the unsigned value 4,294,967,295, while an
invalid Constant range still fails.

**Implemented resolution:** the raw path preserves the 32-bit Maximum pattern,
uses Minimum-selected signedness for order and width, and checks order for every
Input. The generated API retains its signed `int32_t` option contract rather
than reinterpreting a caller's negative number. Retrieved 2026-08-27.

### H-15 - Units add mandatory descriptor metadata

**Input omission:** the supplied raw-validation list does not require the
metadata associated with a declared Unit.

**[HUT 1.7 definition]** §3.3 requires Logical Minimum, Logical Maximum,
Physical Minimum, Physical Maximum, and Unit Exponent when a non-None Unit is
declared for a data Main item. **[HID 1.11 requirement]** §6.2.2.7 defines
these as Global state and Unit Exponent as a signed four-bit code.
**[implementation observation]** the exact target does not
track declaration presence and also accepts legacy full-byte sign extension.

**Implemented resolution:** raw Global state tracks all five declarations
through Push/Pop. A nonzero Unit requires them on every Input, including a
Constant item; Unit zero removes the requirement. Exponent encodings are
limited to numeric codes `0x0` through `0xf`, so target-tolerated `0xfc` is
rejected as nonconforming. Retrieved 2026-08-27.

### H-16 - Usage-range page composition differs from the selected parser's raw loop

**[HID 1.11 requirement]** §6.2.2.8 and **[HUT 1.7 definition]** §3.1
interpret one- and two-byte Usage, Usage Minimum, and Usage Maximum values as
IDs on the current Usage Page, and four-byte values as extended Usages. HID
1.11 additionally requires an extended Minimum to have an extended Maximum.
Neither source defines a default Minimum or expressly forbids a Maximum without
a preceding Minimum.

**[implementation observation]** In common-kernel commit
`35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, `hid_parser_local` calculates its
inclusive range from raw `item_udata()` values before page composition, passes
the Maximum item's size to `hid_add_usage`, and `complete_usage` appends the
current page only for item sizes through two bytes. Consequently short page
`0xffff`, Minimum `0xfffe`, Maximum `0xffff` consumes two entries. Short
Minimum `1` plus extended Maximum `0x00010002` has an untruncated raw-domain
demand of 65,538 entries; the target's capacity branch clips the actual table
population to `HID_MAX_USAGES` and ignores the excess. After Local state is
zeroed, a standalone short Maximum `3` expands the exact target's raw `0..3`
interval; this is an implementation observation, not a specification default.
An extended Minimum followed by a short Maximum violates the HID rule even
though the selected parser has its own arithmetic behavior.

**Implemented resolution:** validation records 2 and 65,538 respectively,
rejects the mismatched extended-Minimum/short-Maximum form as malformed, and
applies the caller's exact-target Usage-capacity policy before request 54. The
target model is computed with checked arithmetic rather than expanding every
entry. Retrieved 2026-08-27.

### H-17 - Pen Invert must precede the In Range tool decision on the legacy target

**Input behavior:** an eraser-enabled generated descriptor placed Tip Switch
and In Range before Invert. That byte layout was internally consistent but did
not account for revision-specific event-dispatch order.

**Finding:** at common-kernel commit
`35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, `hid_report_raw_event` visits
`report->field[]` in descriptor order and `hid_input_field` visits Variable
usages in field order. `hidinput_hid_event` updates `HID_QUIRK_INVERT` on the
Invert field, while its In Range branch reads that state to choose
`BTN_TOOL_PEN` or `BTN_TOOL_RUBBER`. Android 17 ACK commit
`f67745b7d96806e622db56f4be97af16d6e99850` instead supplies an explicit
`hidinput_usages_priorities` order of Eraser, Invert, Tip Switch, Tip Pressure,
then In Range; `hid_report_process_ordering` applies it. Both are exact-source
**[implementation observation]** facts, retrieved 2026-08-27, not HID or
Android-wide requirements.

**Implemented resolution:** when eraser-end selection is enabled, the
descriptor emits Variable fields Invert, Tip Switch, then In Range. The order
preserves the legacy target's state dependency and agrees with the pinned
Android 17 priority relation. Tool switching still uses an automatic departure
before re-entry. This ordering and state machine are **[project policy]**; no
pen profile is promoted from **[unverified on hardware]**.

## Android input discrepancies

### A-01 - 16 contacts is not a public `MotionEvent` limit

**Input text:** 16 was labelled an application/API policy.

**Finding:** public `getPointerCount()` only promises at least one pointer. AOSP
main and Android 16 define `MAX_POINTERS=16`; `syncTouch` discards excess input,
while the same implementation has 32 slots. **[implementation observation]**

**Implemented resolution:** generated touch profiles reject more than 16 as a
current-AOSP portability policy. Documentation does not call 16 a public API or
USB limit.

### A-02 - Current IDC priority is association, then IDC, then inference

**Input text:** "IDC has highest priority."

**Finding:** current main and Android 16 `computeDeviceType` explicitly give a
device-type association precedence over IDC, then inferred properties. Android
8 had IDC over inference because the newer association layer was absent.
**[implementation observation]**

**Resolution:** documentation states the revisioned orders separately; no one
order is generalized across Android releases. **[project policy]**

### A-03 - Versioned `FFFF/FFFF` configuration is skipped for AOA version 0

**Input text:** a `Vendor_ffff_Product_ffff_Version_0000` IDC/KL candidate might
be attempted for an AOA logical HID.

**Finding:** main, Android 16, 10, and 8 only attempt a versioned filename when
`identifier.version != 0`. The documented AOA logical HID identifier has version
zero, so lookup goes to unversioned VID/PID and then canonical name.
**[implementation observation]**

**Resolution:** the versioned-zero candidate is removed from implementation
guidance and recorded as an input error here. **[project policy]**

### A-04 - Platform version 37.1 is Android 17 MR1

**Input text:** version 37.1 was grouped with Android 16/API level 37.

**Finding:** `Build.VERSION_CODES_FULL.CINNAMON_BUN_1` identifies Android 17
minor release 1, platform version 37.1 (`3700001`). **[Android API contract]**

**Resolution:** version 37.1 is a separate Android 17 MR1 row. Public
`HidDevice` documentation is phrased narrowly: on the retrieval date its public
method surface had no continuous Input-report stream/callback. **[Android API
contract]** It is not used to claim that no private or future implementation can
receive Input. **[project policy]**

### A-05 - Android 17 CDD Hat value 1 conflicts with its clockwise rule

**Input ambiguity:** the generic gamepad factory previously treated several
Gamepad forms as portable candidates without tying that status to the complete
Android 17 controller contract.

**Finding:** Android 17 CDD §7.2.6.1 names the Generic Desktop / Game Pad
Application Collection, maps Button Page Usage IDs `1`, `2`, `4`, and `5` to A,
B, X, and Y, and fixes the Hat Switch at Logical `0..7`, Physical `0..315`, Unit
Degrees, and four bits. The subsection says the Hat values increase clockwise
from Up, but its value-1 prose says Up and Left. At Android 17 ACK commit
`f67745b7d96806e622db56f4be97af16d6e99850`, `hid_hat_to_axis` maps the
direction index produced by wire value `1` to `(1,-1)`, or Up-right.
**[implementation observation]** Retrieved 2026-08-27.

**Implemented resolution:** portable-candidate status is limited to the Game
Pad Application Collection with the canonical Hat and a contiguous Button Page
range beginning at `1` with at least `5` fields, so A/B/X/Y are all present.
The contiguity gate and the choice of value `1` as Up-right follow the generated
descriptor form, the CDD's clockwise rule, and the pinned ACK mapping.
Raw-D-pad, no-D-pad, Joystick, and other button-range forms remain conditional.
These choices are **[project policy]** and every controller profile remains
**[unverified on hardware]**.

## Public API and design discrepancies

### D-01 - The public C++ namespace surface is narrower than the design sketch

**Input text:** `DESIGN.md` §1 names public `aoa`, `aoa::hid`, and
`aoa::profiles` namespaces.

**Implemented resolution:** the thin installed wrapper exposes its public
handles and typed node references directly under `aoa`. HID descriptor
construction/parsing remains a private implementation detail, and there is no
public `aoa::profiles` namespace. The preserved design input is not rewritten;
this narrower public surface is recorded here for individual review.
**[project policy]**

### D-02 - Raw descriptors use an immutable Spec, not `aoahid_node_open_raw`

**Input text:** `DESIGN.md` §6.4 names an `aoahid_node_open_raw` operation.

**Implemented resolution:** `aoahid_spec_create_raw` validates and copies the
raw descriptor and accepted Input-report table into an immutable, reusable Spec.
The ordinary `aoahid_node_open` operation then registers that Spec on a Device.
No undocumented `aoahid_node_open_raw` symbol is claimed. **[project policy]**

### D-03 - Mutable tested-target arrays are not part of the stable manifest

**Input conflict:** `AOA_HID_GUIDE.md` §24.1 lists
`tested_android_versions` and `tested_devices`, while the manifest contract in
`DESIGN.md` does not define those arrays.

**Implemented resolution:** the stable ABI manifest exposes static descriptor,
profile, transport-capability, and evidence-conscious status fields only.
Mutable target evidence belongs in `TARGET_MATRIX.md`, where every result can
name its device, build, host/backend, logs, and workflow artifact. No hardware
evidence exists yet, so tested-target arrays were not silently invented or
populated. **[project policy]** **[unverified on hardware]**

## Build and release discrepancies

### C-01 - A tag-triggered workflow cannot reject creation of the tag

**Input text:** the release workflow "refuses the tag."

**Finding:** a `push.tags` workflow runs after the tag push event. It cannot
undo creation of that Git reference. **[GitHub Actions contract]**

**Implemented resolution:** every validation/build/test/architecture/SBOM gate
must succeed before the publish job runs. A failure creates no GitHub Release or
assets. The publish job compares the exact unpeeled remote tag-reference SHA to
the fully fetched local tag object, recursively peels the local tag to the
already validated source commit, and then uses the REST release ID lifecycle
described in C-09. It does not claim to reject or delete the pushed tag.

### C-02 - Runner and action versions are time-dependent

**Finding:** as retrieved, the four native runners are `ubuntu-22.04`,
`ubuntu-22.04-arm`, `windows-2025-vs2026`, and
`windows-11-vs2026-arm`.
**[GitHub Actions contract]** VS 18/ARM64 requires CMake 4.2 or newer for the
Visual Studio 18 2026 generator. **[CMake contract]** Runner images can change.
**[GitHub Actions contract]**

**Implemented resolution:** workflow labels and immutable action SHAs are
pinned in the repository, tool/image versions are printed into build logs, and
binary machine type is inspected before upload. These are current CI choices,
not permanent platform requirements.

### C-03 - Exceptions are contained, not disabled build-wide

**Input text:** `DESIGN.md` §7.3/§13.1 and the supplied implementation prompt
require exceptions to be disabled for the entire build while also requiring
dynamically growing STL-owned object collections and a C ABI that reports
allocation failures instead of terminating.

**Resolution:** RTTI remains disabled. C++ exceptions are enabled only so cold
construction and container-growth failures can unwind owned resources. Every
exported C function catches allocation and foreign exceptions and translates
them to its documented error or failure sentinel; no exception crosses the ABI.
The submit, serialization, completion, pool, and state-mutation paths remain
non-allocating and `noexcept` below the C boundary. This is **[project policy]**
chosen to satisfy the public error and lifetime contracts rather than a
protocol or platform requirement. Sanitizer link runtimes propagate through
both shared and static CMake target interfaces so C-only test/consumer
executables start the required runtime before instrumented code.

### C-04 - Literal zero configuration branches conflicts with the dispatch ban

**Input conflict:** Prompt 0 says the send path has no branch on
configuration. `DESIGN.md` §5.2 narrows that to no hot-path event-mode branch,
while §7.3 also prohibits dynamic dispatch. Optional scan time, report
validation, profile serialization, and caller-poll versus internal-thread
synchronization have different behavior. Selecting those behaviors requires a
conditional, indirect dispatch, duplicated entry points, or unconditional work
that changes semantics.

**Implemented resolution:** immutable configuration is represented by
mode-fixed nullable synchronization pointers and small predictable checks.
Caller-poll mode performs no internal-mode lock acquisition. The send path also
contains explicit immutable checks for scan time, profile/layout behavior,
report validation, and opportunistic polling. The implementation therefore
does **not** claim literal zero configuration branches. No external protocol or
platform source resolves this internal input conflict. **[project policy]**

### C-05 - Output/Feature absence conflicts with raw acknowledgement fields

**Input conflict:** Prompt 0 rule 6 and Review Prompt 12 require Output and
Feature support to be absent, while `DESIGN.md` §6.4 and Phase 6 require raw
options named `requires_output` and `requires_feature_response`, with creation
refused unless both are zero.

**Implemented resolution:** the ABI follows the latter explicit structure
contract. These two fields are rejection acknowledgements only: any nonzero
value fails raw-profile creation. Generated profiles expose no Output descriptor
and no Output or Feature transport operation. A generated descriptor may contain
a declarative Constant Feature item such as Contact Count Maximum; that metadata
does not create a Feature-response path, and
`feature_transport_supported` remains zero. Official AOA establishes the
Input-report request path **[AOA requirement]**, and the audited wahoo path is an
**[implementation observation]**; neither selects the public API shape.
**[project policy]**

### C-06 - One pkg-config file cannot describe both installed variants

**Prior state:** installed metadata used a build-time absolute prefix, made
libusb a private requirement even for a shared-only distribution, omitted the
C++ runtime from static C linkage, and described a combined install with one
`-laoahid` choice.

**Finding:** `pcfiledir` is the installed `.pc` location. The official guide
separates private static requirements/libraries from public linkage, while
dependency `Cflags` are traversed through `Requires.private`. A shared C ABI
consumer does not need libusb headers; a static consumer needs libusb and the
C++ runtime used by the archive. **[pkg-config contract]** The compiler runtime
name is reported by `CMAKE_CXX_IMPLICIT_LINK_LIBRARIES`. **[CMake contract]**

**Implemented resolution:** every `.pc` uses
`prefix=${pcfiledir}/../..`. Shared metadata has neither `Requires.private` nor
`Libs.private`. Static metadata has exact
`Requires.private: libusb-1.0 >= 1.0.30` and a compiler-derived `-lstdc++` or
`-lc++`. Every static release bundles a relocated `libusb-1.0.pc`; shared
releases do not require it. A combined install emits shared-primary
`aoahid.pc` and distinct `aoahid-static.pc`, with `libaoahid_static` as the
combined archive name. Relocated package-only pure-C consumers are compiled
from pkg-config output in CI. Test-only builds that embed the fake USB backend
do not advertise a nonexistent private libusb dependency; real static builds
do. **[project policy]**

### C-07 - A combined CMake export forced a shared consumer to find libusb

**Prior state:** one installed targets export contained both the shared and
static targets. Loading the package to use only the shared target also loaded
the static target's public libusb dependency.

**Finding:** CMake config packages define component meaning, and an unmet
required component makes the package not found. Transitive target link
dependencies are supplied through `INTERFACE_LINK_LIBRARIES`.
**[CMake contract]**

**Implemented resolution:** shared and static targets have separate export
files. No component selects shared when present, otherwise static;
`COMPONENTS shared` loads only shared; `COMPONENTS static` resolves libusb and
loads only static; both may be requested. Required unknown or unavailable
components fail. The explicit installed static name is invariantly
`aoahid::aoahid_static`; a static-only package additionally aliases
`aoahid::aoahid` to it for default-name compatibility. Static targets propagate
a compiler-derived C++ runtime as a link-only interface so a
`project(... LANGUAGES C)` consumer links with `cc`.

### C-26 - Static installed target name differed by package composition

**Prior state:** the static target exported as `aoahid::aoahid_static` when
shared and static libraries were built together, but attempted to export as
`aoahid::aoahid` for a static-only package. Windows x64 and ARM64 release jobs
successfully built and ran the shared installed consumer, then the static-only
consumer reported that its requested `aoahid::aoahid` target did not exist.

**[specified CI implementation observation]** Both hosted MSVC jobs reached
the installed-package smoke test after the DLL, import library, static archive,
architecture, and public-symbol checks had passed. The failure was therefore
in the CMake consumer target contract, not compilation, ABI exports, AOA, USB,
HID, Android, libusb runtime behavior, latency, or hardware operation.

**Implemented resolution:** `aoahid_static` now exports under the invariant
name `aoahid::aoahid_static` in every package composition. When a static-only
package is loaded, its config also creates `aoahid::aoahid` as an alias, so the
documented default target remains source compatible. Linux and Windows release
smoke tests explicitly request `aoahid::aoahid_static` for the static archive,
and repository tests enforce both the invariant export and compatibility alias.
Component availability additionally requires the build contract embedded in
that installed config to agree with the matching export file. The Windows job
checks both build caches and both staging trees before consumer configuration,
so a stale or cross-contaminated export cannot make a static-only package claim
the shared component or vice versa. A cross-platform CTest also injects a stale
shared export into a static-only staged config and requires the shared consumer
configuration to fail while the explicit static consumer remains accepted.
**[repository observation; project policy]** Retrieved 2026-08-27.

### C-08 - The build fallback accepted obsolete libusb until compilation

**Prior state:** the non-pkg-config `find_path`/`find_library` fallback accepted
a header reporting `LIBUSB_API_VERSION 0x01000100`; the source-level API guard
failed only later during compilation.

**Finding:** libusb 1.0.30 defines `LIBUSB_API_VERSION` as `0x0100010C`.
**[libusb contract]**

**Implemented resolution:** build-tree and installed-package fallbacks parse
the discovered header at configure time and reject an API value below
`0x0100010C`. A negative CMake test supplies an obsolete header and requires
the documented configure error. The runtime version check remains separate.

### C-09 - Tag-based release creation needs exact ownership and response-loss recovery

**Prior state:** tag-name-based create/delete cleanup could delete a draft
created by another run, and response loss after create or publish could strand
an unidentified release. Checks also treated the release response's
`target_commitish` as the tagged commit.

**Finding:** REST release operations have a numeric release ID; drafts can be
updated/deleted. `target_commitish` is unused/ignored when the tag already
exists, so its response value is not evidence of the tag target. Upload/list
responses expose asset name, state, size, and a required-but-nullable SHA-256
digest field; the returned `upload_url` is the release-specific upload
hypermedia relation. `gh api
--paginate --slurp` wraps every object response page in an outer array, and the
workflow-runs endpoint returns an object containing `workflow_runs`.
**[GitHub Actions contract]** The official sources do not promise that a
successful server-side create/publish always delivers a parseable client
response.

**Implemented resolution:** the workflow first proves the remote reference is
the fetched tag and that recursive local peeling reaches the validated source
commit. It proves the release tag is absent, arms cleanup, and creates a marker
draft through REST. The marker contains repository, unique run ID, rerun
attempt, and source SHA. It captures the exact ID; on an ambiguous create
response it fully paginates and accepts exactly one marker/tag draft. Cleanup
GET-reverifies ID/tag/draft/marker, retries DELETE, then requires a final 404.
Every upload response and fully paginated asset list must match the exact
name/state/byte-size/SHA-256 set, using the returned `upload_url`. A non-null
API digest must equal the local SHA-256; when the schema-permitted value is
null, the workflow downloads that exact numeric asset ID and verifies its bytes
instead. One PATCH both removes the marker body and publishes; a failed client
response is recovered only when GET of that exact ID already proves the clean
published state and exact asset set. No `target_commitish` response assertion
remains.

### C-10 - Architecture checks must reject a single mixed archive member

**Prior state:** an "any matching Machine" check could accept an archive that
also contained an object for the wrong architecture. A Windows parser also
reduced `IMAGE_FILE_MACHINE_AMD64`/`IMAGE_FILE_MACHINE_ARM64` differently from
the matrix values.

**Implemented resolution:** every `readelf` or `llvm-readobj` Machine entry in
every release library/archive is required to equal the matrix value, and zero
entries fail. Windows compares the complete LLVM tokens. Direct ELF/PE runtime
architecture is independently decoded again by the release collector.
**[GitHub Actions contract]** plus the official object-inspection tool
contracts; this is not an Android rule.

### C-11 - Windows archives copied vcpkg source without its MIT license record

**Prior state:** Windows packages copied the pinned vcpkg libusb manifest and
portfile but omitted vcpkg's MIT `LICENSE.txt`, and SPDX treated the copied
files only as unclassified libaoahid contents.

**Finding:** the exact vcpkg commit contains the MIT license. The copied
license, portfile, and manifest SHA-256 values are respectively
`1ee376fc340e0aa6ad6a3581c94126e741468705096ac92263048a21daa86460`,
`63cc8c6a76d0229859e3ae280471fb625a6304db775e506677d37a3b22b736fc`,
and `865eb2f6137d52dfb3a36d7f95f76e3707811d69f9fbb45e887ccf72d0db76ed`.

**Implemented resolution:** Windows archives contain
`third-party/vcpkg-LICENSE.txt`; package and collection validators pin all
three hashes. Windows SPDX has a third, pinned `vcpkg` package with MIT
declared/concluded license, verification code, `CONTAINS` relationships for
the three files, and `BUILD_DEPENDENCY_OF` libaoahid. Linux SPDX remains the
two-package libaoahid/libusb model. Validation requires exact
`documentDescribes`/`DESCRIBES`, package `CONTAINS`, dependency, and Windows
build-material relationship sets; missing, duplicate, or extra edges fail.
**[SPDX 2.3 contract]**

### C-12 - Generated CMake-helper notice obligations were not established

**Ambiguity found:** `CMakePackageConfigHelpers` and its version templates are
BSD-3-Clause CMake source and generate code intended for installed config
files. CMake's licensing page and module documentation do not explicitly state
the downstream notice obligation, if any, for generated output. A legal
conclusion therefore could not be established from these official sources.

**Implemented resolution:** installed `aoahid-config.cmake` and
`aoahid-config-version.cmake` are now configured from project-owned MIT
templates with `configure_file(@ONLY)`. They implement the tested relocation,
component, SameMajor/range, and pointer-size checks without embedding the
CMake helper/template output. This removes the unresolved generated-text issue
from the distributed files; it does not assert that the prior helper output
required a notice. **[CMake contract]** plus **[project policy]**

### C-13 - Binding registry publication was specified but absent

**Input gap:** `DESIGN.md` §13.5 and Phase 9 require Python wheel and NuGet
publication from the release workflow behind a repository-variable gate. The
prior workflow built no registry package.

**Implemented resolution:** every tagged run builds each package twice and
requires byte identity, validates exact names/version/license/readme/native
dependency boundary, installs and exercises the Python wheel and managed NuGet
assembly, and uploads them as a workflow artifact. Python's complete build-tool
closure is wheel-only and hash-pinned from the official PyPI JSON records.
The C# project has no external `PackageReference`. Publication is default-off
behind `vars.AOAHID_PUBLISH_BINDINGS == 'true'`: PyPI uses the pinned Trusted
Publisher action and OIDC; NuGet uses a scoped `NUGET_API_KEY` only in its
publish step. **[GitHub Actions contract]**

**Unresolved external prerequisite:** the supplied material does not establish
ownership or availability of the registry names `aoahid` and `AoaHid`, nor a
configured PyPI publisher/environment or NuGet secret. Search absence is not
ownership evidence. A repository owner must configure those registry-side
identities before enabling publication. Package construction and validation do
not need registry credentials, so the native GitHub Release waits for that
deterministic package-validation job. Optional PyPI/NuGet publication then
requires both the validated packages and the published native release.

### C-14 - The promised Linux glibc gate was not implemented

**Input gap:** `docs/PORTING.md` promised inspection of minimum GLIBC symbol
versions, but the release workflow did not perform or record it.

**Finding:** GNU `readelf --version-info` displays ELF version sections, and
Ubuntu 22.04 supplies the glibc 2.35 baseline. **[project policy]** selects the
Ubuntu 22.04 artifact baseline; it is not an Android requirement.

**Implemented resolution:** every regular `.so` in every Linux stage is
inspected. Empty/unparseable numeric requirement sets, every nonnumeric
`GLIBC_*` requirement (including `GLIBC_PRIVATE` and `GLIBC_ABI_*`), and any
requirement above `GLIBC_2.35` fail. Canonical per-file and aggregate sets,
maximum required version, and baseline are stored in
`glibc-requirements.json`, build metadata, and the release manifest. The
shared object inside each Linux runtime bundle is reinspected and must agree
with its shared archive.

### C-15 - A standalone runtime binary cannot carry its own license obligations

**Prior ambiguity:** the release published each shared library as a loose
`.so`/`.dll` asset labelled a convenience extract. That asset links libusb
dynamically, but libusb's LGPL-2.1 text, copyright notice, and corresponding
source lived only in the neighboring complete archive. A redistributor who
obtained the loose file alone therefore received a libusb-dependent binary
with no license material in its own distribution unit, and the packaging
self-check that guards the complete archives did not cover it.

**Implemented resolution:** no bare `.so` or `.dll` is uploaded. Each of the
four targets instead publishes a `*-runtime.tar.gz`/`*-runtime.zip` bundle
containing the libaoahid shared library, the libusb runtime it loads,
`LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md`, `build-metadata.json`,
`third-party/libusb-copyright`, `third-party/source/libusb-1.0.30.tar.bz2`,
the pinned vcpkg material on Windows, and a `README.txt` stating the
MIT/LGPL split. `collect_release.validate_runtime_bundle` fails the release if
any of that material is missing, if the libusb runtime is absent, if a bundled
byte differs from the already validated complete archive, or if an unexpected
member appears. Bundle members are copied verbatim from the shared archive, so
they inherit its architecture and glibc validation. The asset total is
unchanged at 23: eight archives, eight SPDX sidecars, four runtime bundles,
one deterministic tagged-source archive, the manifest, and checksums.
**[project policy]**

### C-15a - Package-level SPDX left every file NOASSERTION

**Prior ambiguity:** the SPDX document declared `MIT` for the libaoahid package
and `LGPL-2.1-or-later` for the libusb package, but every `files` entry carried
`licenseConcluded: NOASSERTION`, `licenseInfoInFiles: ["NOASSERTION"]`, and
`copyrightText: NOASSERTION`. A scanner consuming the SBOM could not tell which
packaged bytes were MIT and which were LGPL, which is exactly the distinction
the dynamic-linking design depends on. First-party sources also carried no
per-file license tag, so a single copied file lost its provenance.

**Implemented resolution:** `generate_spdx.file_license` classifies every
packaged file by installed location - MIT for first-party output,
`LGPL-2.1-or-later` for everything installed from libusb, MIT with Microsoft's
copyright for the pinned vcpkg material - and both `generate_spdx.validate_document`
and `collect_release.validate_sidecar` reject a document whose file entries or
package summary disagree. The libaoahid package now concludes
`MIT AND LGPL-2.1-or-later` for the archive while still declaring `MIT` for
libaoahid itself. Separately, every first-party source file carries an
`SPDX-License-Identifier: MIT` tag and copyright line, enforced in CI by
`tools/check_license_headers.py`. **[project policy]**

### C-16 - A documented CMake option prefix silently selected defaults

**Input error:** two README commands omitted one `A` from the `AOAHID_` option
prefix. CMake accepted those as unused cache entries, so the examples did not
select the stated build.

**Implemented resolution:** both commands use the real option names. The
preserved-input/documentation gate scans Markdown and rejects that misspelled
prefix, preventing this silent configuration failure. **[CMake contract]**

### C-17 - "Every target" conflicts with the latest requested release set

**Internal input conflict:** `DESIGN.md` §13.2 lists glibc and musl Linux on
x86-64/AArch64, Windows x86-64/ARM64, and macOS x86-64/arm64. Section 13.5 and
Phase 9 say a tag builds "every target." The latest user instruction instead
names Linux x86-64/AArch64 `.so` and Windows x86-64/ARM64 `.dll`, while also
prioritizing a light release. No external platform source can choose between
these project scopes.

**Current resolution:** CI is configured to compile/test the macOS and musl
portability targets, but the native GitHub Release scope contains the four
explicitly requested glibc/Windows targets: eight variant archives plus four
runtime bundles. `TARGET_MATRIX.md` remains the authority for
whether an exact run or archive has passed. This follows the latest explicit
user scope and does not claim to literally satisfy the older "every target"
wording. Expanding the published matrix to musl/macOS remains an individually
reviewable project decision.
**[project policy]**

### C-18 - ThreadSanitizer is a diagnostic configuration, not a low-latency build

**Input gap:** the requested concurrency validation did not have a supported
build option or a reproducible configure/build/test command. Treating an
ordinary successful test run as evidence of thread safety would also overstate
what a finite schedule establishes.

**[compiler sanitizer contract]** GCC 13.3 §3.12 says
`-fsanitize=thread` instruments memory accesses to detect data races, documents
`TSAN_OPTIONS`, and forbids combining ThreadSanitizer with AddressSanitizer or
LeakSanitizer. Clang 22.1.0's official ThreadSanitizer guide likewise presents
it as a data-race detector, limits it to supported platforms and PIE-style
executables, and states expected run-time slowdowns of 5-15 times and memory
overhead of 5-10 times. Its Security Considerations section says the runtime is
not meant for production executables.

**[CMake contract]** CMake 3.20 supplies separate target compile and link option
commands; the source-compile check modules can probe compiler-and-linker
availability, while `CheckPIESupported`, `POSITION_INDEPENDENT_CODE`, and
`CMP0083` govern the executable PIE check and flags.

**Implemented resolution:** `AOAHID_TSAN=ON` is an explicit Linux-host option
for GNU or Clang C and C++ compilers. Configuration requires successful C and
C++ compile-and-link probes with `-fsanitize=thread`, requires C and C++ PIE
link support, and enables position-independent code only for that sanitizer
configuration. Instrumentation is attached separately to compilation and link
steps, including consumers of an instrumented static target. CMake rejects the
option with `AOAHID_SANITIZE=ON`, with the AddressSanitizer-based fuzz target,
on a non-Linux host, or when either selected compiler/runtime probe fails.

The `tsan` configure, build, and test presets provide one reproducible path.
They select `RelWithDebInfo`, tests and examples, the deterministic fake libusb
backend, and `TSAN_OPTIONS=halt_on_error=1` for CTest. The Linux CI matrix is
configured to run the full CTest suite with the same sanitizer and halt policy;
whether a particular remote run passed remains a `TARGET_MATRIX.md` fact, not a
workflow-definition fact.

**[implementation observation]** The exact suite reports a false mutex double
lock in `std::condition_variable::wait_for` with Ubuntu 22.04's GCC 11.4 and
`libtsan0`, matching unresolved GCC bug 101978. The same shared and static suites
pass with GCC 12.3 and GCC 13.3, while a deliberately unsynchronized fixture is
still detected by GCC 12.3. CMake therefore rejects GNU 11 for
`AOAHID_TSAN=ON`, and CI selects the runner's GCC 12 rather than suppressing a
class of reports or excluding tests. This is a compiler-runtime constraint, not
evidence that a finite passing schedule proves race freedom.

Because the official Clang guide documents large instrumentation overhead and
a non-production runtime, no timing or load measured in this configuration is
used as release-performance evidence. **[project policy]** A passing finite
TSan run means that no report was emitted for the schedules exercised; it is
not a proof that every possible execution is data-race-free and it does not
qualify physical Android behavior.

### C-19 - A short periodic event timeout wastes idle wakeups

**Prior state:** the internal event thread called libusb with a 10 ms timeout,
creating a 100 Hz idle wake cadence even when no Device, transfer, retry, or
teardown work existed.

**[libusb contract]** the official libusb 1.0.30 multi-threaded event-handling
guide recommends a long or unlimited timeout to minimize idle CPU use and
warns against using short periodic timeouts to avoid the event-lock protocol.
The polling API provides `libusb_interrupt_event_handler` specifically to wake
an active event handler, including when stopping a dedicated event thread
before `libusb_exit`.

**[implementation observation]** at libusb v1.0.30 commit
`87a55632db62c9bdc58cd31d3ccfa673f1bb017f`,
`libusb_interrupt_event_handler` sets the user-interrupt event, the event path
wakes and clears it, and an interrupt without another event returns from event
handling without a special application error.

**Implemented resolution:** internal-thread mode uses a bounded 60-second idle
libusb wait; an earlier pending retry deadline shortens it. Teardown first
publishes the stop flag, then interrupts an event handler that has not already
exited, notifies the library condition variable, joins the thread, and only
later destroys the libusb Context. A backend that returns an immediate error
still takes a separate interruptible 10 ms condition-variable backoff,
preventing an error
spin. The one-minute bound is a conservative **[project policy]** fallback for
a backend that fails to honor the documented interrupt, not a protocol timing
requirement.

The fake backend exposes synchronized counters and wait states. Deterministic
tests require nonzero 60-second idle waits with no idle submit/callback work,
require cancellation to wake a blocked five-second event wait, and require
Context teardown to interrupt a blocked 60-second wait without a timeout wake.
These results prove control-flow and operation-count invariants only. No
wall-clock latency, CPU percentage, real libusb-backend load, or
Android-hardware behavior is inferred. **[project policy]**

### C-20 - Input performance wording exceeds the implemented evidence

**Prior wording:** `DESIGN.md` §0.1, §5.2, and §7.3 describe one
allocation per object, literal zero-copy reporting, no hot-path mode branch,
only one backend system call, and a tens-of-nanoseconds internal-mode cost.

**[repository observation]** Device construction allocates the Device and its
implementation, grows several containers and per-slot buffers, and asks libusb
for one transfer per slot. Generated profile serializers write directly into a
slot buffer, but `aoahid_raw_submit` must copy the caller-owned report so its
lifetime extends through asynchronous completion. The shared pool writes HID
ID and report length per acquisition as already recorded in T-02. Submit also
contains immutable mode/validation checks, atomic read-modify-write operations,
and an opportunistic zero-timeout event-handler call in caller-poll mode. The
internal transport guard is a short spin lock, so its CPU cost under contention
cannot be derived without a workload measurement.

**Implemented resolution:** the verbatim supplied design is preserved as
`docs/inputs/DESIGN_ORIGINAL.md`; the live `DESIGN.md`, `README.md`, `API.md`,
and `LATENCY.md` state only the tested implementation boundaries: no
test-observed C++ allocation and no selected library Context/Node mutex in the
prewarmed caller-poll keyboard cycle; fixed-pool O(1) acquisition; direct
serialization for generated profiles; one accepted asynchronous transfer and
one zero-timeout event pass per measured report; and blocking idle waits in
internal-thread mode. They do not claim a nanosecond cost, a universal
zero-copy API, one total allocation, one total system call, literal
branchlessness, or low CPU under contended internal-mode callers. C-04 remains
the configuration-branch decision. **[project policy]**

### C-21 - Caller-poll mode does not remove libusb backend work

**Prior ambiguity:** "zero allocation," "no mutex," and "no internal event
thread" can describe the measured libaoahid layer too broadly if read as a
claim about libusb, the operating-system backend, or the whole process.

**[implementation observation]** In libusb v1.0.30 commit
`87a55632db62c9bdc58cd31d3ccfa673f1bb017f`, `libusb_alloc_transfer`
allocates and initializes a transfer mutex. Every `libusb_submit_transfer`
takes the Context flying-transfer lock and that transfer lock. On Linux usbfs,
`submit_control_transfer` additionally allocates one `usbfs_urb` per control
submission and invokes `USBDEVFS_SUBMITURB`; `reap_for_handle` uses
`USBDEVFS_REAPURBNDELAY`, and `handle_control_completion` frees the URB under
the transfer lock. `op_handle_events` also holds the Context open-device lock
while reaping. On a non-Android Linux host build with discovery enabled, the
udev or netlink event monitor creates its pthread during first-Context
initialization; the source's Android-application compile-time branch starts
neither monitor.

**[implementation observation]** The Windows backend creates an I/O completion
port and dedicated `windows_iocp_thread` for each libusb Context.
`windows_open` uses `FILE_FLAG_OVERLAPPED` and attaches the device handle to
that port. `windows_submit_transfer` maintains an active-transfer list under
the device-handle lock. The WinUSB-like control path passes the `OVERLAPPED`
embedded in `windows_transfer_priv` to `ControlTransfer`; both immediate and
pending completions are routed to the completion port. The IOCP thread takes
the Context open-device and device-handle locks, signals the completed transfer
to libusb core, and a subsequent libusb event-handler pass performs backend
completion and the application callback. The absence of an explicit `calloc`
inside `winusbx_submit_control_transfer` is not evidence that WinUSB or the OS
performs no internal allocation.

**Implemented resolution:** caller-poll mode means that libaoahid creates no
transfer-event thread and selects no libaoahid Context/Node mutex for the
documented single-caller path. It does not mean that libusb is lock-free,
allocation-free, system-call-free, or thread-free. The Linux and Windows source
paths prove operation structure, not duration, so neither an OS ranking nor a
numeric x86-64/ARM64 latency prediction is made. `LATENCY.md` gives the
measurable host and Android boundaries and a conditional bound only for a
caller-controlled polling gap. All real-backend timing and load remain
**[unverified on hardware]**. Retrieved 2026-08-27.

### C-22 - Darwin `nm -gU` is not by itself a dynamic-export list

**Prior state:** the binary ABI gate treated every name printed by Darwin
`nm -gU` as a public dynamic-library export. The macOS x86-64 CI job then
failed that gate even though its configure, build, and CTest steps completed;
the same gate completed on macOS ARM64. GitHub's unauthenticated check-run API
exposed only the failing step and exit status, not the protected log body, so
the particular linker-generated name is not asserted here.

**[implementation observation]** in Apple `cctools` commit
`e0d56624eca2a76c2ace4c21850df9e666de4ca5`, `nm.c`'s `select_symbol`
implements `-g` by rejecting only symbols without `N_EXT`. A symbol carrying
both `N_EXT` and `N_PEXT` therefore remains in ordinary `-g` output even though
it is private external. The extended `-m` form distinguishes `external`,
`weak external`, `private external`, `non-external (was a private external)`,
and `weak external automatically hidden`; `-U` independently removes undefined
imports. Consequently, parsing every final token from `nm -gU` can report a
private Mach-O implementation symbol as if it were part of the library ABI.

**[implementation observation]** Apple `ld64` commit
`f60a74eaa2c99585de1dc0f2820e7a9f8aaf522c` documents that an
`-exported_symbols_list` retains listed global names and makes other globals
private. Its checked-in `ld-classic.1` explicitly defines `*` as matching zero
or more characters, and `Options.cpp` loads this option with
`kAllowWildcards`; `SetWithWildcards` recognizes `*`, `?`, and `[` and applies
the matcher to candidate symbol names. Apple's Dynamic Library Design
Guidelines also require the Mach-O C spelling in an export list, including the
leading underscore. Thus `_aoahid_*` is a supported linker pattern for the
public C prefix, not a guessed glob syntax.

**[CMake contract]** CMake 3.20.6 documents that
`target_link_options()` accepts `LINKER:` with comma-separated linker
arguments and replaces it with the selected compiler driver's wrapper syntax.
The Apple export-list option is therefore expressed as
`LINKER:-exported_symbols_list,<absolute-source-path>` and the list is also a
`LINK_DEPENDS` input, so editing it relinks the dylib. This is equivalent to
AppleClang's `-Wl` forwarding for this toolchain while leaving quoting and
driver-wrapper construction to CMake.

**Implemented resolution:** `cmake/aoahid.exports` contains `_aoahid_*`; the
Apple shared-library link consumes it through the CMake linker wrapper. The
post-link gate runs `nm -gmU`, excludes lines explicitly classified as private,
non-external, or automatically hidden, and compares only true `external` and
`weak external` definitions with the declarations in `aoahid.h`. It still
rejects any unrelated public export rather than hiding all non-`aoahid` names
inside the checker. Parser fixtures cover each accepted and rejected Darwin
classification. These checks establish the generated Mach-O export boundary;
they are not an Android protocol or hardware result. **[repository
observation]** Retrieved 2026-08-27.

### C-23 - CI package and documentation checks must start from explicit state

**Prior state:** the documentation jobs invoked Doxygen with
`OUTPUT_DIRECTORY = build/doxygen` in a checkout where `build/` did not yet
exist. The Windows release consumer also relied on the package finder's
incoming `aoahid_FOUND` value and on configure-time build booleans to decide
which installed component target files existed.

**[specified CI implementation observation]** Ubuntu 22.04's packaged Doxygen
1.9.1 rejected the missing nested output path and, with `WARN_AS_ERROR = YES`,
aborted before generating documentation. The Visual Studio 18 2026 x64 and
ARM64 release jobs successfully built and symbol-checked `aoahid.dll`, then
reported that the installed `aoahid-config.cmake` set `aoahid_FOUND` false
while configuring a required `shared` consumer. These observations establish
the failed automation boundaries only; they are not USB, AOA, HID, Android, or
hardware behavior.

**[CMake contract]** CMake's official `CMakePackageConfigHelpers` documentation
states that `check_required_components(<PackageName>)` checks every requested
non-optional component's `<PackageName>_<Component>_FOUND` value and sets the
package's aggregate `_FOUND` value false when necessary. The installed target
files are the authoritative contents of a particular staged package.

**Implemented resolution:** both Doxygen jobs create `build/doxygen` before
invocation. The installed config starts from `aoahid_FOUND = TRUE`, derives
shared/static availability from the corresponding installed target files,
loads only requested available components, and finishes with
`check_required_components(aoahid)`. Missing, unknown, and dependency-invalid
components therefore remain rejected, while a present shared-only component
does not depend on stale configure-time state. Repository tests enforce the
ordering and explicit package baseline. **[project policy]** Retrieved
2026-08-27.

### C-24 - The combined pkg-config CI check duplicated the release version

**Prior state:** the Linux combined-package validation compared both generated
pkg-config files with a literal `0.1.0`. After the project and generated files
were advanced to `0.1.1`, both checks failed before any installed-consumer test
could run.

**[repository observation]** `pkg_config_metadata.validate_relocatable` correctly
reported that the installed metadata contained `0.1.1` while CI expected
`0.1.0`. This was a stale test expectation, not corrupt generated metadata and
not an AOA, USB, HID, Android, libusb, latency, or hardware failure.

**Implemented resolution:** the CI script imports the existing release metadata
validator and derives the expected pkg-config version from the canonical
project metadata. A repository test requires both comparisons to use that
derived value and rejects a hard-coded `0.1.x` expectation in this path. For
the owner's explicitly requested clean repository recreation, every canonical
version source and the first release tag remain `0.1.0`; no previously
published tag is rewritten. **[project policy]** Retrieved 2026-08-27.

### C-25 - Doxygen 1.9.1 interprets scoped empty-call prose as a link

**Prior state:** after the documentation workflow began creating its nested
output directory, Ubuntu 22.04's packaged Doxygen reached Markdown parsing but
treated a scoped empty-call spelling in `FACT_AUDIT.md` as an explicit link.
With `WARN_AS_ERROR = YES`, the unresolved target aborted the job.

**[specified CI implementation observation]** Doxygen 1.9.1 reported an
unresolved explicit-link request for the `max` member spelled as an empty
function call after a C++ scope qualifier. Re-running the repository's exact
`tools/docs/Doxyfile` with Ubuntu Jammy's Doxygen 1.9.1 package reproduced the
failure. Rewording the affected prose without changing the documented C++
behavior made that exact binary finish with no warning or error. This is a
documentation-parser compatibility observation, not a C++, USB, AOA, HID,
Android, libusb, latency, or hardware rule.

**Implemented resolution:** live Markdown inputs describe scoped members in
plain prose where an empty-call spelling would trigger Doxygen 1.9.1's link
parser. A repository regression test scans `CHANGELOG.md` and every top-level
document consumed by Doxygen and rejects scoped empty-call spellings inside
code spans. Strict warnings remain enabled; no warning class is suppressed.
**[repository observation]** Retrieved 2026-08-27.

### C-26 - GCC 14 `-Wstringop-overflow` false positive spans sibling arms of one large function

**Prior state:** removing the Keyboard profile's Array press-order vector
from `KeyboardState` left `make_neutral`'s overall shape otherwise
unchanged: one function with an `if`/`else if` chain over
`std::get_if<Alternative>(&node->state)`, one arm per `NodeState`
alternative, each filling its own `std::vector<std::uint8_t>`. The `-O3`
musl AArch64 and x86-64 CI jobs, both building with GCC 14.2.0, failed with
`-Werror=stringop-overflow` on the **Mouse** arm's `std::fill(mouse->buttons
.begin(), mouse->buttons.end(), std::uint8_t{0})`, reporting a bound between
`2**63` and `SIZE_MAX` for the underlying `__builtin_memset` -- an
impossible object size, and a call this change never touched.

**[specified CI implementation observation]** The identical Mouse-arm call
compiled warning-free before the Keyboard arm's `KeyboardState` struct lost
a member, and every arm still compiles warning-free locally under GCC 16.2.1
at `-O3`; no GCC 14 installation was available in this environment to
interactively confirm the analyzer's internal path. Reassigning the warning
to a call in an unrelated sibling arm after only the Keyboard arm's struct
changed indicates GCC 14 unified its `-Wstringop-overflow` value-range
analysis across every arm of the one large function, rather than bounding
each `std::vector<std::uint8_t>` fill from that arm's own local state. This
is consistent with the project's earlier GCC 14
`vector::insert(initializer_list)` false positive (`CHANGELOG.md`
`[0.1.0]`): a real analyzer imprecision, not an out-of-bounds write.

**Implemented resolution:** split each `NodeState` alternative's
neutralization into its own `[[gnu::noinline]]` helper function taking only
that alternative's pointer, so no sibling arm's vector access remains in the
same compiled function for the analyzer to unify ranges across.
`make_neutral` itself is now a short dispatcher. This is a one-time
Node-close-path operation, never the per-report send path, so the added
call is immaterial to the send-path latency policy in `docs/LATENCY.md`. No
warning class is suppressed. **[repository observation]**.
