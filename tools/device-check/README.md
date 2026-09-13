# Physical Android evidence capture

`capture.sh` records three of the four evidence layers defined in
`docs/TARGET_MATRIX.md`: the kernel device/capabilities, Android input
classification, and a bounded event stream. It does not infer the event node,
capture duration, target, or pass/fail result.

```sh
tools/device-check/capture.sh \
  <adb-serial> /dev/input/eventN <seconds> <output-directory>
```

Run the application-API test separately and record its exact commit and log in
the same evidence record. A source review, successful USB request, or this
capture alone is not a hardware-verification result.

## Application-API evidence APK

`app/` is a standalone Android application. It enumerates the `InputDevice`
objects visible to the framework and records key and motion events delivered to
its foreground `Activity`. The display is bounded to 256 KiB; the same records
are also written to logcat under the tag `AoaHidDeviceCheck`. The program does
not convert observations into a support verdict.

The CI workflow builds a debug APK as the `aoahid-device-check-debug-apk`
artifact. To build the same project directly, use the AGP-documented Gradle and
JDK versions:

```sh
gradle --no-daemon -p tools/device-check/app :app:assembleDebug
adb -s <adb-serial> install -r \
  tools/device-check/app/app/build/outputs/apk/debug/app-debug.apk
adb -s <adb-serial> shell am start -n \
  dev.aoahid.devicecheck/.MainActivity
adb -s <adb-serial> logcat -s 'AoaHidDeviceCheck:I' '*:S'
```

Exercise the exact keys, axes, contacts, buttons, hover states, and lift states
listed for the profile in `docs/TARGET_MATRIX.md`. Preserve the APK source
commit, app log, device/build identity, `capture.sh` output, and the human
pass/fail decision together. Events intercepted by Android before they reach
the foreground activity are not application-API successes.

## Primary sources used by the application

| Official source | Section or symbol | Revision | Retrieved |
| --- | --- | --- | --- |
| [Android Gradle plugin 9.3.0](https://developer.android.com/build/releases/gradle-plugin) | Compatibility: Gradle 9.5.0, JDK 17, maximum API 37 | 9.3.0 (July 2026) | 2026-08-27 |
| [InputManager API reference](https://developer.android.com/reference/android/hardware/input/InputManager) | `getInputDeviceIds`, `getInputDevice`, `registerInputDeviceListener`, `InputDeviceListener` | Versionless platform API page | 2026-08-27 |
| [InputDevice API reference](https://developer.android.com/reference/android/view/InputDevice) | Identity/source accessors, `getMotionRanges`, `MotionRange` accessors | Versionless platform API page | 2026-08-27 |
| [KeyEvent API reference](https://developer.android.com/reference/android/view/KeyEvent) | Device/source, action, key code, scan code, meta state, repeat count | Versionless platform API page | 2026-08-27 |
| [MotionEvent API reference](https://developer.android.com/reference/android/view/MotionEvent) | Action, pointer ID/count, tool type, coordinates, pressure, orientation, axis values | Versionless platform API page | 2026-08-27 |
| [Activity API reference](https://developer.android.com/reference/android/app/Activity) | `dispatchKeyEvent`, `dispatchGenericMotionEvent`, `dispatchTouchEvent` | Versionless platform API page | 2026-08-27 |

The commands mirror `AOA_HID_GUIDE.md` §§36–40 and the official Android input
documentation registered in `docs/FACT_AUDIT.md`; evidence status remains
**[Unverified on hardware]** until all four layers pass on one identified
device/build combination.
