# aoahid Python binding

This wheel contains only the platform-independent `ctypes` declarations for
the libaoahid C ABI. It deliberately contains no `.so`, `.dll`, `.dylib`, or
libusb runtime. Install the matching libaoahid shared distribution separately,
then pass its exact path to `aoahid.load(path)`. The binding never guesses a
native-library path or chooses an option override. Callers still provide every
product/target field. A zero-valued Device transport-tuning field is normalized
by the native C API to the values documented in `docs/API.md`; this Python layer
does not apply them itself.

The Python package version and the native libaoahid version must match. Complete
native archives, dependency notices, and SPDX documents are published with the
corresponding GitHub Release.
