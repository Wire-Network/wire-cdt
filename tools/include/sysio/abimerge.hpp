#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#pragma GCC diagnostic ignored "-Wcovered-switch-default"
#include <jsoncons/json.hpp>
#include "abi.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using jsoncons::json;
using jsoncons::ojson;

class ABIMerger {
   public:
      /// version_major/version_minor seed an empty accumulator and are not retained: every
      /// document that reaches version_of() declares its own version, and one that does not
      /// is rejected rather than falling back to the merger's.
      ABIMerger(ojson a, int version_major, int version_minor) : abi(a) {
         if (abi.empty()) {
            abi["version"] = abi_version::version_string(version_major, version_minor);
            abi["types"] = ojson::array();
            abi["structs"] = ojson::array();
            abi["actions"] = ojson::array();
            abi["tables"] = ojson::array();
            abi["ricardian_clauses"] = ojson::array();
            abi["variants"] = ojson::array();
            abi["action_results"] = ojson::array();
         }
      }
      void set_abi(ojson a) {
         abi = a;
      }
      std::string get_abi_string()const {
         std::stringstream ss;
         ss << pretty_print(abi);
         return ss.str();
      }
      ojson merge(ojson other) {
         ojson ret;
         if (abi.has_key("____comment"))
            ret["____comment"] = abi["____comment"];
         else if (other.has_key("____comment"))
            ret["____comment"] = other["____comment"];
         // The emitted version is the newer of the two documents, so the capability
         // gate below must consult THAT, not just the left-hand side. Gating on the
         // left alone emitted e.g. 1.10 while dropping the action_results the newer
         // side carried -- a version stamp promising a section the ABI lacks.
         std::pair<int, int> merged_version = std::max(version_of(abi), version_of(other));
         const std::pair<int, int> declared_version = merged_version;
         // Inserted HERE, before any other section, because ojson preserves insertion order
         // and "version" is the first key of every ABI this toolchain has ever emitted.
         // The value is corrected in place below once promotion is known; overwriting an
         // existing key keeps its position, whereas assigning it late would move it to the
         // end of the object and change the bytes of every contract's ABI.
         ret["version"]  = merge_version(other);
         ret["types"]    = merge_types(other);
         ret["structs"]  = merge_structs(other);
         ret["actions"]  = merge_actions(other);
         ret["tables"]   = merge_tables(other);
         ret["ricardian_clauses"]  = merge_clauses(other);

         // A section belongs to the emitted document if it has content, and the emitted
         // VERSION is then raised to one that admits it.
         //
         // Master emitted `variants` unconditionally, so a contract with a std::variant
         // parameter built at -abi-version 1.0 got a document stamped 1.0 that nonetheless
         // carried a section the format introduced at 1.1 -- self-inconsistent, though not
         // lossy. Simply gating the section on the requested version would have made it
         // lossy: the struct field stays typed `variant_uint64_string` while the array
         // defining it disappears. Promoting the stamp keeps the document complete AND
         // consistent, and is what the protobuf path already does when it moves to 1.3.
         // Promote FIRST, from every gated section, then emit -- so the outcome does not
         // depend on the order the sections are considered in. (Emitting as we go meant a
         // populated action_results could raise the version to 1.2 after an empty variants
         // had already been skipped, leaving a 1.2 document missing a section 1.2 requires.)
         ojson variants_section = merge_variants(other);
         ojson results_section  = merge_action_results(other);
         if (!variants_section.empty() && variants_since)
            merged_version = std::max(merged_version, *variants_since);
         if (!results_section.empty() && action_results_since)
            merged_version = std::max(merged_version, *action_results_since);

         // A gated section is emitted when it has content, or when the version requires it to
         // be present -- an empty array is the correct representation in that second case,
         // and section() rejects a document that omits it. Below its version, absent.
         const auto emit_section = [&](const char* key, ojson section,
                                       const section_since& since) {
            if (!section.empty() || (since && merged_version >= *since))
               ret[key] = std::move(section);
         };
         emit_section("variants", std::move(variants_section), variants_since);
         emit_section("action_results", std::move(results_section), action_results_since);

         // Corrected in place (keeping its leading position) only if emit_section raised the
         // version above what either input declared. When nothing forced a promotion the
         // newer document's own version STRING stands -- parse ignores the namespace prefix,
         // so an inherited "eosio::abi/1.2" is valid and rewriting it to "sysio::abi/" would
         // be a silent change to every merged document.
         if (merged_version != declared_version)
            ret["version"] = abi_version::version_string(merged_version.first, merged_version.second);
         {
            ojson merged_enums = merge_enums(other);
            if (!merged_enums.empty())
               ret["enums"] = merged_enums;
         }
         return ret;
      }
   private:
      /// The (major, minor) a document declares.
      ///
      /// Every document reaching here carries a version: the constructor seeds an empty
      /// accumulator with one, merge() always stamps ret["version"], and abigen emits it in
      /// every descriptor. So a missing key is a malformed external document rather than the
      /// accumulator case, and defaulting it silently stamped the emission default onto input
      /// the base implementation rejected. An unparsable version is malformed for the same
      /// reason -- every capability gate below keys off this value.
      std::pair<int, int> version_of(const ojson& doc) const {
         if (!doc.has_key("version"))
            throw std::runtime_error("Error, ABI is missing its version");

         const auto text = doc["version"].as<std::string>();
         int major_v = 0;
         int minor_v = 0;
         if (!abi_version::parse_version_string(text, major_v, minor_v))
            throw std::runtime_error("Error, ABI declares an unsupported version : " + text);
         return {major_v, minor_v};
      }

