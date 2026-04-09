#pragma once
#include <sysio/sysio.hpp>
#include <sysio/kv_global.hpp>

using namespace sysio;

struct [[sysio::table("appconfig")]] app_config {
   uint64_t max_transfer;
   uint32_t fee_bps;       // basis points (1/100th of a percent)
   bool     paused;
   SYSLIB_SERIALIZE(app_config, (max_transfer)(fee_bps)(paused))
};

class [[sysio::contract("kv_global_example")]] kv_global_example : public contract {
public:
   using contract::contract;

   kv::global<"appconfig"_n, app_config> config{get_self()};

   [[sysio::action]] void setconfig(uint64_t max_transfer, uint32_t fee_bps);
   [[sysio::action]] void pause();
   [[sysio::action]] void unpause();
   [[sysio::action]] void transfer(name from, name to, uint64_t amount);
   [[sysio::action]] void getconfig();
};
