#pragma once
#include <algorithm>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendPluginRegistry.h>

#include "gen.hpp"
#include "ppcallbacks.hpp"
#include <sysio/abi.hpp>

// DJB2 hash of 8 big-endian bytes of a uint64_t, truncated to uint16.
// Must match sysio::kv::compute_table_id in kv_constants.hpp.

// DJB2 initial hash seed (canonical value from Daniel J. Bernstein's hash function).
static constexpr uint64_t djbh_seed = 5381;

// DJB2-hash the 8 big-endian bytes of a uint64_t, optionally continuing from an existing hash.
static inline uint64_t djbh_hash_raw(uint64_t raw, uint64_t hash = djbh_seed) {
   for (int i = 0; i < 8; ++i)
      // hash * 33 (2^5 + 1), then add byte
      hash = ((hash << 5) + hash) + static_cast<uint8_t>(raw >> (56 - i * 8));
   return hash;
}

// Narrowing cast to uint16_t truncates to the low 16 bits (well-defined for unsigned).
static inline uint16_t compute_table_id_from_raw(uint64_t raw) {
   return static_cast<uint16_t>(djbh_hash_raw(raw));
}

// Must match sysio::kv::compute_sec_table_id in kv_constants.hpp.
// Chains two 8-byte DJB2 passes: first the table bytes, then the index bytes.
static inline uint16_t compute_sec_table_id_from_raw(uint64_t table_raw, uint64_t index_raw) {
   return static_cast<uint16_t>(djbh_hash_raw(index_raw, djbh_hash_raw(table_raw)));
}

// Must match sysio::kv::compute_mi_sec_table_id in kv_constants.hpp.
// multi_index/kv_multi_index uses positional indices (index_pos + 1 as synthetic raw).
static inline uint16_t compute_mi_sec_table_id_from_raw(uint64_t table_raw, uint8_t index_pos) {
   return compute_sec_table_id_from_raw(table_raw, static_cast<uint64_t>(index_pos) + 1);
}


#include <fstream>
#include <jsoncons/json.hpp>

using namespace clang;
using namespace sysio::cdt;
using jsoncons::json;
using jsoncons::ojson;

extern std::string output;

namespace sysio { namespace cdt {
   class abigen : public generation_utils {
      std::set<std::string> checked_actions;
      std::set<std::string> kv_key_structs; // structs referenced by [[sysio::kv_key]], must survive validate_struct
   public:
      using generation_utils::generation_utils;

      clang::SourceManager* source_manager = nullptr;

      bool no_abigen = false;

      static abigen& get() {
         static abigen ag;
         return ag;
      }

      void set_abi_version(int major, int minor) {
         _abi.version_major = major;
         _abi.version_minor = minor;
      }

      void add_typedef( const clang::QualType& t ) {
         abi_typedef ret;
         ret.new_type_name = get_base_type_name( t );
         auto td = get_type_alias(t);
         if (td.empty())
            return;
         ret.type = translate_type(td[0]);
         if(!is_builtin_type(td[0]))
            add_type(td[0]);
         _abi.typedefs.insert(ret);
      }

      template<typename T>
      void add_wasm_action(const clang_wrapper::Decl<T>& decl, const std::string& handler) {
         wasm_action ret;
         ret.name = get_action_name(decl);
         ret.handler = handler;
         _abi.wasm_actions.insert(ret);
      }

      template<typename T>
      void add_wasm_notify(const clang_wrapper::Decl<T>& decl, const std::string& handler) {
         wasm_notify ret;
         auto str = get_notify_pair(decl);
         auto pos = str.find("::");
         if (pos == std::string::npos) {
            std::cerr << "Error, the argument of sysio::on_notify attribute should have separator '::'" << std::endl;
            throw;
         }
         ret.contract = str.substr(0, pos);
         ret.name = str.substr(pos+2);
         ret.handler = handler;
         _abi.wasm_notifies.insert(ret);
      }

      template<typename T>
      void add_wasm_entries(const clang_wrapper::Decl<T>& decl) {
         if (const auto* Attr = decl->template getAttr<clang::WebAssemblyExportNameAttr>()) {
            _abi.wasm_entries.insert(Attr->getExportName().str());
         }
      }

      void add_action( const clang::CXXRecordDecl* _decl ) {
         auto decl = clang_wrapper::wrap_decl(_decl);
         abi_action ret;
         auto action_name = decl.getSysioActionAttr()->getName();

         if (!checked_actions.insert(get_action_name(decl)).second)
            if (!suppress_ricardian_warnings)
               CDT_CHECK_WARN(!rcs[get_action_name(decl)].empty(), "abigen_warning", decl->getLocation(), "Action <"+get_action_name(decl)+"> does not have a ricardian contract");

         ret.ricardian_contract = rcs[get_action_name(decl)];

         if (action_name.empty()) {
            validate_name(decl->getName().str(), [&](auto s) { CDT_ERROR("abigen_error", decl->getLocation(), s); });
            ret.name = decl->getName().str();
         }
         else {
            validate_name( action_name.str(), [&](auto s) { CDT_ERROR("abigen_error", decl->getLocation(), s); });
            ret.name = action_name.str();
         }
         ret.type = decl->getName().str();
         _abi.actions.insert(ret);
      }

      void add_action( const clang::CXXMethodDecl* _decl ) {
         auto decl = clang_wrapper::wrap_decl(_decl);
         abi_action ret;

         auto action_name = decl.getSysioActionAttr()->getName();

         if (!checked_actions.insert(get_action_name(decl)).second)
            if (!suppress_ricardian_warnings)
               CDT_CHECK_WARN(!rcs[get_action_name(decl)].empty(), "abigen_warning", decl->getLocation(), "Action <"+get_action_name(decl)+"> does not have a ricardian contract");

         ret.ricardian_contract = rcs[get_action_name(decl)];

         if (action_name.empty()) {
            validate_name( decl->getNameAsString(), [&](auto s) { CDT_ERROR("abigen_error", decl->getLocation(), s); } );
            ret.name = decl->getNameAsString();
         }
         else {
            validate_name( action_name.str(), [&](auto s) { CDT_ERROR("abigen_error", decl->getLocation(), s); } );
            ret.name = action_name.str();
         }
         // When a single pb<T> parameter, point action type directly at protobuf type
         if (is_single_pb_param(_decl)) {
            auto param_type = _decl->parameters()[0]->getType().getNonReferenceType().getUnqualifiedType();
            ret.type = translate_type(param_type);
         } else {
            ret.type = decl->getNameAsString();
         }
         _abi.actions.insert(ret);
         // Handle action return types
         if (translate_type(decl->getReturnType()) != "void") {
            add_type(decl->getReturnType());
            _abi.action_results.insert({get_action_name(decl), translate_type(decl->getReturnType())});
         }
      }

      void add_tuple(const clang::QualType& type) {
         auto pt = llvm::dyn_cast<clang::ElaboratedType>(type.getTypePtr());
         auto tst = llvm::dyn_cast<clang::TemplateSpecializationType>((pt) ? pt->desugar().getTypePtr() : type.getTypePtr());
         if (!tst) {
            CDT_INTERNAL_ERROR("template specialization failure");
         }
         abi_struct tup;
         tup.name = get_type(type);
         for (int i = 0; i < tst->template_arguments().size(); ++i) {
            clang::QualType ftype = std::get<clang::QualType>(get_template_argument(type, i));
            add_type(ftype);
            tup.fields.push_back( {"field_"+std::to_string(i),
                  translate_type(ftype)} );
         }
         _abi.structs.insert(tup);
      }

      void add_pair(const clang::QualType& type) {
         for (int i = 0; i < 2; ++i) {
            clang::QualType ftype = std::get<clang::QualType>(get_template_argument(type, i));
            std::string ty = translate_type(ftype);
            add_type(ftype);
         }
         abi_struct pair;
         pair.name = get_type(type);
         pair.fields.push_back( {"first", get_template_argument_as_string(type)} );
         pair.fields.push_back( {"second", get_template_argument_as_string(type, 1)} );
         add_type(std::get<clang::QualType>(get_template_argument(type)));
         add_type(std::get<clang::QualType>(get_template_argument(type, 1)));
         _abi.structs.insert(pair);
      }

      void add_map(const clang::QualType& type) {
         for (int i = 0; i < 2; ++i) {
            add_type(std::get<clang::QualType>(get_template_argument(type, i)));
         }
         abi_struct map_info;
         std::string name = get_type(type);
         map_info.name = name.substr(0, name.length() - 2);
         auto remove_ending_brackets = [&]( std::string name ) {
            int i = name.length()-1;
            for (; i >= 0; i--)
               if ( name[i] != '[' && name[i] != ']' )
                  break;
            return name.substr(0,i+1);
         };
         map_info.name = remove_ending_brackets(name);
         map_info.fields.push_back( {"first", get_template_argument_as_string(type)} );
         map_info.fields.push_back( {"second", get_template_argument_as_string(type, 1)} );
         add_type(std::get<clang::QualType>(get_template_argument(type)));
         add_type(std::get<clang::QualType>(get_template_argument(type, 1)));
         _abi.structs.insert(map_info);
      }