      /// The newer of the two versions, canonicalised to this toolchain's namespace.
      ///
      /// Returning the winning document's raw string emitted whatever prefix it carried: a
      /// descriptor declaring "eosio::abi/1.10" produced a merged ABI stamped the same way,
      /// which Wire's abi_serializer rejects outright -- it requires "sysio::abi/1.". Version
      /// ORDERING ignores the prefix by design, so a foreign descriptor can still be ingested;
      /// what it must not do is leave the output undeployable.
      std::string merge_version(ojson b) {
         const auto winner = std::max(version_of(abi), version_of(b));
         return abi_version::version_string(winner.first, winner.second);
      }

      // Field order is significant: it is the serialization order, so {x,y} and {y,x} are
      // different wire layouts. The previous form matched by set membership plus size, so two
      // descriptors declaring the same struct with reordered fields merged as identical and
      // whichever .desc sorted first silently won -- a determinism hazard keyed on filename.
      static bool struct_is_same(ojson a, ojson b) {
         if (a["name"] != b["name"] || a["base"] != b["base"])
            return false;
         const auto& fa = a["fields"];
         const auto& fb = b["fields"];
         if (fa.size() != fb.size())
            return false;
         for (size_t i = 0; i < fa.size(); ++i)
            if (fa[i]["name"] != fb[i]["name"] || fa[i]["type"] != fb[i]["type"])
               return false;
         return true;
      }

      static bool type_is_same(ojson a, ojson b) {
         return a["new_type_name"] == b["new_type_name"] &&
                a["type"] == b["type"];
      }

      static bool action_is_same(ojson a, ojson b) {
         return a["name"] == b["name"] &&
                a["type"] == b["type"] &&
                a["ricardian_contract"] == b["ricardian_contract"];
      }


      // Length and order, like struct_is_same and like cdt-abidiff's find_variants. The
      // previous form asked only whether every type in `a` appeared somewhere in `b`, so
      // ["uint64"] and ["uint64","string"] compared equal: merging them kept the accumulator's
      // shorter list and dropped the `string` alternative outright, or -- with the descriptors
      // in the other order -- failed the build with "v already defined". Which of the two you
      // got was decided by sorted .desc filename order.
      static bool variant_is_same(ojson a, ojson b) {
         if (a["name"] != b["name"])
            return false;
         const auto& ta = a["types"];
         const auto& tb = b["types"];
         if (ta.size() != tb.size())
            return false;
         for (size_t i = 0; i < ta.size(); ++i)
            if (ta[i] != tb[i])
               return false;
         return true;
      }

