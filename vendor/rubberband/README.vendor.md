# Vendored Rubber Band

This directory contains the source files required by Video Editor's
single-translation-unit Rubber Band build.

- Upstream: https://github.com/breakfastquay/rubberband
- Revision: `1d95888bec3ae0a17c0c4af791810d5a63f6bc35`
- Version reported by the headers: 4.0.0
- License: GNU GPL version 2 or later; see `COPYING`

The `rubberband`, `single`, and `src` directories are copied unchanged from
that revision. `CMakeLists.txt` compiles `single/RubberBandSingle.cpp` into the
local `rubberband_vendor` static library, so configuring the project does not
download anything.
