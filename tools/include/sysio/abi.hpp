#pragma once

#include <iostream>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <unordered_set>

/**
 * The ABI format version this toolchain emits.
 *
 * Single source of truth for every host tool and for the abigen plugin: cdt-cpp,
 * cdt-cc, cdt-ld, cdt-codegen and `sysio_abigen` all take their default from here,
 * so a standalone `cdt-codegen` run and an `add_contract()` build cannot stamp
 * different versions into a contract's `.abi`. Bumping the format is a one-line
 * change here plus a refresh of the `tests/toolchain/abigen-pass/<fixture>.abi`
 * fixtures, which pin the emitted string byte-for-byte.
 */
namespace abi_version {
   inline constexpr int default_major = 1;
   inline constexpr int default_minor = 2;

   /// "<major>.<minor>" -- the spelling accepted by the `-abi-version` driver flag
   /// and by the abigen plugin's `abi_version=` plugin argument.
   inline std::string spelling(int major_v, int minor_v) {
      return std::to_string(major_v) + "." + std::to_string(minor_v);
   }

   inline std::string default_spelling() { return spelling(default_major, default_minor); }

   /// The highest ABI major this toolchain can emit. `to_json` only knows how to
   /// serialize the 1.x shape, so accepting a higher major would stamp a version we
   /// cannot honour -- a 2.0 ABI would silently lose every section gated below.
   inline constexpr int max_supported_major = 1;

   /// The minor from which the `protobuf_types` ABI section is understood. A
   /// contract that emits one is bumped from the baseline to here; a contract that
   /// does not stays at the baseline.
   inline constexpr int protobuf_minor = 3;

   /// The version at which `variants` entered the format. A FIXED point in the format's
   /// history, unrelated to max_supported_major, which is only the highest major this
   /// toolchain accepts. Deriving one from the other made raising the accepted maximum move
   /// every introduction with it -- supports_variants(1, 10) would become false while parse()
   /// still accepted major 1, letting valid 1.x documents omit sections they require.
   inline constexpr int variants_major = 1;
   inline constexpr int variants_minor = 1;

   /// The version at which `action_results` entered the format.
   inline constexpr int action_results_major = 1;
   inline constexpr int action_results_minor = 2;

   /// Does a version carry the `variants` section?
   ///
   /// Provided for symmetry with `supports_action_results`; nothing calls it today, because
   /// ABIMerger gates on the introduction pair directly and abigen emits `variants`
   /// unconditionally.
   inline constexpr bool supports_variants(int major_v, int minor_v) {
      return major_v > variants_major ||
             (major_v == variants_major && minor_v >= variants_minor);
   }

   /// Does a version carry the `action_results` section?
   ///
   /// Used by the abigen plugin to decide whether to emit the section. ABIMerger answers the
   /// same question from the introduction constants above rather than through this predicate,
   /// because it needs the (major, minor) pair itself to promote a merged document's version;
   /// both therefore read the one rule declared here. Two spellings of that rule is how a
   /// contract ends up with a version stamp promising a section its ABI does not carry.
   ///
   /// cdt-abidiff does NOT gate on it -- it diffs every section unconditionally, since a
   /// section present in one document and absent from the other is exactly the difference it
   /// exists to report, whatever version either side declares.
   inline constexpr bool supports_action_results(int major_v, int minor_v) {
      return major_v > action_results_major ||
             (major_v == action_results_major && minor_v >= action_results_minor);
   }

   /// The full "sysio::abi/<major>.<minor>" string stamped into a contract's ABI.
   inline std::string version_string(int major_v, int minor_v) {
      return "sysio::abi/" + spelling(major_v, minor_v);
   }

