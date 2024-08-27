---
content_title: SYSIO.CDT To CDT
---

For this release all of the naming of namespaces and file paths will remain as `sysio`, so there is no need to migrate away from that with this release.

But there are a few differences.

1. The tool names have change prefix. I.e. tools like `sysio-cpp` are now `cdt-cpp`.
2. The CMake toolchain file has changed its name from `SysioWasmToolchain.cmake` to `CDTWasmToolchain.cmake`.
3. The library path for the CMake package has changed from `/usr/local/lib/cmake/sysio.cdt` to `/usr/local/lib/cmake/cdt` (or alternatively from `/usr/lib/cmake/sysio.cdt` to `/usr/lib/cmake/cdt` if installing from the Debian package). Additionally, the name to use with CMake `find_package` has similarly changed: use `find_package(cdt)` now instead of `find_package(sysio.cdt)`.