      void add_struct( const clang::CXXRecordDecl* decl, const std::string& rname="" ) {
         abi_struct ret;
         if ( decl->getNumBases() == 1 ) {
            ret.base = get_type(decl->bases_begin()->getType());
            add_type(decl->bases_begin()->getType());
         }
         for ( auto field : decl->fields() ) {
            if ( field->getName() == "transaction_extensions") {
               abi_struct ext;
               ext.name = "extension";
               ext.fields.push_back( {"type", "uint16"} );
               ext.fields.push_back( {"data", "bytes"} );
               ret.fields.push_back( {"transaction_extensions", "extension[]"});
               _abi.structs.insert(ext);
            }
            else {
               ret.fields.push_back({field->getName().str(), get_type(field->getType())});
               add_type(field->getType());
            }
         }
         if (!rname.empty())
            ret.name = rname;
         else
            ret.name = decl->getName().str();

         const auto res = _abi.structs.insert(ret);
      }

      // Check if an action method has a single pb<T> parameter, allowing the ABI
      // to point directly at the protobuf type instead of generating a wrapper struct.
      bool is_single_pb_param( const clang::CXXMethodDecl* decl ) {
         if (decl->param_size() != 1)
            return false;
         auto param_type = decl->parameters()[0]->getType().getNonReferenceType().getUnqualifiedType();
         return is_template_specialization(param_type, {"pb"});
      }

      void add_struct( const clang::CXXMethodDecl* decl ) {
         // When an action has a single pb<T> parameter, skip generating the wrapper
         // struct — the action type points directly at the protobuf type.
         if (is_single_pb_param(decl)) {
            add_type(decl->parameters()[0]->getType().getNonReferenceType().getUnqualifiedType());
            return;
         }
         abi_struct new_struct;
         new_struct.name = decl->getNameAsString();
         for (auto param : decl->parameters() ) {
            auto param_type = param->getType().getNonReferenceType().getUnqualifiedType();
            new_struct.fields.push_back({param->getNameAsString(), get_type(param_type)});
            add_type(param_type);
         }
         _abi.structs.insert(new_struct);
      }

      void add_table( const clang::CXXRecordDecl* _decl ) {
         auto decl = clang_wrapper::wrap_decl(_decl);
         auto table_name = decl.getSysioTableAttr()->getName();
         // A bare [[sysio::table]] names no table, so there is nothing to describe here: the
         // name, the table_id and the key layout all come from whatever multi_index / kv::table
         // instantiates this struct, and each instantiation emits its own entry. Naming an entry
         // after the ROW STRUCT produced a second table beside the real one, under a table_id
         // nothing ever writes to.
         //
         // Emitting it and pruning later was tried and abandoned: the placeholder cannot be
         // told apart from a real table by anything that survives into the descriptor. It
         // collides in the by-name set with an instantiation that happens to share the struct's
         // name, so the marked entry IS sometimes the live table; and the emitted `type` string
         // cannot distinguish two declarations that share an unqualified name. Both led to a
         // live table being deleted. Not creating it is the only form with nothing to
         // disambiguate.
         //
         // Consequence: a struct annotated but never instantiated gets no table entry. It
         // describes a table nothing can read or write, and nothing in wire-sysio relies on it.
         if (table_name.empty())
            return;
         // Recorded, not applied. Whether this names a table -- and which -- depends on how many
         // tables are instantiated over the struct link-wide, which no single translation unit
         // knows. cdt-codegen decides after the descriptors merge; see abi_table_annotation.
         abi_table_annotation t;
         t.type = decl->getNameAsString();
         // No 13-char restriction -- an `_i` literal's name is hashed, not encoded, so it may be
         // as long as it likes. The CHARSET is restricted though, to what a C++ identifier
         // allows: this name is the table's public label, and it travels out through the ABI to
         // wire-sysio, SHiP and Hyperion, none of which should have to carry whatever the
         // attribute happened to be written with.
         t.name = table_name.str();
         CDT_CHECK_ERROR(!t.name.empty() &&
                         std::all_of(t.name.begin(), t.name.end(), [](unsigned char c) {
                            return std::isalnum(c) || c == '_';
                         }), "abigen_error", _decl->getLocation(),
            "[[sysio::table(\"" + t.name + "\")]] is not a usable table name; use only letters, "
            "digits and underscore");
         t.row  = _decl->getQualifiedNameAsString();
         t.loc  = _decl->getLocation().printToString(_decl->getASTContext().getSourceManager());

         // [[sysio::kv_key("struct_name")]] — resolve key struct fields into key_names/key_types
         if (decl.isSysioKvKey()) {
            auto key_struct_name = decl.getSysioKvKeyAttr()->getName().str();
            if (key_struct_name.empty()) {
               // No argument: standard scoped key layout [scope:8B][pk:8B]
               t.key_names = {"scope", "primary_key"};
               t.key_types = {"name", "uint64"};
            } else {
               // Search for key struct: nested types, then enclosing class
               const clang::CXXRecordDecl* key_record = nullptr;
               // Check nested types within the table struct
               for (auto* d : _decl->decls()) {
                  if (auto* r = llvm::dyn_cast<clang::CXXRecordDecl>(d)) {
                     if (r->getNameAsString() == key_struct_name && r->isCompleteDefinition()) {
                        key_record = r; break;
                     }
                  }
               }
               // Check enclosing class (contract)
               if (!key_record) {
                  if (auto* ctx = _decl->getDeclContext()) {
                     for (auto* d : ctx->decls()) {
                        if (auto* r = llvm::dyn_cast<clang::CXXRecordDecl>(d)) {
                           if (r->getNameAsString() == key_struct_name && r->isCompleteDefinition()) {
                              key_record = r; break;
                           }
                        }
                     }
                  }
               }

               if (key_record) {
                  // Extract field names/types for key_names/key_types
                  for (auto* field : key_record->fields()) {
                     t.key_names.push_back(field->getName().str());
                     t.key_types.push_back(translate_type(field->getType()));
                  }
                  // Add the key struct to ABI structs so clients can reference it
                  kv_key_structs.insert(key_struct_name);
                  abi_struct ks;
                  ks.name = key_struct_name;
                  for (auto* field : key_record->fields()) {
                     ks.fields.push_back({field->getName().str(), get_type(field->getType())});
                     add_type(field->getType());
                  }
                  _abi.structs.insert(ks);
               } else {
                  // Fallback: check already-processed ABI structs
                  bool found = false;
                  for (const auto& s : _abi.structs) {
                     if (s.name == key_struct_name) {
                        for (const auto& f : s.fields) {
                           t.key_names.push_back(f.name);
                           t.key_types.push_back(f.type);
                        }
                        found = true;
                        break;
                     }
                  }
                  CDT_CHECK_WARN(found, "abigen_warning", _decl->getLocation(),
                     "kv_key struct '" + key_struct_name + "' not found; key_names/key_types will be empty in ABI");
               }
            }
         }

         _abi.table_annotations.insert(t);
      }

      enum class kv_table_kind { legacy, kv_standard, kv_global };

      /// Extract a single abi_secondary_index from an indexed_by<Name, Extractor> or
      /// kv::index<Name, Extractor> template specialization. Returns false if the
      /// argument is not a valid index template specialization.
      ///
      /// \p arg   the template argument that should be the indexed_by/kv::index type
      /// \p tid   precomputed table_id (caller decides positional vs hashed formula)
      /// \p out   populated with name, key_type, table_id on success
      bool extract_secondary_index(const clang::TemplateArgument& arg,
                                   uint16_t tid,
                                   abi_secondary_index& out) {
         if (arg.getKind() != clang::TemplateArgument::Type) return false;
         auto* record = arg.getAsType().getTypePtr()->getAsCXXRecordDecl();
         auto* idx_spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
         if (!idx_spec || idx_spec->getTemplateArgs().size() < 1) return false;
         auto first_arg = idx_spec->getTemplateArgs()[0];
         if (first_arg.getKind() != clang::TemplateArgument::Integral) return false;
         auto idx_name_raw = first_arg.getAsIntegral().getLimitedValue();
         out.name = name_to_string(idx_name_raw);
         out.table_id = tid;
         // Determine key type from extractor (e.g. const_mem_fun<T, KeyType, Ptr>).
         // The extractor's KeyType is the second template arg of const_mem_fun/member_data.
         if (idx_spec->getTemplateArgs().size() >= 2) {
            auto ext_arg = idx_spec->getTemplateArgs()[1];
            if (ext_arg.getKind() == clang::TemplateArgument::Type) {
               auto* ext_record = ext_arg.getAsType().getTypePtr()->getAsCXXRecordDecl();
               if (ext_record) {
                  auto* ext_spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(ext_record);
                  if (ext_spec && ext_spec->getTemplateArgs().size() >= 2) {
                     out.key_type = translate_type(ext_spec->getTemplateArgs()[1].getAsType());
                  }
               }
            }
         }
         if (out.key_type.empty()) out.key_type = "bytes";
         return true;
      }

