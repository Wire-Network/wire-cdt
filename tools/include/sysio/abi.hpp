#pragma once

#include <iostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include <unordered_set>

/**
 * The ABI format version this toolchain emits.
 *
 * Single source of truth for every host tool and for the abigen plugin: cdt-cpp,
 * cdt-cc, cdt-ld, cdt-codegen and `sysio_abigen` all take their default from here,
 * so a standalone `cdt-codegen` run and an `add_contract()` build cannot stamp
 * different versions into a contract's `.abi`. Bumping the format is a one-line
 * change here plus a refresh of the `tests/toolchain/abigen-pass/*.abi` fixtures,
 * which pin the emitted string byte-for-byte.
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

   /// The minor from which the `protobuf_types` ABI section is understood. A
   /// contract that emits one is bumped from the baseline to here; a contract that
   /// does not stays at the baseline.
   inline constexpr int protobuf_minor = 3;

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

      try {
         major_out = std::stoi(major_text);
         minor_out = std::stoi(minor_text);
      } catch (const std::exception&) {
         return false;   // out of int range
      }
      return true;
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
   bool operator<(const abi_table& t) const { return name < t.name; }
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
