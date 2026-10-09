#pragma once

/**
 * @file
 * Provide the WASM sysroot's extended integer types when add_native_contract()
 * uses the host standard library. Real typedefs preserve C++ construction and
 * name lookup semantics; preprocessor substitutions do not.
 */

/// Unsigned 128-bit integer, matching the CDT WASM sysroot.
typedef unsigned __int128 uint128_t;

/// Signed 128-bit integer, matching the CDT WASM sysroot.
typedef __int128 int128_t;
