#pragma once

#include <sysio/kv_singleton.hpp>

namespace sysio {

   // sysio::singleton is now backed by the KV database.
   // This header provides backward compatibility so existing contracts
   // continue to compile with #include <sysio/singleton.hpp> and sysio::singleton<...>.
   template<sysio::name::raw SingletonName, typename T>
   using singleton = kv_singleton<SingletonName, T>;

} // namespace sysio