      static bool table_is_same(ojson a, ojson b) {
         // key_names/key_types may differ: template-detected tables have them
         // populated while attribute-only tables have empty arrays. Both are
         // valid representations of the same table — treat as compatible.
         // Optional-key tolerant: an ABI from another toolchain need not carry the Wire
         // extensions (table_id, secondary_indexes) at all.
         const auto field = [](const ojson& o, const char* k) {
            static const ojson absent = ojson::null();
            return o.has_key(k) ? o[k] : absent;
         };
         // "Unspecified" is either an ABSENT key or an empty array, and the two are not
         // interchangeable in jsoncons: an absent key reads as null, and null.empty() is
         // FALSE while array.empty() is true, so testing empty() alone never fired for a
         // missing key. abigen writes secondary_indexes only when non-empty, so a
         // translation unit that sees a table's [[sysio::table]] but not its indexed
         // instantiation omits the key entirely -- and that TU's descriptor then failed to
         // merge with the one that has it, breaking multi-file contracts that master builds.
         const auto unspecified = [](const ojson& v) { return v.is_null() || v.empty(); };
         const auto compatible = [&](const char* k) {
            const ojson x = field(a, k);
            const ojson y = field(b, k);
            return x == y || unspecified(x) || unspecified(y);
         };
         return a["name"] == b["name"] &&
                a["type"] == b["type"] &&
                field(a, "index_type") == field(b, "index_type") &&
                // table_id is where the row physically lives and each secondary index carries
                // its own, so a difference in either is a different table -- not a merge.
                // These were omitted while cdt-abidiff's tables_match compared them, leaving
                // the differ and the merger disagreeing on table identity.
                field(a, "table_id") == field(b, "table_id") &&
                compatible("key_names") &&
                compatible("key_types") &&
                compatible("secondary_indexes");
      }

      static bool clause_is_same(ojson a, ojson b) {
         return a["id"] == b["id"] &&
                a["body"] == b["body"];
      }

      static bool action_result_is_same(ojson a, ojson b) {
         return a["name"] == b["name"] &&
                a["result_type"] == b["result_type"];
      }

      static bool enum_is_same(ojson a, ojson b) {
         return a["name"] == b["name"] &&
                a["type"] == b["type"] &&
                a["values"] == b["values"];
      }

      /// The version at which a section entered the format, or nullopt for one that is never
      /// mandatory.
      ///
      /// Absence is only legitimate below that version: abigen emits `variants` in every
      /// document and `action_results` in every document whose version supports it, so a 1.10
      /// descriptor missing either is truncated, not merely old. Treating them as optional at
      /// every version -- as an earlier revision did -- silently dropped contract interface
      /// content. `enums` is emitted only when non-empty, so it is genuinely optional
      /// everywhere and carries no threshold.
      using section_since = std::optional<std::pair<int, int>>;

      static constexpr std::pair<int, int> baseline_section{0, 0};
      static const section_since variants_since;
      static const section_since action_results_since;
      static const section_since never_mandatory;

      static const ojson& section(const ojson& doc, const std::string& type,
                                  const section_since& since, std::pair<int, int> doc_version) {
         static const ojson empty = ojson::array();
         if (doc.has_key(type))
            return doc[type];
         if (!since || doc_version < *since)
            return empty;
         throw std::runtime_error("Error, ABI at " +
                                  abi_version::version_string(doc_version.first, doc_version.second) +
                                  " is missing section : " + type);
      }

