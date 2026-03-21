/**
 *  @file db.h
 *  @copyright defined in eos/LICENSE
 *  @brief Legacy database C API — replaced by KV intrinsics
 *
 *  The legacy db_*_i64 and db_idx* functions have been removed.
 *  All contract storage now uses the KV database API declared in <sysio/kv.h>.
 *  The C++ wrapper is available via <sysio/multi_index.hpp> (drop-in replacement).
 */
#pragma once

#include <sysio/kv.h>
