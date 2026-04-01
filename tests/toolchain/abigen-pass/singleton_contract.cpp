#include <sysio/asset.hpp>
#include <sysio/sysio.hpp>
#include <sysio/name.hpp>
#include <sysio/singleton.hpp>
#include <sysio/kv_multi_index.hpp>

#include "exclude_from_abi.hpp"
 
using namespace sysio;

struct [[sysio::table]] out_of_class2 {
    uint64_t id;
    uint64_t primary_key() const { return id; }
};
typedef sysio::kv_multi_index<"mi.config5"_n, out_of_class2> out_of_class_index51;
using uout_of_class_index51 = sysio::kv_multi_index<"mi.config51"_n, out_of_class2>;

struct [[sysio::table, sysio::contract("singleton_contract")]] out_of_class3 {
    uint64_t id;
    uint64_t primary_key() const { return id; }
};
typedef sysio::kv_multi_index<"mi.config52"_n, out_of_class3> out_of_class_index52;

typedef sysio::singleton<"smpl.conf5"_n, sysio::name> smpl_config5;
typedef sysio::singleton<"config5"_n, out_of_class2> config5;
typedef smpl_config5 smpl_config51;
typedef config5 config51;
using  smpl_conf51 = sysio::singleton<"smpl.conf51"_n, sysio::name>;
using  config52 = sysio::singleton<"config52"_n, out_of_class2>;
using smpl_conf52 = smpl_conf51;
using config53 = config51;

class [[sysio::contract("singleton_contract")]] singleton_contract : public contract {
    public:
        using contract::contract;
        
    [[sysio::action]]
        void whatever() {};
 
    struct [[sysio::table]] tbl_config {
        uint64_t y;
        uint64_t x;
    };
    
    typedef sysio::singleton<"config"_n, tbl_config>    config;
    typedef sysio::singleton<"smpl.config"_n, name>     smpl_config;
    using smpl_config2 = smpl_config5;
    typedef config551 config2; //from exclude_from_abi.hpp
};
