# AoaHid .NET binding

This NuGet package contains only the managed P/Invoke declarations for the
libaoahid C ABI. It does not contain `aoahid.dll`, `libaoahid.so`, libusb, or
other native runtime assets. Install the matching libaoahid shared distribution
separately and make it available to the .NET native-library loader under the
name `aoahid`.

The NuGet package version and native libaoahid version must match. Complete
native archives, dependency notices, and SPDX documents are published with the
corresponding GitHub Release. Every C option remains explicit; this binding
does not choose tuning overrides. Callers provide every product/target field;
the native C API applies the documented bounded fallback when a Device
transport-tuning field is zero.