      /// Add a kv::table<Name, K, V> — extracts key metadata from K, uses V as row type.
      /// If V has [[sysio::kv_key("struct")]] annotation, that overrides K's fields.
      void add_kv_table( uint64_t name, const clang::CXXRecordDecl* key_decl, const clang::QualType& value,
                         std::vector<abi_secondary_index> sec_indexes, bool scoped ) {
         const auto* val_decl = value.getTypePtr()->getAsCXXRecordDecl();
         abi_table t;
         // The ABI type name, not the C++ record name -- see add_table() for why they differ.
         t.type = get_type(value);
         t.table_id     = compute_table_id_from_raw(name);
         t.has_table_id = true;

         // The table parameter is the name. A [[sysio::table("name")]] on V may still rename it,
         // link-wide, in cdt-codegen; `row` is what pairs the two up.
         t.name = name_to_string(name);
         if (val_decl)
            t.row = val_decl->getQualifiedNameAsString();

         // Use [[sysio::kv_key("struct")]] from V if present, otherwise auto-derive from K.
         // Only a class can carry the attribute, and V need not be one -- the row type check
         // that reports a non-describable V runs after this, so a scalar V arrives here first.
         auto val_wrap = clang_wrapper::wrap_decl(val_decl);
         const clang::CXXRecordDecl* key_source = key_decl;
         if (val_decl && val_wrap.isSysioKvKey()) {
            auto kv_key_name = val_wrap.getSysioKvKeyAttr()->getName().str();
            if (!kv_key_name.empty()) {
               // Nested types first, then the enclosing context -- the same order add_table
               // uses for the identical attribute. Searching only the enclosing context missed
               // a key struct declared INSIDE the value row, and silently fell back to the
               // physical key, so the ABI advertised the physical field names while the
               // annotation named a logical override.
               const clang::CXXRecordDecl* override_key = nullptr;
               for (auto* d : val_decl->decls()) {
                  if (auto* r = llvm::dyn_cast<clang::CXXRecordDecl>(d)) {
                     if (r->getNameAsString() == kv_key_name && r->isCompleteDefinition()) {
                        override_key = r; break;
                     }
                  }
               }
               if (!override_key) {
                  if (auto* ctx = val_decl->getDeclContext()) {
                     for (auto* d : ctx->decls()) {
                        if (auto* r = llvm::dyn_cast<clang::CXXRecordDecl>(d)) {
                           if (r->getNameAsString() == kv_key_name && r->isCompleteDefinition()) {
                              override_key = r; break;
                           }
                        }
                     }
                  }
               }
               if (override_key) {
                  key_source = override_key;
                  // Protect it from validate_struct, as the other path does for its own -- and
                  // DECLARE it, which protecting alone does not do. An entry kept in a set it
                  // never joined is nothing: the override struct was never emitted and its
                  // fields never ran through add_type, so a key field of a contract type left
                  // the ABI naming a type the document does not define. A bare
                  // [[sysio::table, sysio::kv_key("k")]] published key_types ["logical_id"]
                  // with neither `k` nor `logical_id` in structs, and query-key decoding had
                  // nothing to resolve. add_table's identical branch has always done both;
                  // this one only ever did half, and the bare attribute returns before
                  // reaching that branch at all.
                  kv_key_structs.insert(kv_key_name);
                  abi_struct ks;
                  ks.name = kv_key_name;
                  for (auto* field : override_key->fields()) {
                     ks.fields.push_back({field->getName().str(), get_type(field->getType())});
                     add_type(field->getType());
                  }
                  _abi.structs.insert(ks);
               } else {
                  // An ERROR, not a warning: this is the translation unit that instantiates the
                  // table, so it is the one whose key layout reaches the ABI. Falling back to
                  // the physical key published a layout the author did not ask for, and no
                  // later pass can repair it -- another TU that happens to see the struct
                  // completed enriches only the ANNOTATION, while the live table keeps the
                  // physical names. The override has to be visible here.
                  CDT_CHECK_ERROR(false, "abigen_error", val_decl->getLocation(),
                     "kv_key struct '" + kv_key_name + "' is not visible where this table is "
                     "instantiated, so its key layout cannot be described; include the "
                     "definition in this translation unit");
               }
            }
         }

         if (scoped) {
            t.key_names.push_back("scope");
            t.key_types.push_back("name");
         }
         for (auto* field : key_source->fields()) {
            t.key_names.push_back(field->getName().str());
            t.key_types.push_back(translate_type(field->getType()));
         }
         t.secondary_indexes = std::move(sec_indexes);
         kv_key_structs.insert(key_decl->getNameAsString());
         insert_table(std::move(t), val_decl ? val_decl->getLocation() : key_decl->getLocation());
      }

      /// Add the table an instantiation of multi_index / kv_multi_index / kv_singleton /
      /// kv::global names.
      ///
      /// \p name  the raw table parameter: a name encoding, or the DJB2 hash a `_i` literal gives
      /// \p row   the row type, which need NOT be a class -- sysio::singleton<"cfg"_n, uint64_t>
      ///          is ordinary contract code
      /// Insert, or report the entry this one collides with.
      ///
      /// abi_table is ordered by NAME, so a second table under a name already present is
      /// dropped by the set -- silently, and inside the plugin, so it never reaches the
      /// descriptor and none of cdt-codegen's checks can see it. Two instantiations of the same
      /// table are the ordinary case and must stay silent; a different table_id, row or type
      /// under one name is a contract that writes two tables and describes one.
      void insert_table( abi_table t, const clang::SourceLocation& loc ) {
         const auto [it, inserted] = _abi.tables.insert(t);
         if (inserted)
            return;
         // Everything an entry describes, not just its identity. Two kv::tables sharing a name
         // over one row type differ ONLY in the key struct, so comparing id, type and row alone
         // called them the same table and kept whichever arrived first -- silently, which is
         // exactly what this is here to stop.
         if (it->table_id == t.table_id && it->type == t.type && it->row == t.row &&
             it->key_names == t.key_names && it->key_types == t.key_types &&
             it->secondary_indexes == t.secondary_indexes)
            return;
         const auto describe = [](const abi_table& e) {
            std::string d = "table_id " + std::to_string(e.table_id) + " over '" +
                            (e.row.empty() ? e.type : e.row) + "' keyed on (";
            for (std::size_t i = 0; i < e.key_names.size(); ++i)
               d += (i ? ", " : "") + e.key_names[i];
            return d + ")";
         };
         CDT_CHECK_WARN(false, "abigen_warning", loc,
            "two different tables are both called '" + t.name + "': " + describe(*it) +
            ", and " + describe(t) +
            "; the ABI can describe only one, and the second is not described");
      }

      void add_table( uint64_t name, const clang::QualType& row, kv_table_kind kind = kv_table_kind::legacy,
                      std::vector<abi_secondary_index> sec_indexes = {},
                      const clang::SourceLocation& loc = {} ) {
         abi_table t;
         // The ABI type name, not the C++ record name. std::string's record is `basic_string`
         // and sysio::checksum256's is `fixed_bytes`, neither of which any ABI declares, and the
         // chain refuses a document whose table names a type it cannot resolve
         // (invalid_type_inside_abi). get_type() gives the spelling add_type() would declare.
         t.type = get_type(row);
         // Same rule add_kv_table uses: the template parameter is the name, and a
         // [[sysio::table("name")]] on the row struct may rename it link-wide, in cdt-codegen.
         // That annotation is the only channel a `_i` name has -- the raw is a DJB2 hash and
         // no string is recoverable from it -- and a row is always a struct, so there is always
         // somewhere to put it.
         t.name = name_to_string(name);
         if (const auto* row_decl = row.getTypePtr()->getAsCXXRecordDecl())
            t.row = row_decl->getQualifiedNameAsString();
         t.table_id     = compute_table_id_from_raw(name);
         t.has_table_id = true;
         if (kind == kv_table_kind::kv_standard) {
            // KV multi_index: key = [scope:8B BE][pk:8B BE], table_id provides isolation
            t.key_names = {"scope", "primary_key"};
            t.key_types = {"name", "uint64"};
         } else if (kind == kv_table_kind::kv_global) {
            // KV global: single 8-byte big-endian name key
            t.key_names = {"name"};
            t.key_types = {"name"};
         }
         t.secondary_indexes = std::move(sec_indexes);
         insert_table(std::move(t), loc);
      }

      /// A table row must be a STRUCT this document declares.
      ///
      /// That is the rule upstream has always enforced -- its add_table() takes a
      /// CXXRecordDecl and dereferences it -- and keeping it means a contract ported from
      /// another Antelope chain behaves here exactly as it did there. It also keeps the ABI
      /// self-describing: a table's rows are named fields, not a bare scalar or a container
      /// whose shape lives only in the C++.
      ///
      /// So this refuses a scalar, a container, a variant, a binary_extension, and the class
      /// types a contract author does not own -- std::string, sysio::checksum256,
      /// fixed_bytes<N> -- with one diagnostic naming the row. Wrap the value in a one-field
      /// struct, which is what the ABI needs in order to describe it at all.
      ///
      /// Must be called after add_type(row): that is what puts the struct in the document, and
      /// validate_struct() keeps a struct a table names, so a type found here is still there
      /// when the document is written.
      bool abi_row_is_struct( const clang::QualType& row ) {
         if (!row.getTypePtr()->getAsCXXRecordDecl())
            return false;
         const std::string type = get_type(row);
         if (is_builtin_type(type))
            return false;
         if (starts_with(type, "protobuf::"))
            return true;
         for (const auto& s : _abi.structs)
            if (s.name == type) return true;
         return false;
      }

