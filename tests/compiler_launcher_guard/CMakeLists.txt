cmake_minimum_required(VERSION 3.19)
project(cdt_compiler_launcher_guard)

find_package(cdt REQUIRED)

# These object-only targets prove that distinct Wasm sources do not collapse to
# the same cached object and that the second clean build is genuinely warm.
add_library(cache_probe_a OBJECT cache_probe_a.cpp)
add_library(cache_probe_b OBJECT cache_probe_b.cpp)

# add_contract() must clear the global launcher because its compiler steps emit
# sidecars that a generic compiler cache cannot restore.
add_contract(launcher_guard launcher_guard launcher_guard.cpp)
foreach(LANG C CXX)
  get_target_property(LAUNCHER launcher_guard ${LANG}_COMPILER_LAUNCHER)
  if(LAUNCHER)
    message(FATAL_ERROR
      "add_contract() retained ${LANG}_COMPILER_LAUNCHER: ${LAUNCHER}")
  endif()
endforeach()
unset(LANG)
unset(LAUNCHER)

# Native object-only compilation must use a separate directory from the Wasm
# probes above.
add_contract_native(native_probe native_probe native_probe.cpp)
get_target_property(NATIVE_LAUNCHER native_probe CXX_COMPILER_LAUNCHER)
list(FIND NATIVE_LAUNCHER "CCACHE_DIR=${CDT_NATIVE_CCACHE_DIR}"
  NATIVE_CACHE_DIR_INDEX)
if(NATIVE_CACHE_DIR_INDEX EQUAL -1)
  message(FATAL_ERROR
    "add_contract_native() did not select the native cache: ${NATIVE_LAUNCHER}")
endif()

set(SAVED_NATIVE_CCACHE_DIR "${CDT_NATIVE_CCACHE_DIR}")
set(CDT_NATIVE_CCACHE_DIR "")
add_contract_native(native_probe_uncached native_probe_uncached native_probe.cpp)
get_target_property(UNCACHED_NATIVE_LAUNCHER
  native_probe_uncached CXX_COMPILER_LAUNCHER)
if(UNCACHED_NATIVE_LAUNCHER)
  message(FATAL_ERROR
    "Native target without a separate cache retained its launcher: ${UNCACHED_NATIVE_LAUNCHER}")
endif()
set(CDT_NATIVE_CCACHE_DIR "${SAVED_NATIVE_CCACHE_DIR}")
unset(SAVED_NATIVE_CCACHE_DIR)
unset(NATIVE_CACHE_DIR_INDEX)
unset(NATIVE_LAUNCHER)
unset(UNCACHED_NATIVE_LAUNCHER)
