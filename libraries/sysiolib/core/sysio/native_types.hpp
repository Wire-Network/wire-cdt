#pragma once

/**
 * @file
 * Provide the WASM sysroot's extended integer types when add_native_contract()
 * uses the host standard library. Real typedefs preserve C++ construction and
 * name lookup semantics; preprocessor substitutions do not.
 * Mark these compiler extensions explicitly so force-including this header is
 * warning-free even when the consumer enables pedantic diagnostics.
 */

/// Unsigned 128-bit integer, matching the CDT WASM sysroot.
__extension__ typedef unsigned __int128 uint128_t;

/// Signed 128-bit integer, matching the CDT WASM sysroot.
__extension__ typedef __int128 int128_t;
