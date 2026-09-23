# Compiling these examples

Every command below was actually run and verified against a real build of
this package before being written down here. If a command fails for you
exactly as written, that is a bug in this file or the library, not a step
you are missing -- please open an issue.

## C examples

All C examples link against `libaoahid`, which is written in C++ internally.
Even the **static** archive (`lib/libaoahid.a`) still needs the C++ standard
library at link time, so every command below includes `-lstdc++`. Forgetting
it produces `undefined reference to std::...` linker errors.

From the directory this README is in (i.e. the package root, one level above
`examples/`):

```sh
# One example per HID profile kind. None of these opens a device; each builds a
# complete Spec, prints the descriptor it produced, and then prints the exact
# call sequence that would send that profile to a real phone.
cc -std=c17 -O2 -Iinclude examples/c/profiles/keyboard.c    lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o keyboard
cc -std=c17 -O2 -Iinclude examples/c/profiles/mouse.c       lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o mouse
cc -std=c17 -O2 -Iinclude examples/c/profiles/toggle.c      lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o toggle
cc -std=c17 -O2 -Iinclude examples/c/profiles/gamepad.c     lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o gamepad
cc -std=c17 -O2 -Iinclude examples/c/profiles/touchscreen.c lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o touchscreen
cc -std=c17 -O2 -Iinclude examples/c/profiles/pen.c         lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o pen
cc -std=c17 -O2 -Iinclude examples/c/profiles/battery.c     lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o battery
cc -std=c17 -O2 -Iinclude examples/c/profiles/raw.c         lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o raw

# The full discovery-to-teardown lifecycle example. Opens a real device if one
# is connected.
cc -std=c17 -O2 -Iinclude examples/c/multi_profile.c lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o multi_profile

# Human-observable real-device demos (docs/QUICKSTART.md has the full
# walkthrough). These need -lm in addition to the above for math.h.
cc -std=c17 -O2 -Iinclude examples/c/verify/verify_keyboard.c lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o verify_keyboard
cc -std=c17 -O2 -Iinclude examples/c/verify/verify_touch.c    lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -lm -o verify_touch
cc -std=c17 -O2 -Iinclude examples/c/verify/verify_mouse.c    lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -lm -o verify_mouse
cc -std=c17 -O2 -Iinclude examples/c/verify/verify_toggle.c   lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o verify_toggle
cc -std=c17 -O2 -Iinclude examples/c/verify/verify_battery.c  lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o verify_battery
cc -std=c17 -O2 -Iinclude examples/c/verify/verify_all.c      lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -lm -o verify_all
cc -std=c17 -O2 -Iinclude examples/c/verify/verify_accessory.c lib/libaoahid.a -lstdc++ -lusb-1.0 -lpthread -o verify_accessory
```

If you downloaded the **shared** package instead (`lib/libaoahid.so` /
`aoahid.dll`), replace `lib/libaoahid.a` with `-Llib -laoahid` in every
command above, and make sure the shared library is on your loader's search
path at run time (`LD_LIBRARY_PATH=./lib` on Linux, `DYLD_LIBRARY_PATH=./lib`
on macOS, or just keep the `.dll` next to the `.exe` on Windows).

The `*-runtime` release bundle is not enough to compile any of this: it carries
the shared libraries and their license material for deploying a finished
application, but no headers, import library, or package metadata. Build against
a `*-shared` or `*-static` archive of the same version.

`libusb-1.0` above assumes it is installed system-wide (`pkg-config
--libs libusb-1.0` will confirm the exact flags your system needs if this
does not link). See `docs/PORTING.md` for platform-specific notes.

## Python, Rust, and C# examples

See `examples/python/`, `examples/rust/`, and `examples/csharp/` directly;
each has its own build file (`Cargo.toml`, `.csproj`) that names the exact
dependency versions it was written against. `docs/EXAMPLES.md` in this
package explains what each example demonstrates.
