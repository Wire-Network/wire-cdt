#include <sysio/sysio.hpp>
#include <tuple>
#include <vector>

using namespace sysio;
using std::map;
using std::string;
using std::tuple;
using std::vector;

class [[sysio::contract]] nested_container : public contract {
public:
   using contract::contract;

    [[sysio::action]] 
    void map2map(map<string, string> m, map<string, map<string, string>> m2m) {}

    [[sysio::action]]
    void settuple2(name user, const tuple  <int, double, string, vector<int> >& tp2) {}
};
