// A container payload is a row type the ABI can name, and must be named.
//
// `sysio::singleton<"values"_n, std::vector<uint64_t>>` is ordinary contract code that the
// runtime stores and a client decodes perfectly well. abigen rejected it outright:
//
//   error: abigen error (table 'values' has row type 'std::vector<uint64_t>', which the ABI
//          cannot describe; use a struct or a builtin ABI type)
//
// The row was describable all along. A template argument read with getTemplateArgs()[i]
// .getAsType() is CANONICAL, and translate_type()'s container, map and variant branches match
// only on a TemplateSpecializationType -- so the very same std::vector<uint64_t> translated to
// `uint64[]` as an action parameter, where the sugar survives, and to the bare record name
// `vector` as a row. The describability guard then caught `vector`, correctly, and reported the
// row rather than the missing sugar. Restoring the sugar puts both spellings on one path.
//
// The guard itself is unchanged in kind: a row is describable when the ABI can resolve what it
// names, and `uint64[]` resolves by stripping the suffix -- which is exactly how the chain's
// own serializer reads it. abigen-fail/undescribable_row_type pins the other side.
//
// ONE level. A container of a container is still refused -- abigen-fail/nested_container_row --
// because the machinery that names those (`B_vector_uint64_E`) is driven off the same printed
// form and declares the typedef the name needs, and a half-resugared type slipped past it and
// published `B_vector_uint64_E[]` with nothing declaring it. Refusing where the author can see
// it is what master did; supporting it is a separate piece of work.
#include <sysio/sysio.hpp>
#include <sysio/singleton.hpp>
#include <map>
#include <optional>
#include <utility>
#include <vector>

using namespace sysio;

class [[sysio::contract("singleton_container_row")]] singleton_container_row : public contract {
public:
   using contract::contract;

   struct row {
      uint64_t v;
      SYSLIB_SERIALIZE(row, (v))
   };

   using vec_singleton    = sysio::singleton<"vecsing"_n,    std::vector<uint64_t>>;
   using bytes_singleton  = sysio::singleton<"bytessing"_n,  std::vector<uint8_t>>;
   using opt_singleton    = sysio::singleton<"optsing"_n,    std::optional<uint64_t>>;
   using map_singleton    = sysio::singleton<"mapsing"_n,    std::map<uint64_t, uint64_t>>;
   using pair_singleton   = sysio::singleton<"pairsing"_n,   std::pair<uint64_t, uint64_t>>;
   // A struct element still has to be DECLARED: the table names `row[]`, and the chain refuses
   // a document whose table names a type it cannot resolve.
   using rowvec_singleton = sysio::singleton<"rowvecsing"_n, std::vector<row>>;

   [[sysio::action]]
   void test() {
      vec_singleton    a(get_self(), get_self().value);
      bytes_singleton  b(get_self(), get_self().value);
      opt_singleton    d(get_self(), get_self().value);
      map_singleton    e(get_self(), get_self().value);
      pair_singleton   f(get_self(), get_self().value);
      rowvec_singleton g(get_self(), get_self().value);

      a.set({1, 2, 3}, get_self());
      b.set({4, 5}, get_self());
      d.set(8, get_self());
      e.set({{9, 10}}, get_self());
      f.set({11, 12}, get_self());
      g.set({row{13}}, get_self());
   }
};