      template <typename F>
      void add_object_to_array(ojson& ret, ojson a, ojson b, std::string type, std::string id,
                               F&& is_same_func,
                               const section_since& since = section_since{baseline_section}) {
         for (auto obj_a : section(a, type, since, version_of(a)).array_range()) {
            ret.push_back(obj_a);
         }
         for (auto obj_b : section(b, type, since, version_of(b)).array_range()) {
            bool should_skip = false;
            for (size_t i = 0; i < ret.size(); ++i) {
               if (ret[i][id] == obj_b[id]) {
                  if (!is_same_func(ret[i], obj_b)) {
                     throw std::runtime_error(std::string("Error, ABI structs malformed : ")+ret[i][id].as<std::string>()+" already defined");
                  }
                  // Take the richer value for EACH optional list independently, rather than
                  // replacing the whole entry. Two earlier forms were both order-dependent:
                  // checking only key_names dropped a secondary_indexes the other side
                  // carried, and replacing wholesale on any of the three discarded whichever
                  // list the accumulator was richer in -- so two descriptors each rich in a
                  // different key produced a different result depending on which .desc
                  // sorted first. Per-key, the union is the same either way.
                  //
                  // is_same_func has already established these describe the same entity, so
                  // there is no conflict to resolve here: a populated list only ever fills
                  // in for an absent or empty one.
                  for (const char* k : {"key_names", "key_types", "secondary_indexes"}) {
                     const bool have_a = ret[i].count(k) && !ret[i][k].empty();
                     const bool have_b = obj_b.count(k) && !obj_b[k].empty();
                     if (!have_a && have_b)
                        ret[i][k] = obj_b[k];
                  }
                  should_skip = true;
               }
            }
            if (!should_skip)
               ret.push_back(obj_b);
         }
      }

      void add_object_to_object(ojson& ret, ojson a, ojson b, std::string type, std::string id) {
         for (auto obj_a : a[type].object_range()) {
            ret.insert_or_assign(obj_a.key(), obj_a.value());
         }
         for (auto obj_b : b[type].object_range()) {
            bool should_skip = false;
            for (auto obj_a : a[type].object_range()) {
               if (obj_a.key() == obj_b.key()) {
                  if (obj_a.value() != obj_b.value()) {
                     throw std::runtime_error(std::string("Error, ABI structs malformed : ")+std::string(obj_a.key().data(), obj_a.key().size())+" already defined");
                  }
                  else {
                     should_skip = true;
                  }
               }
            }
            if (!should_skip) {
               ret.insert_or_assign(obj_b.key(), obj_b.value());
            }
         }
      }

      ojson merge_structs(ojson b) {
         ojson structs = ojson::array();
         add_object_to_array(structs, abi, b, "structs", "name", struct_is_same);
         return structs;
      }

      ojson merge_types(ojson b) {
         ojson types = ojson::array();
         add_object_to_array(types, abi, b, "types", "new_type_name", type_is_same);
         return types;
      }

      ojson merge_variants(ojson b) {
         ojson vars = ojson::array();
         add_object_to_array(vars, abi, b, "variants", "name", variant_is_same, variants_since);
         return vars;
      }

      ojson merge_actions(ojson b) {
         ojson acts = ojson::array();
         add_object_to_array(acts, abi, b, "actions", "name", action_is_same);
         return acts;
      }

      ojson merge_tables(ojson b) {
         ojson tabs = ojson::array();
         add_object_to_array(tabs, abi, b, "tables", "name", table_is_same);
         return tabs;
      }

      ojson merge_clauses(ojson b) {
         ojson cls = ojson::array();
         add_object_to_array(cls, abi, b, "ricardian_clauses", "id", clause_is_same);
         return cls;
      }

      ojson merge_action_results(ojson b) {
         ojson res = ojson::array();
         add_object_to_array(res, abi, b, "action_results", "name", action_result_is_same, action_results_since);
         return res;
      }

      ojson merge_enums(ojson b) {
         ojson enums = ojson::array();
         if (abi.has_key("enums") || b.has_key("enums")) {
            if (!abi.has_key("enums")) abi["enums"] = ojson::array();
            if (!b.has_key("enums")) b["enums"] = ojson::array();
            add_object_to_array(enums, abi, b, "enums", "name", enum_is_same, never_mandatory);
         }
         return enums;
      }

      ojson abi;
};

// The fixed versions at which each section entered the format -- not default_major (what this
// toolchain emits by default) and not max_supported_major (the highest major it accepts).
// Deriving them from either makes a change to that unrelated knob silently move every
// threshold.
inline const ABIMerger::section_since ABIMerger::variants_since{
   std::pair<int, int>{abi_version::variants_major, abi_version::variants_minor}};
inline const ABIMerger::section_since ABIMerger::action_results_since{
   std::pair<int, int>{abi_version::action_results_major, abi_version::action_results_minor}};
inline const ABIMerger::section_since ABIMerger::never_mandatory{};

#pragma GCC diagnostic pop