      void add_clauses( const std::vector<std::pair<std::string, std::string>>& clauses ) {
         for ( auto clp : clauses ) {
            _abi.ricardian_clauses.push_back({std::get<0>(clp), std::get<1>(clp)});
         }
      }

      void add_contracts( const std::map<std::string, std::string>& rc ) {
         rcs = rc;
      }

      void add_variant( const clang::QualType& t ) {
         abi_variant var;
         auto pt = llvm::dyn_cast<clang::ElaboratedType>(t.getTypePtr());
         auto tst = llvm::dyn_cast<clang::TemplateSpecializationType>((pt) ? pt->desugar().getTypePtr() : t.getTypePtr());
         var.name = get_type(t);
         for (int i=0; i < tst->template_arguments().size(); ++i) {
            var.types.push_back(get_template_argument_as_string( t, i ));
            add_type(std::get<clang::QualType>(get_template_argument( t, i )));
         }
         _abi.variants.insert(var);
      }

      // --- Explicit nested type handling (wire-cdt unique) ---
      inline void adding_explicit_nested_dispatcher(const clang::QualType& inside_type, int depth, std::string & inside_type_name){
         if(is_explicit_nested(inside_type)){
            inside_type_name = add_explicit_nested_type(inside_type, depth + 1);
         } else if(is_explicit_container(inside_type)) {
            inside_type_name = add_explicit_nested_type(inside_type, depth + 1);
         }else if (is_builtin_type(translate_type(inside_type))){
            inside_type_name = translate_type(inside_type);
         } else if (is_aliasing(inside_type)) {
            add_typedef(inside_type);
            inside_type_name = get_base_type_name( inside_type );
         }   else if (is_template_specialization(inside_type, {})) {
            add_struct(inside_type.getTypePtr()->getAsCXXRecordDecl(), get_template_name(inside_type));
            inside_type_name = get_template_name(inside_type);
         }else if (inside_type.getTypePtr()->isRecordType()) {
            add_struct(inside_type.getTypePtr()->getAsCXXRecordDecl());
            inside_type_name = inside_type.getTypePtr()->getAsCXXRecordDecl()->getNameAsString();
         } else if (inside_type.getTypePtr()->isEnumeralType()) {
            add_type(inside_type);
            inside_type_name = get_base_type_name(inside_type);
         } else {
            std::string errstring = "adding_explicit_nested_dispatcher: this inside type  ";
            errstring += inside_type.getAsString();
            errstring += " is unexpected, maybe not supported so far. \n";
            CDT_INTERNAL_ERROR(errstring);
         }
      }

      void add_explicit_nested_linear(const clang::QualType& type, int depth, abi_typedef & abidef, std::string & ret, const std::string & tname, bool & gottype){
         ret += tname + "_";
         auto inside_type = std::get<clang::QualType>(get_template_argument(type));
         std::string inside_type_name;
         adding_explicit_nested_dispatcher(inside_type, depth, inside_type_name);
         if(inside_type_name != ""){
            ret += inside_type_name;
            abidef.type = inside_type_name + ( (tname == "optional") ? "?" : "[]" );
            gottype = true;
         }
      }

      void add_explicit_nested_map(const clang::QualType& type, int depth, abi_typedef & abidef, std::string & ret, const std::string & tname, bool & gottype){
         ret += tname + "_";
         clang::QualType inside_type[2];
         std::string inside_type_name[2];
         for(int i = 0; i < 2; ++i){
            inside_type[i] = std::get<clang::QualType>(get_template_argument(type, i));
            adding_explicit_nested_dispatcher(inside_type[i], depth, inside_type_name[i]);
         }

         if(inside_type_name[0] != "" && inside_type_name[1] != ""){
            ret += inside_type_name[0] + "_" + inside_type_name[1];
            abidef.type = "pair_" + inside_type_name[0] + "_" + inside_type_name[1] + "[]";

            abi_struct kv;
            kv.name = "pair_" + inside_type_name[0] + "_" + inside_type_name[1];
            kv.fields.push_back( {"first", inside_type_name[0]} );
            kv.fields.push_back( {"second", inside_type_name[1]} );
            _abi.structs.insert(kv);

            gottype = true;
         }
      }

      void add_explicit_nested_pair(const clang::QualType& type, int depth, abi_typedef & abidef, std::string & ret, const std::string & tname, bool & gottype){
         ret += tname + "_";
         clang::QualType inside_type[2];
         std::string inside_type_name[2];
         for(int i = 0; i < 2; ++i){
            inside_type[i] = std::get<clang::QualType>(get_template_argument(type, i));
            adding_explicit_nested_dispatcher(inside_type[i], depth, inside_type_name[i]);
         }

         if(inside_type_name[0] != "" && inside_type_name[1] != ""){
            ret += inside_type_name[0] + "_" + inside_type_name[1];
            abidef.type = "pair_" + inside_type_name[0] + "_" + inside_type_name[1];

            abi_struct pair;
            pair.name = "pair_" + inside_type_name[0] + "_" + inside_type_name[1];
            pair.fields.push_back( {"first", inside_type_name[0]} );
            pair.fields.push_back( {"second", inside_type_name[1]} );
            _abi.structs.insert(pair);

            gottype = true;
         }
      }

      void add_explicit_nested_tuple(const clang::QualType& type, int argcnt, int depth, abi_typedef & abidef, std::string & ret, const std::string & tname, bool & gottype){
         ret += tname + "_";
         std::vector<clang::QualType> inside_type(argcnt);
         std::vector<std::string> inside_type_name(argcnt);
         for(int i = 0; i < argcnt; ++i){
            inside_type[i] = std::get<clang::QualType>(get_template_argument(type, i));
            adding_explicit_nested_dispatcher(inside_type[i], depth, inside_type_name[i]);
         }
         bool allgot = true;
         for(auto & inside_tn : inside_type_name) {
            if(inside_tn == "") allgot = false;
         }
         if(allgot){
            abi_struct tup;
            tup.name = "tuple_";
            abidef.type = "tuple_";
            for (int i = 0; i < argcnt; ++i) {
               ret += inside_type_name[i] + (i < (argcnt - 1) ? "_" : "");
               abidef.type += inside_type_name[i] + (i < (argcnt - 1) ? "_" : "");
               tup.name += inside_type_name[i] + (i < (argcnt - 1) ? "_" : "");
               tup.fields.push_back( {"field_"+std::to_string(i), inside_type_name[i]} );
            }
            _abi.structs.insert(tup);

            gottype = true;
         }
      }

      void add_explicit_nested_array(const clang::QualType& type, int depth, abi_typedef & abidef, std::string & ret, const std::string & tname, bool & gottype){
         ret += tname + "_";
         auto inside_type = std::get<clang::QualType>(get_template_argument(type));
         std::string inside_type_name;
         adding_explicit_nested_dispatcher(inside_type, depth, inside_type_name);

         if(inside_type_name != ""){
            ret += inside_type_name + "_";
            std::string orig = type.getAsString();
            auto pos1 = orig.find_last_of(',');
            auto pos2 = orig.find_last_of('>');
            std::string digits = orig.substr(pos1 + 1, pos2 - pos1 - 1);
            digits.erase(std::remove(digits.begin(), digits.end(), ' '), digits.end());
            ret += digits;
            abidef.type = inside_type_name + "[" + digits + "]" ;
            gottype = true;
         }
      }

      void add_explicit_nested_variant(const clang::QualType& type, int argcnt, int depth, abi_typedef & abidef, std::string & ret, const std::string & tname, bool & gottype){
         ret += tname + "_";
         std::vector<clang::QualType> inside_type(argcnt);
         std::vector<std::string> inside_type_name(argcnt);
         for(int i = 0; i < argcnt; ++i){
            inside_type[i] = std::get<clang::QualType>(get_template_argument(type, i));
            adding_explicit_nested_dispatcher(inside_type[i], depth, inside_type_name[i]);
         }
         bool allgot = true;
         for(auto & inside_tn : inside_type_name) {
            if(inside_tn == "") allgot = false;
         }

         if(allgot){
            abi_variant var;
            var.name = "variant_";
            abidef.type = "variant_";
            for (int i = 0; i < argcnt; ++i) {
               ret += inside_type_name[i] + (i < (argcnt - 1) ? "_" : "");
               abidef.type += inside_type_name[i] + (i < (argcnt - 1) ? "_" : "");
               var.name += inside_type_name[i] + (i < (argcnt - 1) ? "_" : "");
               var.types.push_back( inside_type_name[i]);
            }
            _abi.variants.insert(var);

            gottype = true;
         }
      }