   /**
    * Parse a "<major>" or "<major>.<minor>" spelling.
    *
    * Integer parsing throughout: the previous float round-trip
    * (`(int)((stof(v) - (int)stof(v)) * 10)`) truncated on any minor whose decimal
    * expansion falls short in binary -- "1.3" parsed as minor 2 -- which silently
    * desynced the version handed to the plugin from the one handed to ABIMerger.
    *
    * A zero major is rejected: there is no ABI 0.x.
    *
    * That rule once carried a second job, worth recording because it is why the driver can
    * be as simple as it now is. cdt-cpp USED to read a zero major as "the option was never
    * given", so accepting one would have let `cdt-cpp -abi-version 0.1` fall back to the
    * default while `cdt-codegen --abi-version 0.1` honoured it -- reintroducing exactly the
    * divergence this namespace exists to remove. That sentinel is retired: the driver now
    * records and forwards the parsed version unconditionally
    * (tools/cc/cdt-cpp.cpp.in), which it can do precisely BECAUSE zero never survives this
    * parse. Keeping the rejection is what stops the sentinel being needed again.
    *
    * A major above max_supported_major is rejected too. to_json only knows the 1.x
    * shape and gates action_results on major == 1, so a 2.0 or 10.2 request would
    * otherwise be accepted, compared as "newer than 1.2" by the merger, and then
    * emitted without the very sections the higher version implies.
    *
    * @param text      the spelling to parse
    * @param major_out set to the major component on success; untouched on failure
    * @param minor_out set to the minor component on success (0 when omitted);
    *                  untouched on failure
    * @return true when @p text is a well-formed version, false otherwise. Callers
    *         are expected to emit a diagnostic and exit non-zero on false rather
    *         than proceeding with a partially-parsed version.
    */
   inline bool parse(const std::string& text, int& major_out, int& minor_out) {
      if (text.empty())
         return false;

      const auto dot = text.find('.');
      const std::string major_text = text.substr(0, dot);
      const std::string minor_text = (dot == std::string::npos) ? std::string("0")
                                                                : text.substr(dot + 1);

      // Reject anything std::stoi would otherwise accept by prefix ("1x", " 1", "1.2.3").
      auto all_digits = [](const std::string& v) {
         return !v.empty() && v.find_first_not_of("0123456789") == std::string::npos;
      };
      if (!all_digits(major_text) || !all_digits(minor_text))
         return false;

      int major_v = 0;
      int minor_v = 0;
      try {
         major_v = std::stoi(major_text);
         minor_v = std::stoi(minor_text);
      } catch (const std::exception&) {
         return false;   // out of int range
      }
      if (major_v == 0 || major_v > max_supported_major)
         return false;

      major_out = major_v;
      minor_out = minor_v;
      return true;
   }

   /**
    * Parse the "<ns>::abi/<major>.<minor>" string stamped into a contract's ABI.
    *
    * The namespace prefix is not inspected, so a descriptor carrying an inherited
    * `eosio::abi/1.2` parses the same as a `sysio::abi/1.2` one. Everything up to
    * and including the last '/' is dropped and the remainder handed to parse();
    * a string with no '/' is parsed whole.
    *
    * @param text      the version string to parse
    * @param major_out set to the major component on success; untouched on failure
    * @param minor_out set to the minor component on success; untouched on failure
    * @return true when @p text carries a well-formed version, false otherwise
    */
   inline bool parse_version_string(const std::string& text, int& major_out, int& minor_out) {
      const auto slash = text.rfind('/');
      return parse(slash == std::string::npos ? text : text.substr(slash + 1),
                   major_out, minor_out);
   }
} // namespace abi_version

struct abi_typedef {
   std::string new_type_name;
   std::string type;
   bool operator<(const abi_typedef& t) const { return new_type_name < t.new_type_name; }
};

struct abi_field {
   std::string name;
   std::string type;
};

struct abi_struct {
   std::string name;
   std::string base;
   std::vector<abi_field> fields;
   bool operator<(const abi_struct& s) const { return name < s.name; }
};

struct abi_action {
   std::string name;
   std::string type;
   std::string ricardian_contract;
   bool operator<(const abi_action& s) const { return name < s.name; }
};

struct abi_secondary_index {
   std::string name;
   std::string key_type;
   uint16_t    table_id = 0;
};

struct abi_table {
   std::string name;
   std::string type;
   std::string index_type;
   std::vector<std::string> key_names;
   std::vector<std::string> key_types;
   uint16_t table_id = 0;
   std::vector<abi_secondary_index> secondary_indexes;
   /// Qualified name of the row struct this table was instantiated over, e.g. "ns1::config_row".
   /// Descriptor-only (emitted as ____row, stripped from the ABI): it is how cdt-codegen matches
   /// a [[sysio::table("name")]] to its tables across translation units, which `type` cannot do
   /// -- ns1::row and ns2::row both serialise as `row`. Empty when the row type is not a class.
   std::string row;
   bool operator<(const abi_table& t) const { return name < t.name; }
};

