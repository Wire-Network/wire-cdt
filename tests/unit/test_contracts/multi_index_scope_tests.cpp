#include <sysio/sysio.hpp>
#include <sysio/kv_multi_index.hpp>

// Separate contract for cross-scope secondary index isolation tests.
// Split from multi_index_tests.cpp to stay under max_transaction_net_usage.

namespace _scope_tests {

struct [[sysio::table("scopetbl")]] row {
   uint64_t id;
   uint64_t sec;

   auto primary_key() const { return id; }
   uint64_t get_secondary() const { return sec; }

   SYSLIB_SERIALIZE(row, (id)(sec))
};

} // namespace _scope_tests

class [[sysio::contract]] mi_scope_tests : public sysio::contract {
public:
   using sysio::contract::contract;

   using scope_table = sysio::multi_index<"scopetbl"_n, _scope_tests::row,
      sysio::indexed_by<"bysec"_n, sysio::const_mem_fun<_scope_tests::row, uint64_t,
         &_scope_tests::row::get_secondary>>
   >;

   // Same PKs and overlapping secondary keys in two scopes — iteration isolated
   [[sysio::action]]
   void xscope() {
      sysio::name payer = get_self();
      scope_table ta(get_self(), "scope.a"_n.value);
      scope_table tb(get_self(), "scope.b"_n.value);

      ta.emplace(payer, [](auto& r) { r.id = 1; r.sec = 100; });
      ta.emplace(payer, [](auto& r) { r.id = 2; r.sec = 200; });
      tb.emplace(payer, [](auto& r) { r.id = 1; r.sec = 300; });
      tb.emplace(payer, [](auto& r) { r.id = 2; r.sec = 400; });

      auto idxA = ta.get_index<"bysec"_n>();
      auto it = idxA.begin();
      sysio::check(it != idxA.end() && it->sec == 100, "xscope: A[0]=100");
      ++it;
      sysio::check(it != idxA.end() && it->sec == 200, "xscope: A[1]=200");
      ++it;
      sysio::check(it == idxA.end(), "xscope: A end after 2");

      auto idxB = tb.get_index<"bysec"_n>();
      auto itb = idxB.begin();
      sysio::check(itb != idxB.end() && itb->sec == 300, "xscope: B[0]=300");
      ++itb;
      sysio::check(itb != idxB.end() && itb->sec == 400, "xscope: B[1]=400");
      ++itb;
      sysio::check(itb == idxB.end(), "xscope: B end after 2");
   }

   // find() in scope A must not see scope B's entries
   [[sysio::action]]
   void xscopefind() {
      sysio::name payer = get_self();
      scope_table ta(get_self(), "xfind.a"_n.value);
      scope_table tb(get_self(), "xfind.b"_n.value);

      ta.emplace(payer, [](auto& r) { r.id = 1; r.sec = 50; });
      tb.emplace(payer, [](auto& r) { r.id = 1; r.sec = 99; });

      auto idxA = ta.get_index<"bysec"_n>();
      sysio::check(idxA.find(99) == idxA.end(), "xscopefind: 99 not in A");
      sysio::check(idxA.find(50) != idxA.end(), "xscopefind: 50 in A");
   }

   // erase from scope A must not affect scope B
   [[sysio::action]]
   void xscopeerase() {
      sysio::name payer = get_self();
      scope_table ta(get_self(), "xerase.a"_n.value);
      scope_table tb(get_self(), "xerase.b"_n.value);

      ta.emplace(payer, [](auto& r) { r.id = 1; r.sec = 77; });
      tb.emplace(payer, [](auto& r) { r.id = 1; r.sec = 77; });

      auto idxA = ta.get_index<"bysec"_n>();
      idxA.erase(idxA.find(77));
      sysio::check(idxA.begin() == idxA.end(), "xscopeerase: A empty");

      auto idxB = tb.get_index<"bysec"_n>();
      sysio::check(idxB.find(77) != idxB.end(), "xscopeerase: B still has 77");
   }

   // upper_bound in scope A stops at scope boundary
   [[sysio::action]]
   void xscopeub() {
      sysio::name payer = get_self();
      scope_table ta(get_self(), "xub.a"_n.value);
      scope_table tb(get_self(), "xub.b"_n.value);

      ta.emplace(payer, [](auto& r) { r.id = 1; r.sec = 10; });
      ta.emplace(payer, [](auto& r) { r.id = 2; r.sec = 20; });
      tb.emplace(payer, [](auto& r) { r.id = 1; r.sec = 15; });

      auto idxA = ta.get_index<"bysec"_n>();
      auto it = idxA.upper_bound(10);
      sysio::check(it != idxA.end() && it->sec == 20, "xscopeub: ub(10)=20");
      ++it;
      sysio::check(it == idxA.end(), "xscopeub: end after ub");
   }

   // reverse iteration stays within scope
   [[sysio::action]]
   void xscoperev() {
      sysio::name payer = get_self();
      scope_table ta(get_self(), "xrev.a"_n.value);
      scope_table tb(get_self(), "xrev.b"_n.value);

      ta.emplace(payer, [](auto& r) { r.id = 1; r.sec = 100; });
      ta.emplace(payer, [](auto& r) { r.id = 2; r.sec = 200; });
      tb.emplace(payer, [](auto& r) { r.id = 1; r.sec = 300; });

      auto idxA = ta.get_index<"bysec"_n>();
      auto rit = idxA.rbegin();
      sysio::check(rit != idxA.rend() && rit->sec == 200, "xscoperev: rbegin=200");
      ++rit;
      sysio::check(rit != idxA.rend() && rit->sec == 100, "xscoperev: next=100");
      ++rit;
      sysio::check(rit == idxA.rend(), "xscoperev: rend after 2");
   }
};
