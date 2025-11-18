#include <sysio/sysio.hpp>
#include <sysio/instant_finality.hpp>

class [[sysio::contract]] instant_finality_tests : public sysio::contract{
public:
    using contract::contract;

    [[sysio::action]]
    void setfinalizer(const sysio::finalizer_policy& finalizer_policy) {
        sysio::set_finalizers(finalizer_policy);
    }
};
