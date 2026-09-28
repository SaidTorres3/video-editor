# OpenFX host support

Source: https://github.com/AcademySoftwareFoundation/openfx
Revision: `e40728885390ec16276d11e00025de9b4282060c`
License: BSD-3-Clause; see LICENSE.md and source headers.

The API headers, HostSupport sources, and Basic example (test fixture only) are vendored. The editor reuses
these suites rather than implementing its own OpenFX ABI. They are compiled
statically; third-party Windows x64 effect bundles remain external plugins.

Local changes: persistent XML cache loading is disabled (readCache throws),
removing the unused Expat dependency. Plugin descriptions are loaded from the
user-selected binary. No plugin cache XML is read or written. HostSupport is
compiled without UNICODE because its upstream Windows loader uses narrow paths.
Binary enumeration also validates the plugin count, null entries, entry points,
and unsupported APIs before creating descriptors.
