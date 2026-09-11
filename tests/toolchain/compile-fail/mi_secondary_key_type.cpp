// A multi_index secondary key must be one of the five types upstream supports.
//
// The secondary index is a byte-ordered map: kv_idx_lower_bound hands the encoded key to the
// chain, which compares it with memcmp. So the ENCODING carries the order, and only the five
// types with an order-preserving encoder have one -- uint64_t, uint128_t, double, long double
// and checksum256, which is exactly upstream's five db_idx* intrinsic families.
//
// A uint32_t used to compile and then reach a generic pack(), which writes an integer in
// native little-endian. Under that, 0x00000100 sorts BELOW 0x00000001, so lower_bound and
// ordered iteration returned rows in the key's byte order rather than its value order --
// silently, because find() matches on equality and does not depend on the encoding.
//
// multi_index is the backward-compatibility shim, and a contract ported from an Antelope
// chain cannot have a key outside the five: it would not have compiled where it came from.
// So the set is closed rather than widened. A contract that wants a narrow integer, an enum,
// a name or a composite key should use kv::table with kv::index, which encodes through
// be_key_stream -- the same encoding the chain's be_key_codec builds query bounds with.
#include <sysio/sysio.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

class [[sysio::contract("mi_secondary_key_type")]] mi_secondary_key_type : public contract {
public:
   using contract::contract;

   struct [[sysio::table]] rec {
      uint64_t id;
      uint32_t small;

      uint64_t primary_key() const { return id; }
      uint32_t by_small() const { return small; }

      SYSLIB_SERIALIZE(rec, (id)(small))
   };

   using recs = multi_index<"recs"_n, rec,
      indexed_by<"bysmall"_n, const_mem_fun<rec, uint32_t, &rec::by_small>>>;

   [[sysio::action]]
   void scan(uint32_t from) {
      recs t(get_self(), get_self().value);
      auto idx = t.get_index<"bysmall"_n>();
      for (auto it = idx.lower_bound(from); it != idx.end(); ++it)
         print(it->id, " ");
   }
};
