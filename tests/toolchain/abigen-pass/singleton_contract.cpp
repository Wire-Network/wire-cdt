#include <sysio/asset.hpp>
#include <sysio/sysio.hpp>
#include <sysio/name.hpp>
#include <sysio/singleton.hpp>
#include <sysio/kv_multi_index.hpp>

#include "exclude_from_abi.hpp"
 
using namespace sysio;

// A table row is a struct, so a singleton over a bare `sysio::name` is wrapped in one. It used
// to compile and describe nothing at all -- sysio::singleton is an alias template over
// kv_singleton, which the table visitor never matched, so no singleton in this fixture reached
// the ABI. Now that they do, the row rule applies to them like any other table, and the wrapper
// is what makes the entry self-describing rather than a bare scalar the client has to guess at.
struct [[sysio::table]] simple_name {
    sysio::name value;
};

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

typedef sysio::singleton<"smpl.conf5"_n, simple_name> smpl_config5;
typedef sysio::singleton<"config5"_n, out_of_class2> config5;
typedef smpl_config5 smpl_config51;
typedef config5 config51;
using  smpl_conf51 = sysio::singleton<"smpl.conf51"_n, simple_name>;
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
    typedef sysio::singleton<"smpl.config"_n, simple_name> smpl_config;
    using smpl_config2 = smpl_config5;
    typedef config551 config2; //from exclude_from_abi.hpp
};
