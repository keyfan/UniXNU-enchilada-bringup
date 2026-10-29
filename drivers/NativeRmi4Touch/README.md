# NativeRmi4Touch

Synaptics RMI4 subset for a 2-D touch sensor: the page description table (with the S3706B fixed-address fallback of the
postmarketOS clone-controller quirks), F01 device control and F12 2-D sensor (register descriptors, or the hardcoded layout when
a controller has none). Transport-independent pure C; page selection is done here.

Current state: built into the enchilada kernel by `scripts/build-enchilada-touch.py` (libEnchiladaTouch.a); not yet run on hardware.

Host test: `tests/rmi4-test.cpp` against the controller model `tests/FakeRmi.h` (genuine and clone layouts), run by
`scripts/check-enchilada-touch-host.py`.