      // return combined typename, and mid-type will be added automatically, only be used on explicit nested type has <<>> or more
      std::string add_explicit_nested_type(const clang::QualType& type, int depth = 0){
         abi_typedef abidef;
         std::string ret = "B_";
         bool gottype = false;
         auto pt = llvm::dyn_cast<clang::ElaboratedType>(type.getTypePtr());
         if(auto tst = llvm::dyn_cast<clang::TemplateSpecializationType>(pt ? pt->desugar().getTypePtr() : type.getTypePtr())){
            if(auto rt = llvm::dyn_cast<clang::RecordType>(tst->desugar())){
               if(auto * decl = rt->getDecl()){
                  std::string tname = decl->getName().str();
                  if(tname == "vector" || tname == "set" || tname == "deque" || tname == "list" || tname == "optional") {
                     add_explicit_nested_linear(type, depth,  abidef, ret, tname, gottype);
                  } else if (tname == "map" ) {
                     add_explicit_nested_map(type, depth, abidef, ret, tname, gottype);
                  } else if (tname == "pair" ) {
                     add_explicit_nested_pair(type, depth, abidef, ret, tname, gottype);
                  } else if (tname == "tuple")  {
                     int argcnt = tst->template_arguments().size();
                     add_explicit_nested_tuple(type, argcnt, depth, abidef, ret, tname, gottype);
                  } else if (tname == "array")  {
                     add_explicit_nested_array(type, depth, abidef, ret, tname, gottype);
                  } else if (tname == "variant") {
                     int argcnt = tst->template_arguments().size();
                     add_explicit_nested_variant(type, argcnt, depth, abidef, ret, tname, gottype);
                  }
               }
            }
         }

         if(!gottype) {
            std::string errstring = "add_explicit_nested_type failed to fetch type from ";
            errstring += type.getAsString();
            CDT_INTERNAL_ERROR(errstring);
            return "";
         }
         ret +="_E";
         abidef.new_type_name = ret;  // the name is combined from container name and low layer type
         if(depth > 0) _abi.typedefs.insert(abidef);
         return ret;
      }

      void add_type( const clang::QualType& t ) {
         if (evaluated.count(t.getTypePtr()))
            return;
         evaluated.insert(t.getTypePtr());
         auto type = get_ignored_type(t);
         if(is_explicit_nested(t)){
            add_explicit_nested_type(t.getNonReferenceType());
            return;
         }
         if (!is_builtin_type(translate_type(type))) {
            // Handle C++ enums (both scoped `enum class` and unscoped `enum`)
            // by creating an enum_def with member names and values.
            if (type.getTypePtr()->isEnumeralType()) {
               // Use canonical type for the cast — the non-canonical pointer may be
               // wrapped in ElaboratedType (common in LLVM 18), which would cause
               // dyn_cast<EnumType> to return null even though isEnumeralType() is true.
               const clang::EnumType* ET = llvm::dyn_cast<clang::EnumType>(type.getCanonicalType().getTypePtr());
               if (ET) {
                  const clang::EnumDecl* ED = ET->getDecl();
                  if (ED) {
                     abi_enum en;
                     en.name = get_base_type_name(type);
                     en.type = translate_type(ED->getIntegerType());
                     if (!en.name.empty() && !en.type.empty()) {
                        for (auto it = ED->enumerator_begin(); it != ED->enumerator_end(); ++it) {
                           abi_enum_value ev;
                           ev.name = it->getNameAsString();
                           ev.value = it->getInitVal().isUnsigned()
                              ? static_cast<int64_t>(it->getInitVal().getZExtValue())
                              : it->getInitVal().getSExtValue();
                           en.values.push_back(ev);
                        }
                        _abi.enums.insert(en);
                     }
                  }
               }
            }
            else if (is_aliasing(type)) {
               add_typedef(type);
            }
            else if (is_template_specialization(type, {"pb"})) {
               // Protobuf types are not added as regular ABI structs.
               // They are tracked via pb_types and embedded as protobuf_types in the ABI.
               auto translated = translate_type(type);
               if (translated.find("protobuf::") == 0) {
                  pb_types.insert(translated.substr(sizeof("protobuf::") - 1));
               }
               return;
            }
            else if (is_template_specialization(type, {"vector", "set", "deque", "list", "optional", "binary_extension", "ignore"})) {
               add_type(std::get<clang::QualType>(get_template_argument(type)));
            }
            else if (is_template_specialization(type, {"map"}))
               add_map(type);
            else if (is_template_specialization(type, {"pair"}))
               add_pair(type);
            else if (is_template_specialization(type, {"tuple"}))
               add_tuple(type);
            else if (is_template_specialization(type, {"array"}) )
               add_type(std::get<clang::QualType>(get_template_argument(type, 0)));
            else if (is_template_specialization(type, {"variant"}))
               add_variant(type);
            else if (is_template_specialization(type, {})) {
               add_struct(type.getTypePtr()->getAsCXXRecordDecl(), get_template_name(type));
            }
            else if (type.getTypePtr()->isRecordType())
               add_struct(type.getTypePtr()->getAsCXXRecordDecl());
         }
      }

      std::string generate_json_comment() {
         std::stringstream ss;
         ss << "This file was generated with sysio-abigen.";
         ss << " DO NOT EDIT ";
         return ss.str();
      }

      ojson struct_to_json( const abi_struct& s ) {
         ojson o;
         o["name"] = s.name;
         o["base"] = s.base;
         o["fields"] = ojson::array();
         for ( auto field : s.fields ) {
            ojson f;
            f["name"] = field.name;
            f["type"] = field.type;
            o["fields"].push_back(f);
         }
         return o;
      }

      ojson variant_to_json( const abi_variant& v ) {
         ojson o;
         o["name"] = v.name;
         o["types"] = ojson::array();
         for ( auto ty : v.types ) {
            o["types"].push_back( ty );
         }
         return o;
      }

      ojson typedef_to_json( const abi_typedef& t ) {
         ojson o;
         o["new_type_name"] = t.new_type_name;
         o["type"]          = t.type;
         return o;
      }

      ojson action_to_json( const abi_action& a ) {
         ojson o;
         o["name"] = a.name;
         o["type"] = a.type;
         o["ricardian_contract"] = a.ricardian_contract;
         return o;
      }

      ojson clause_to_json( const abi_ricardian_clause_pair& clause ) {
         ojson o;
         o["id"] = clause.id;
         o["body"] = clause.body;
         return o;
      }

      ojson table_to_json( const abi_table& t ) {
         ojson o;
         o["name"] = t.name;
         o["type"] = t.type;
         o["index_type"] = "i64";
         o["key_names"] = ojson::array();
         for (const auto& kn : t.key_names)
            o["key_names"].push_back(kn);
         o["key_types"] = ojson::array();
         for (const auto& kt : t.key_types)
            o["key_types"].push_back(kt);
         // Presence, not value: zero is a table_id the hash really produces.
         if (t.has_table_id)
            o["table_id"] = t.table_id;
         // Descriptor-only; cdt-codegen strips it. See abi_table::row.
         if (!t.row.empty())
            o["____row"] = t.row;
         if (!t.secondary_indexes.empty()) {
            o["secondary_indexes"] = ojson::array();
            for (const auto& si : t.secondary_indexes) {
               ojson idx;
               idx["name"] = si.name;
               idx["key_type"] = si.key_type;
               idx["table_id"] = si.table_id;
               o["secondary_indexes"].push_back(std::move(idx));
            }
         }
         return o;
      }

      ojson enum_to_json( const abi_enum& e ) {
         ojson o;
         o["name"] = e.name;
         o["type"] = e.type;
         o["values"] = ojson::array();
         for ( auto ev : e.values ) {
            ojson v;
            v["name"] = ev.name;
            v["value"] = ev.value;
            o["values"].push_back(v);
         }
         return o;
      }

      ojson action_result_to_json( const abi_action_result& result ) {
         ojson o;
         o["name"] = result.name;
         o["result_type"] = result.type;
         return o;
      }

      ojson wasm_action_to_json(const wasm_action& a) {
         ojson o;
         o["name"] = a.name;
         o["handler"] = a.handler;
         return o;
      }

      ojson wasm_notify_to_json(const wasm_notify& n) {
         ojson o;
         o["contract"] = n.contract;
         o["name"] = n.name;
         o["handler"] = n.handler;
         return o;
      }

      void set_has_pre_dispatch()  { _abi.has_pre_dispatch  = true; }
      void set_has_post_dispatch() { _abi.has_post_dispatch = true; }

      bool has_wasm_data() const {
         return !_abi.wasm_actions.empty() || !_abi.wasm_notifies.empty() || !_abi.wasm_entries.empty();
      }

      bool is_empty() {
         return _abi.structs.empty() && _abi.typedefs.empty() && _abi.actions.empty() &&
                _abi.tables.empty() && _abi.table_annotations.empty() &&
                _abi.ricardian_clauses.empty() && _abi.variants.empty() && _abi.enums.empty();
      }

