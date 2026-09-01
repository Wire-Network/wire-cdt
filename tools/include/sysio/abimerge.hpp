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
         const std::pair<int, int> merged_version = std::max(version_of(abi), version_of(other));
         ret["version"]  = merge_version(other);
         ret["types"]    = merge_types(other);
         ret["structs"]  = merge_structs(other);
         ret["actions"]  = merge_actions(other);
         ret["tables"]   = merge_tables(other);
         ret["ricardian_clauses"]  = merge_clauses(other);
         // Both version-gated sections consult the MERGED version, and both are compared as
         // parsed components: deriving them from the string's last three characters mis-read
         // any two-digit minor ("sysio::abi/1.10" -> ".10"). variants was previously emitted
         // unconditionally, so a 1.0 document merged at 1.0 produced a 1.0 ABI carrying a
         // variants array -- contradicting the variants_since rule declared below.
         if (abi_version::supports_variants(merged_version.first, merged_version.second)) {
            ret["variants"] = merge_variants(other);
         }
         if (abi_version::supports_action_results(merged_version.first, merged_version.second)) {
            ret["action_results"] = merge_action_results(other);
         }
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

      std::string merge_version(ojson b) {
         return version_of(abi) < version_of(b) ? b["version"].as<std::string>()
                                                : abi["version"].as<std::string>();
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
         const auto compatible = [](const ojson& x, const ojson& y) {
            return x == y || x.empty() || y.empty();
         };
         return a["name"] == b["name"] &&
                a["type"] == b["type"] &&
                a["index_type"] == b["index_type"] &&
                compatible(a["key_names"], b["key_names"]) &&
                compatible(a["key_types"], b["key_types"]);
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
                  // Prefer the entry with richer key metadata (non-empty key_names)
                  if (ret[i].count("key_names") && obj_b.count("key_names") &&
                      ret[i]["key_names"].empty() && !obj_b["key_names"].empty()) {
                     ret[i] = obj_b;
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

// max_supported_major, not default_major: these say which version of the FORMAT introduced the
// section, which is a property of the format, not of what this toolchain happens to emit by
// default. The two are equal today, so bumping the emission default would silently move every
// threshold with it.
inline const ABIMerger::section_since ABIMerger::variants_since{
   std::pair<int, int>{abi_version::max_supported_major, abi_version::variants_minor}};
inline const ABIMerger::section_since ABIMerger::action_results_since{
   std::pair<int, int>{abi_version::max_supported_major, abi_version::action_results_minor}};
inline const ABIMerger::section_since ABIMerger::never_mandatory{};

#pragma GCC diagnostic pop