/// A [[sysio::table("name")]] on a row struct, carried in the descriptor rather than applied.
///
/// The annotation renames the table published over that struct, but only when the struct backs
/// exactly one and the name is free -- and both are link-wide facts. A single translation unit
/// that renames on its own partial view produces descriptors that disagree, which the merge then
/// refuses. So abigen records the annotation and cdt-codegen applies it after the merge.
struct abi_table_annotation {
   std::string name;   ///< the annotation's argument: the ABI name it asks for
   std::string type;   ///< the row struct's ABI type name
   std::string row;    ///< the row struct's qualified name, matching abi_table::row
   std::string loc;    ///< source location of the annotated struct, for diagnostics
   std::vector<std::string> key_names;  ///< resolved [[sysio::kv_key]] override, if any
   std::vector<std::string> key_types;
   bool operator<(const abi_table_annotation& a) const { return name < a.name; }
};

struct abi_ricardian_clause_pair {
   std::string id;
   std::string body;
};

struct abi_variant {
   std::string name;
   std::vector<std::string> types;
   bool operator<(const abi_variant& t) const { return name < t.name; }
};

struct abi_error_message {
   uint64_t    error_code;
   std::string error_msg;
};

struct wasm_action {
   std::string name;
   std::string handler;
};

struct wasm_notify {
   std::string name;
   std::string contract;
   std::string handler;
};

namespace std {
   template<>
   struct less<wasm_action> {
      bool operator()(const wasm_action& lhs, const wasm_action& rhs) const {
         return lhs.name < rhs.name;
      }
   };

   template<>
   struct less<wasm_notify> {
      bool operator()(const wasm_notify& lhs, const wasm_notify& rhs) const {
         if (lhs.name == rhs.name) {
            if (lhs.contract == "*" && rhs.contract != "*") {
               return false;
            } else if (lhs.contract != "*" && rhs.contract == "*") {
               return true;
            }
         }
         return std::tie(lhs.name, lhs.contract) < std::tie(rhs.name, rhs.contract);
      }
   };
}

struct abi_enum_value {
   std::string name;
   int64_t     value;
};

struct abi_enum {
   std::string name;
   std::string type;  // underlying type, e.g. "uint8"
   std::vector<abi_enum_value> values;
   bool operator<(const abi_enum& e) const { return name < e.name; }
};

struct abi_action_result {
   std::string name;
   std::string type;
   bool operator<(const abi_action_result& ar) const { return name < ar.name; }
};

/// From sysio libraries/chain/include/sysio/chain/abi_def.hpp
struct abi {
   int version_major = abi_version::default_major;
   int version_minor = abi_version::default_minor;
   std::string version_string()const { return abi_version::version_string(version_major, version_minor); }
   std::set<abi_struct>                   structs;
   std::set<abi_typedef>                  typedefs;
   std::set<abi_action>                   actions;
   std::set<abi_table>                    tables;
   std::set<abi_table_annotation>         table_annotations;
   std::set<abi_variant>                  variants;
   std::set<abi_enum>                     enums;
   std::vector<abi_ricardian_clause_pair> ricardian_clauses;
   std::vector<abi_error_message>         error_messages;
   std::set<wasm_action>                  wasm_actions;
   std::set<wasm_notify>                  wasm_notifies;
   std::set<std::string>                  wasm_entries;
   std::set<abi_action_result>            action_results;
   bool                                   has_pre_dispatch  = false;
   bool                                   has_post_dispatch = false;
};

inline void dump( const abi& abi ) {
   std::cout << "ABI : ";
   std::cout << "\n\tversion : " << abi.version_string();
   std::cout << "\n\tstructs : ";
   for (auto s : abi.structs) {
      std::cout << "\n\t\tstruct : ";
      std::cout << "\n\t\t\tname : " << s.name;
      std::cout << "\n\t\t\tbase : " << s.base;
      std::cout << "\n\t\t\tfields : ";
      for (auto f : s.fields) {
         std::cout << "\n\t\t\t\tfield : ";
         std::cout << "\n\t\t\t\t\tname : " << f.name;
         std::cout << "\n\t\t\t\t\ttype : " << f.type << '\n';
      }
   }
}