      ojson to_json() {
         ojson o;
         o["____comment"] = generate_json_comment();
         o["version"]     = _abi.version_string();
         o["structs"]     = ojson::array();
         auto remove_suffix = [&]( std::string name ) {
            int i = name.length()-1;
            for (; i >= 0; i--)
               if ( name[i] != '[' && name[i] != ']' && name[i] != '?' && name[i] != '$' )
                  break;
            return name.substr(0,i+1);
         };

         // Tables go out exactly as the instantiations named them. A
         // [[sysio::table("name")]] is carried alongside in ____table_annotations and applied by
         // cdt-codegen once every descriptor is in, because whether it names a table -- and
         // which -- depends on how many tables the struct backs link-wide.
         const std::set<abi_table>& set_of_tables = _abi.tables;

         std::function<std::string(const std::string&)> get_root_name;
         get_root_name = [&] (const std::string& name) {
            for (auto td : _abi.typedefs)
               if (remove_suffix(name) == td.new_type_name)
                  return get_root_name(td.type);
            return name;
         };

         auto validate_struct = [&]( abi_struct as ) {
            if ( is_builtin_type(_translate_type(as.name)) )
               return false;
            if ( is_reserved(_translate_type(as.name)) ) {
               return false;
            }
            for ( auto s : _abi.structs ) {
               for ( auto f : s.fields ) {
                  if (as.name == _translate_type(remove_suffix(f.type)))
                     return true;
               }
               for ( auto v : _abi.variants ) {
                  for ( auto vt : v.types ) {
                     if (as.name == _translate_type(remove_suffix(vt)))
                        return true;
                  }
               }
               if (get_root_name(s.base) == as.name)
                  return true;
            }
            for ( auto a : _abi.actions ) {
               if (as.name == _translate_type(a.type))
                  return true;
            }
            for( auto t : set_of_tables ) {
               if (as.name == _translate_type(t.type))
                  return true;
               // Include structs referenced by [[sysio::kv_key]]
               if (kv_key_structs.count(as.name))
                  return true;
            }
            // An annotation may still become a table in cdt-codegen, and a table naming a type
            // the document does not declare is refused by the chain.
            for( const auto& ta : _abi.table_annotations ) {
               if (as.name == _translate_type(ta.type))
                  return true;
            }
            for( auto td : _abi.typedefs ) {
               if (as.name == _translate_type(remove_suffix(td.type)))
                  return true;
            }
            for( auto ar : _abi.action_results ) {
               if (as.name == _translate_type(ar.type))
                  return true;
            }
            return false;
         };

         auto validate_types = [&]( abi_typedef td ) {
            for ( auto as : _abi.structs )
               if (validate_struct(as)) {
                  for ( auto f : as.fields )
                     if ( remove_suffix(f.type) == td.new_type_name )
                        return true;
                  if (as.base == td.new_type_name)
                     return true;
               }

            for ( auto v : _abi.variants ) {
               for ( auto vt : v.types ) {
                  if ( remove_suffix(vt) == td.new_type_name )
                     return true;
               }
            }
            for ( auto t : _abi.tables )
               if ( t.type == td.new_type_name )
                  return true;
            for ( auto a : _abi.actions )
               if ( a.type == td.new_type_name )
                  return true;
            for ( auto _td : _abi.typedefs )
               if ( remove_suffix(_td.type) == td.new_type_name )
                  return true;
            for ( auto ar : _abi.action_results ) {
               if ( ar.type == td.new_type_name )
                  return true;
            }
            return false;
         };

         for ( auto s : _abi.structs ) {
            const auto res = validate_struct(s);
            if (res)
               o["structs"].push_back(struct_to_json(s));
         }
         o["types"]       = ojson::array();
         for ( auto t : _abi.typedefs ) {
            if (validate_types(t))
               o["types"].push_back(typedef_to_json( t ));
         }
         o["actions"]     = ojson::array();
         for ( auto a : _abi.actions ) {
            o["actions"].push_back(action_to_json( a ));
         }
         o["tables"]     = ojson::array();
         for ( auto t : set_of_tables ) {
            o["tables"].push_back(table_to_json( t ));
         }
         if (!_abi.table_annotations.empty()) {
            o["____table_annotations"] = ojson::array();
            for ( const auto& ta : _abi.table_annotations ) {
               ojson a;
               a["name"] = ta.name;
               a["type"] = ta.type;
               a["row"]  = ta.row;
               a["loc"]  = ta.loc;
               a["key_names"] = ojson::array();
               for (const auto& kn : ta.key_names)
                  a["key_names"].push_back(kn);
               a["key_types"] = ojson::array();
               for (const auto& kt : ta.key_types)
                  a["key_types"].push_back(kt);
               o["____table_annotations"].push_back(std::move(a));
            }
         }
         o["ricardian_clauses"]  = ojson::array();
         for ( auto rc : _abi.ricardian_clauses ) {
            o["ricardian_clauses"].push_back(clause_to_json( rc ));
         }
         o["variants"]   = ojson::array();
         for ( auto v : _abi.variants ) {
            o["variants"].push_back(variant_to_json( v ));
         }
         o["abi_extensions"]     = ojson::array();
         if (abi_version::supports_action_results(_abi.version_major, _abi.version_minor)) {
            o["action_results"]  = ojson::array();
            for ( auto ar : _abi.action_results ) {
               o["action_results"].push_back(action_result_to_json( ar ));
            }
         }

         auto validate_enums = [&]( abi_enum en ) {
            for ( auto as : _abi.structs )
               if (validate_struct(as)) {
                  for ( auto f : as.fields )
                     if ( remove_suffix(f.type) == en.name )
                        return true;
               }
            for ( auto a : _abi.actions )
               if ( a.type == en.name )
                  return true;
            for ( auto t : _abi.tables )
               if ( t.type == en.name )
                  return true;
            for ( auto ar : _abi.action_results )
               if ( ar.type == en.name )
                  return true;
            return false;
         };

         {
            ojson enums_arr = ojson::array();
            for ( auto e : _abi.enums ) {
               if (validate_enums(e))
                  enums_arr.push_back(enum_to_json( e ));
            }
            if (!enums_arr.empty())
               o["enums"] = enums_arr;
         }
         return o;
      }

      ojson to_json_debug() {
         auto o = to_json();
         o["wasm_actions"] = ojson::array();
         for (auto& a : _abi.wasm_actions) {
            o["wasm_actions"].push_back(wasm_action_to_json(a));
         }
         o["wasm_notifies"] = ojson::array();
         for (auto& n : _abi.wasm_notifies) {
            o["wasm_notifies"].push_back(wasm_notify_to_json(n));
         }
         o["wasm_entries"] = ojson::array();
         for (auto& e : _abi.wasm_entries) {
            o["wasm_entries"].push_back(e);
         }
         o["pb_types"] = ojson::array();
         for (auto& e : pb_types) {
            o["pb_types"].push_back(e);
         }
         if (_abi.has_pre_dispatch)
            o["has_pre_dispatch"] = true;
         if (_abi.has_post_dispatch)
            o["has_post_dispatch"] = true;
         return o;
      }

      private:
         abi                                   _abi;
         std::map<std::string, std::string>    rcs;
         std::set<const clang::Type*>          evaluated;
         std::set<std::string>                 pb_types;
   };

   class sysio_abigen_visitor : public RecursiveASTVisitor<sysio_abigen_visitor>, public generation_utils {
      private:
         bool has_added_clauses = false;
         abigen& ag = abigen::get();
         const clang::CXXRecordDecl* contract_class = NULL;

      public:
         explicit sysio_abigen_visitor(CompilerInstance *CI) {
            get_error_emitter().set_compiler_instance(CI);
         }

         bool shouldVisitTemplateInstantiations() const {
            return true;
         }

         virtual bool VisitCXXMethodDecl(clang::CXXMethodDecl* _decl) {
            auto decl = clang_wrapper::wrap_decl(_decl);
            if (!has_added_clauses) {
               ag.add_clauses(ag.parse_clauses());
               ag.add_contracts(ag.parse_contracts());
               has_added_clauses = true;
            }

            if (decl.isSysioAction() && ag.is_sysio_contract(decl, ag.get_contract_name())) {
               ag.add_struct(_decl);
               ag.add_action(_decl);
               for (auto param : _decl->parameters()) {
                  ag.add_type( param->getType() );
               }
            }
            return true;
         }
         virtual bool VisitCXXRecordDecl(clang::CXXRecordDecl* _decl) {
            auto decl = clang_wrapper::wrap_decl(_decl);
            if (!has_added_clauses) {
               ag.add_clauses(ag.parse_clauses());
               ag.add_contracts(ag.parse_contracts());
               has_added_clauses = true;
            }
            if ((decl.isSysioAction() || decl.isSysioTable()) && ag.is_sysio_contract(decl, ag.get_contract_name())) {
               ag.add_struct(_decl);
               if (decl.isSysioAction())
                  ag.add_action(_decl);
               if (decl.isSysioTable())
                  ag.add_table(_decl);
               for (auto field : _decl->fields()) {
                  ag.add_type( field->getType() );
               }
            }
            return true;
         }

         bool is_same_type(const clang::Decl* decl1, const clang::CXXRecordDecl* decl2) const {
            if (!decl1 || !decl2)
               return false;
            if (decl1 == decl2)
               return true;

            // checking if declaration is a typedef or using
            if (const clang::TypedefNameDecl* typedef_decl = llvm::dyn_cast<clang::TypedefNameDecl>(decl1)) {
               if (const auto* cur_type = typedef_decl->getUnderlyingType().getTypePtrOrNull()) {
                  if (decl2 == cur_type->getAsCXXRecordDecl()) {
                        return true;
                  }
               }
            }

            // A data member whose type IS the specialization owns it exactly as much as an
            // alias to it does -- `singleton<"cfg"_n, uint64_t> cfg;` is the most direct way a
            // contract holds one, and needs no alias at all. Matching only the specialization
            // itself and a TypedefNameDecl meant such a member admitted nothing: a member
            // singleton over a scalar, or over a row struct carrying no annotation, emitted no
            // table whatsoever. The contract built and its `tables` array was empty.
            //
            // Members whose row struct DOES carry [[sysio::table]] were admitted all along by
            // the other arm of the caller's ||, which is what kept the hole this narrow.
            //
            // DeclaratorDecl rather than FieldDecl so a static data member counts too; a member
            // function is one as well, but its type is a function type and asks nothing here.
            if (const clang::DeclaratorDecl* var_decl = llvm::dyn_cast<clang::DeclaratorDecl>(decl1)) {
               if (const auto* cur_type = var_decl->getType().getTypePtrOrNull()) {
                  if (decl2 == cur_type->getAsCXXRecordDecl())
                     return true;
               }
            }

            return false;
         }

