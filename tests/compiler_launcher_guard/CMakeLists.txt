cmake_minimum_required(VERSION 3.19)
project(cdt_compiler_launcher_guard)

find_package(cdt REQUIRED)

# This object-only target proves that the explicitly enabled launcher is active
# and that the second clean build actually exercises a warm cache.
add_library(cache_probe OBJECT cache_probe.cpp)

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
