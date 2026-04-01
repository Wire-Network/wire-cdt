/**
 * @file
 * @copyright defined in eos/LICENSE
 */
#pragma once

#include <sysio/kv_multi_index.hpp>

namespace sysio {

   // sysio::multi_index is now backed by the KV database.
   // This header provides backward compatibility so existing contracts
   // continue to compile with #include <sysio/multi_index.hpp> and sysio::multi_index<...>.
   template<sysio::name::raw TableName, typename T, typename... Indices>
   using multi_index = kv_multi_index<TableName, T, Indices...>;

} // namespace sysio