         bool defined_in_contract(const clang::ClassTemplateSpecializationDecl* decl) {
            if (!contract_class)
               return false;

            for (const clang::Decl* cur_decl : contract_class->decls()) {
               if (is_same_type(cur_decl, decl))
                  return true;
            }

            return false;
         }

         /// The table name as the contract WROTE it, when the template parameter cannot carry
         /// it -- or empty, which means name_to_string() is right and should be used.
         ///
         /// `_i` (hash_id.hpp) DJB2-hashes its argument and hands the result over as name::raw,
         /// so the parameter holds no name encoding and name_to_string() decodes it into text
         /// nothing can address the table by. #115 recovers the spelling from
         /// [[sysio::table("name")]] on the row struct -- but a row need not be a class
         /// (singleton<"cfg"_i, uint64_t> is ordinary contract code) and a scalar has nowhere
         /// to put an annotation. That combination published a decoded hash as the table name,
         /// at the correct table_id, so the table was live and unaddressable by name.
         ///
         /// The written form is still in the AST. A specialization over a non-class row reaches
         /// the ABI only through the contract member that names it, and that member keeps a
         /// TypeSourceInfo whose first argument is the literal exactly as it was typed.
         ///
         /// `_n` needs none of this: there the raw IS the name, and re-deriving it from source
         /// text would only add a way to disagree with the runtime.
         virtual bool VisitDecl(clang::Decl* decl) {
            if (const auto* d = dyn_cast<clang::ClassTemplateSpecializationDecl>(decl)) {
               // kv::cached_value<Store> is a transparent wrapper: the table it stands for is the one
               // its Store parameter describes. Unwrap to that Store, but keep the WRAPPER as the decl
               // whose use marks the table as belonging to this contract -- the inner specialization is
               // only ever named inside cached_value, so defined_in_contract() would reject it and a
               // payload without [[sysio::table]] would silently lose its ABI table entry. Nothing
               // fails at build time; it surfaces later as clio get table, SHiP, and the generated SDK
               // types no longer seeing the table.
               const clang::ClassTemplateSpecializationDecl* owner = d;
               if (d->getName() == "cached_value" && d->getTemplateArgs().size() >= 1 &&
                   d->getTemplateArgs()[0].getKind() == clang::TemplateArgument::Type) {
                  if (const auto* store = d->getTemplateArgs()[0].getAsType().getTypePtr()->getAsCXXRecordDecl()) {
                     if (const auto* store_spec = dyn_cast<clang::ClassTemplateSpecializationDecl>(store))
                        d = store_spec;
                  }
               }
               // These are the names a SPECIALIZATION can carry, which is not the same as the
               // names a contract writes. sysio::multi_index and sysio::singleton are alias
               // templates (multi_index.hpp, singleton.hpp) over kv_multi_index and
               // kv_singleton; an alias template has no specialization of its own, so the AST
               // only ever holds the aliased one and neither spelling can match here. That is
               // how every singleton went undescribed: `kv_multi_index` was on this list and
               // `kv_singleton` was not, so a contract's multi_index tables reached the ABI
               // and its singletons -- written the same way, through the same alias -- did not.
               if (d->getName() == "multi_index" || d->getName() == "singleton" ||
                   d->getName() == "kv_multi_index" || d->getName() == "kv_singleton" ||
                   d->getName() == "table" || d->getName() == "scoped_table" ||
                   d->getName() == "global") {
                  abigen::kv_table_kind kind = abigen::kv_table_kind::legacy;
                  // kv_singleton IS a kv_multi_index -- it holds one privately and pins the
                  // primary key -- so it lands under the same [scope][primary_key] KV layout.
                  if (d->getName() == "kv_multi_index" || d->getName() == "kv_singleton")
                     kind = abigen::kv_table_kind::kv_standard;
                  else if (d->getName() == "global")
                     kind = abigen::kv_table_kind::kv_global;
                  if ((d->getName() == "table" || d->getName() == "scoped_table") && d->getTemplateArgs().size() >= 3) {
                     // kv::table<Name, K, V, ...Indices>
                     const auto  key_qual = d->getTemplateArgs()[1].getAsType();
                     const auto  val_qual = d->getTemplateArgs()[2].getAsType();
                     const auto* key_type = key_qual.getTypePtr()->getAsCXXRecordDecl();
                     const auto* val_type = val_qual.getTypePtr()->getAsCXXRecordDecl();
                     auto val_decl = clang_wrapper::wrap_decl(val_type);
                     if ((val_decl.isSysioTable() && ag.is_sysio_contract(val_decl, ag.get_contract_name())) || defined_in_contract(owner)) {
                        auto table_name_raw = d->getTemplateArgs()[0].getAsIntegral().getLimitedValue();

                        // A kv::table's key layout IS the key struct's fields, so a key that is
                        // not a class describes nothing -- and asking a non-class type for its
                        // CXXRecordDecl gives null, which crashed the compiler here rather than
                        // saying so. Reported against the contract class: an implicit
                        // specialization's location is the template inside sysiolib.
                        CDT_CHECK_ERROR(key_type, "abigen_error",
                           contract_class ? contract_class->getLocation() : d->getLocation(),
                           "table '" + name_to_string(table_name_raw) + "' has key type '" +
                           key_qual.getAsString() + "', which is not a struct; a kv::table key "
                           "type's fields are the table's key layout");
                        if (!key_type)
                           return true;

                        // Extract secondary index info from Indices... (args[3..])
                        // Variadic packs may appear as individual args OR as a single Pack arg.
                        // kv::table uses hashed index name for table_id (compute_sec_table_id_from_raw).
                        std::vector<abi_secondary_index> sec_indexes;

                        auto extract_index = [&](const clang::TemplateArgument& arg) {
                           // For kv::table we need the index name to compute the table_id, so
                           // peek at the first template arg to get the name BEFORE delegating.
                           if (arg.getKind() != clang::TemplateArgument::Type) return;
                           auto* record = arg.getAsType().getTypePtr()->getAsCXXRecordDecl();
                           auto* idx_spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
                           if (!idx_spec || idx_spec->getTemplateArgs().size() < 1) return;
                           auto first_arg = idx_spec->getTemplateArgs()[0];
                           if (first_arg.getKind() != clang::TemplateArgument::Integral) return;
                           auto idx_name_raw = first_arg.getAsIntegral().getLimitedValue();
                           const uint16_t tid = compute_sec_table_id_from_raw(table_name_raw, idx_name_raw);
                           abi_secondary_index si;
                           if (ag.extract_secondary_index(arg, tid, si))
                              sec_indexes.push_back(std::move(si));
                        };


                        for (size_t i = 3; i < d->getTemplateArgs().size(); ++i) {
                           auto arg = d->getTemplateArgs()[i];
                           if (arg.getKind() == clang::TemplateArgument::Pack) {
                              // Variadic pack — iterate its contents
                              for (const auto& pack_elem : arg.pack_elements()) {
                                 extract_index(pack_elem);
                              }
                           } else {
                              extract_index(arg);
                           }
                        }

                        ag.add_kv_table(table_name_raw, key_type, val_qual, std::move(sec_indexes),
                                        d->getName() == "scoped_table");
                        ag.add_type(val_qual);
                        ag.add_struct(key_type);
                        CDT_CHECK_ERROR(ag.abi_row_is_struct(val_qual), "abigen_error",
                           contract_class ? contract_class->getLocation() : d->getLocation(),
                           "table '" + name_to_string(table_name_raw) + "' has row type '" +
                           val_qual.getAsString() + "', which is not a contract struct; a table "
                           "row must be a struct this contract declares -- wrap the value in one");
                     }
                  } else {
                     // multi_index, singleton, kv_multi_index, kv_singleton, global — arg[1] is
                     // the row type. For kv_singleton that is the payload T, not its private
                     // one-field `row` wrapper: SYSLIB_SERIALIZE(row, (value)) makes the two
                     // serialize to the same bytes, and T is the type a client decodes to.
                     //
                     // A row type need not be a CLASS -- sysio::singleton<"cfg"_n, uint64_t> is
                     // ordinary contract code -- and asking a non-class type for its
                     // CXXRecordDecl gives null. Only a class can carry [[sysio::table]], so a
                     // null decl simply has no annotation, which is what wrap_decl reports for
                     // it. The row is carried on as a QualType from here because add_table used
                     // to take the decl and read a name off it, and that null deref crashed the
                     // compiler outright as soon as kv_singleton started reaching this branch.
                     const auto row_type = d->getTemplateArgs()[1].getAsType();
                     const auto* table_type = row_type.getTypePtr()->getAsCXXRecordDecl();
                     auto table_decl = clang_wrapper::wrap_decl(table_type);
                     if ((table_decl.isSysioTable() && ag.is_sysio_contract(table_decl, ag.get_contract_name())) ||
                         defined_in_contract(owner)) {
                        const auto table_name_raw = d->getTemplateArgs()[0].getAsIntegral().getLimitedValue();

                        // Extract indexed_by<...> secondary indices for multi_index/kv_multi_index.
                        // Layout: multi_index<TableName, T, indexed_by<Name1, Ext1>, indexed_by<Name2, Ext2>, ...>
                        // Indices are positional: table_id uses index_pos (compute_mi_sec_table_id).
                        std::vector<abi_secondary_index> sec_indexes;
                        if (d->getName() == "multi_index" || d->getName() == "kv_multi_index") {
                           uint8_t index_pos = 0;
                           auto extract_indexed_by = [&](const clang::TemplateArgument& arg) {
                              const uint16_t tid = compute_mi_sec_table_id_from_raw(table_name_raw, index_pos);
                              abi_secondary_index si;
                              if (ag.extract_secondary_index(arg, tid, si)) {
                                 sec_indexes.push_back(std::move(si));
                                 ++index_pos;
                              }
                           };

                           // multi_index template args: [0]=TableName, [1]=T, [2..]=indexed_by<...>
                           for (size_t i = 2; i < d->getTemplateArgs().size(); ++i) {
                              auto arg = d->getTemplateArgs()[i];
                              if (arg.getKind() == clang::TemplateArgument::Pack) {
                                 for (const auto& pack_elem : arg.pack_elements()) {
                                    extract_indexed_by(pack_elem);
                                 }
                              } else {
                                 extract_indexed_by(arg);
                              }
                           }
                        }

                        ag.add_table(table_name_raw, row_type, kind, std::move(sec_indexes),
                                     contract_class ? contract_class->getLocation() : d->getLocation());
                        // The ABI has to DEFINE the type it names as a table's row.
                        // [[sysio::table]] is not what makes a struct part of the contract:
                        // defined_in_contract() admits a table whose row type carries no
                        // annotation at all, which is how an ordinary singleton over a plain
                        // struct is written -- and the chain refuses a document whose table
                        // names a type it does not define (invalid_type_inside_abi), so this
                        // cannot be conditional on the annotation. add_type is the same entry
                        // point an action parameter goes through, so a builtin, a container and
                        // an alias each land where they already do, and its spelling is the one
                        // add_table published as `type`.
                        ag.add_type(row_type);
                        // Reported against the contract class: `d` is an implicit
                        // specialization, so its location is the template's definition inside
                        // sysiolib, which tells the author nothing about their own source.
                        CDT_CHECK_ERROR(ag.abi_row_is_struct(row_type), "abigen_error",
                           contract_class ? contract_class->getLocation() : d->getLocation(),
                           "table '" + name_to_string(table_name_raw) + "' has row type '" +
                           row_type.getAsString() + "', which is not a contract struct; a table "
                           "row must be a struct this contract declares -- wrap the value in one");
                     }
                  }
               }
            }
            return true;
         }
         inline void set_contract_class(const CXXRecordDecl* decl) {
            contract_class = decl;
         }
   };

