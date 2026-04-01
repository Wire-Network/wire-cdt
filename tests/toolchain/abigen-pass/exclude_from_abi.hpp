#pragma once

#include <sysio/sysio.hpp>
#include <sysio/name.hpp>
#include <sysio/singleton.hpp>
#include <sysio/kv_multi_index.hpp>



struct [[sysio::table]] out_of_class {
    uint64_t id;
    uint64_t primary_key() const { return id; }
};
typedef sysio::kv_multi_index<"mi.config55"_n, out_of_class> out_of_class_index;
using uout_of_class_index = sysio::kv_multi_index<"mi.config551"_n, out_of_class>;

typedef sysio::singleton<"smpl.conf55"_n, sysio::name> smpl_config55;
typedef sysio::singleton<"config55"_n, out_of_class> config55;
typedef smpl_config55 smpl_config551;
typedef config55 config551;
using  smpl_conf551 = sysio::singleton<"smpl.conf551"_n, sysio::name>;
using  config552 = sysio::singleton<"config552"_n, out_of_class>;
using smpl_conf552 = smpl_conf551;
using config553 = config551;

class [[sysio::contract("singleton_contract_simple2")]] singleton_contract_simple2 : public sysio::contract {
    public:
        using sysio::contract::contract;
        
    [[sysio::action]]
        void whatever() {};
    
    struct [[sysio::table]] inside_class {
        uint64_t id;
        uint64_t primary_key() const { return id; }
    };
    typedef sysio::singleton<"smpl.conf552"_n, sysio::name> smpl_conf552;
    typedef sysio::singleton<"config552"_n, inside_class> config552;
    typedef smpl_conf552 smpl_conf553;
    typedef config552 config553;
    using smpl_conf554 = sysio::singleton<"smpl.conf554"_n, sysio::name>;
    using config554 = sysio::singleton<"config554"_n, inside_class>;
    using smpl_conf555 = smpl_conf554;
    using config555 = config554;



    typedef sysio::kv_multi_index<"mi.config553"_n, inside_class> inside_class_index;
    using uinside_class_index = sysio::kv_multi_index<"mi.config554"_n, inside_class>;
};
