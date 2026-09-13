# Contributing

Use the canonical
[issue tracker](https://github.com/nemarpuc/Libaoa_hid/issues) for proposed
changes and submit patches through
[pull requests](https://github.com/nemarpuc/Libaoa_hid/pulls).

Every protocol or platform statement must cite an official primary source with
an immutable revision where applicable, section or symbol, and retrieval date.
Do not infer Usage IDs, wire values, Android classification, or libusb behavior.

Changes must keep product, target, HID descriptor, and parser-policy choices
caller-supplied. Only documented host-transport tuning zeros may normalize to
project-policy fallbacks; do not add another fallback without tests and an
evidence-qualified audit entry. Add boundary and failure tests, run formatting
and the complete CTest suite, and update the fact audit when source evidence
changes. Hardware claims require all four observations in
`docs/TARGET_MATRIX.md`; source review alone is not hardware evidence.