   class contract_class_finder : public RecursiveASTVisitor<contract_class_finder> {
   private:
      abigen& ag = abigen::get();
      const clang::CXXRecordDecl* contract_class = nullptr;
   public:
      virtual bool VisitCXXRecordDecl(clang::CXXRecordDecl* _cxx_decl) {
         auto cxx_decl = clang_wrapper::wrap_decl(_cxx_decl);
         if (cxx_decl.isSysioContract()) {
            bool is_sysio_contract = false;
            // on this point it could be just an attribute so let's check base classes
            for (const auto& base : _cxx_decl->bases()) {
               if (const clang::Type *base_type = base.getType().getTypePtrOrNull()) {
                  if (const auto* cur_cxx_decl = base_type->getAsCXXRecordDecl()) {
                     if (cur_cxx_decl->getQualifiedNameAsString() == "sysio::contract") {
                        is_sysio_contract = true;
                        break;
                     }
                  }
               }
            }
            if (!is_sysio_contract)
               return true;

            auto attr_name = cxx_decl.getSysioContractAttr()->getName();
            auto name = attr_name.empty() ? _cxx_decl->getName() : attr_name;
            // When contract name is empty (auto-detect mode), accept any contract class
            if (ag.get_contract_name().empty() || name == llvm::StringRef(ag.get_contract_name())) {
               contract_class = _cxx_decl;
               return false;
            }
         }

         return true;
      }
      inline bool contract_found() const {
         return contract_class != nullptr;
      }
      inline const clang::CXXRecordDecl* get_contract() const {
         return contract_class;
      }
   };

   class sysio_abigen_consumer : public ASTConsumer {
      private:
         sysio_abigen_visitor *visitor;
         std::string main_file;
         CompilerInstance* ci;

      public:
         explicit sysio_abigen_consumer(CompilerInstance *CI, std::string file)
            : visitor(new sysio_abigen_visitor(CI)), main_file(file), ci(CI) { }

         virtual void HandleTranslationUnit(ASTContext &Context) {
            if (abigen::get().no_abigen || output.empty()) {
               return;
            }
            auto& src_mgr = Context.getSourceManager();
            abigen::get().source_manager = &src_mgr;
            auto& f_mgr = src_mgr.getFileManager();
            // LLVM 18: getOptionalFileRef returns Optional<FileEntryRef>
            auto main_fe = f_mgr.getOptionalFileRef(main_file);
            if (main_fe) {
               auto fid = src_mgr.getOrCreateFileID(*main_fe, SrcMgr::CharacteristicKind::C_User);
               contract_class_finder cf;
               cf.TraverseDecl(Context.getTranslationUnitDecl());
               if (cf.contract_found()) {
                  visitor->set_contract_class(cf.get_contract());
                  // In auto-detect mode (no explicit contract name), set the contract name
                  // from the discovered contract class so ricardian file lookup works.
                  if (abigen::get().get_contract_name().empty()) {
                     auto decl = clang_wrapper::wrap_decl(cf.get_contract());
                     auto attr_name = decl.getSysioContractAttr()->getName();
                     auto name = attr_name.empty() ? cf.get_contract()->getName() : attr_name;
                     abigen::get().set_contract_name(name.str());
                  }
               }
               // Always traverse the TU for ABI data, even when the contract class
               // was not found in this TU.  The old (cdt-llvm) abigen did this
               // unconditionally; skipping it drops types referenced by actions
               // defined in other TUs of the same contract.
               visitor->TraverseDecl(Context.getTranslationUnitDecl());

               std::ofstream ofs(output + ".desc");
               if (!ofs) throw;
               // Write desc file if ABI has content, the contract class was found,
               // or the codegen plugin added wasm actions/notifies/entries.
               // The codegen plugin runs in auto-detect mode and may find actions
               // even when the abigen's contract name filter doesn't match.
               if (!abigen::get().is_empty() || cf.contract_found() || abigen::get().has_wasm_data())
                  ofs << pretty_print(abigen::get().to_json_debug());
               ofs.close();
            }
         }
   };

   class sysio_abigen_frontend_action : public PluginASTAction {
      public:
         std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) override {
            CI.getPreprocessor().addPPCallbacks(std::make_unique<sysio_ppcallbacks>(CI.getSourceManager(), file.str()));
            return std::make_unique<sysio_abigen_consumer>(&CI, file.str());
         }

         bool ParseArgs(const CompilerInstance& CI, const std::vector<std::string>& args) override {
            if (args.empty())
               return true;

            std::vector<std::string> resource_dirs;
            for (const auto& arg : sysio_plugin::tokenize(args[0])) {
               if (sysio::cdt::starts_with(arg, "contract=")) {
                  abigen::get().set_contract_name(arg.substr(arg.find("=")+1));
               } else if (sysio::cdt::starts_with(arg, "output=")) {
                  output = arg.substr(arg.find("=")+1);
               } else if (sysio::cdt::starts_with(arg, "abi_version=")) {
                  auto str = arg.substr(arg.find("=")+1);
                  int  abi_version_major = abi_version::default_major;
                  int  abi_version_minor = abi_version::default_minor;
                  if (!abi_version::parse(str, abi_version_major, abi_version_minor)) {
                     llvm::errs() << "sysio_abigen: invalid abi_version '" << str
                                  << "': expected <major>[.<minor>]\n";
                     return false;
                  }
                  abigen::get().set_abi_version(abi_version_major, abi_version_minor);
               } else if (arg == "no_abigen") {
                  abigen::get().no_abigen = true;
               } else if (arg == "suppress_ricardian_warnings") {
                  abigen::get().set_suppress_ricardian_warning(true);
               } else if (sysio::cdt::starts_with(arg, "R=")) {
                  resource_dirs.emplace_back(arg.substr(arg.find("=")+1));
               } else if (sysio::cdt::starts_with(arg, "is_wasm=")) {
                  abigen::get().is_wasm = arg.substr(arg.find("=")+1) == "true";
               } else {
                  return false;
               }
            }
            if (resource_dirs.size()) {
               abigen::get().set_resource_dirs(resource_dirs);
            }
            return true;
         }

         ActionType getActionType() override {
            return AddBeforeMainAction;
         }
   };
}} // ns sysio::cdt
