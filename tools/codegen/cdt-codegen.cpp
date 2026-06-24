#include <cstdint>
#include <sysio/abi.hpp>
#include <sysio/abimerge.hpp>
#include <sysio/whereami/whereami.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <llvm/Support/Program.h>

#include <absl/strings/string_view.h>
#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/util/json_util.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#include <jsoncons/json.hpp>
#pragma GCC diagnostic pop

using jsoncons::ojson;

namespace gpb = google::protobuf;
namespace gpbc = google::protobuf::compiler;

class ProtobufErrorCollector : public gpbc::MultiFileErrorCollector {
 public:
   void RecordError(absl::string_view filename, int line, int column, absl::string_view message) override {
      std::cerr << "protobuf error: " << filename << " (" << line << ", " << column << ")  " << message << std::endl;
   }
};

#ifndef CDT_VERSION
#define CDT_VERSION "5.2.0"
#endif

#ifndef SHARED_LIB_SUFFIX
#ifdef __APPLE__
#define SHARED_LIB_SUFFIX ".dylib"
#else
#define SHARED_LIB_SUFFIX ".so"
#endif
#endif

static int file_size(const char* filename) {
   struct stat st;
   stat(filename, &st);
   return st.st_size;
}

static bool exists(const char* filename) {
   struct stat st;
   return stat(filename, &st) == 0;
}

// Write dispatch code (apply entry point) to an output stream. apply() is a single
// strong export; the generated dispatcher is the only one in the link (a contract that
// defines its own apply() suppresses generation upstream via dispatcher_was_found).
static void write_sysio_dispatch(std::ostream& ofs, const std::set<wasm_action>& wasm_actions,
                                 const std::set<wasm_notify>& wasm_notifies,
                                 bool has_pre_dispatch, bool has_post_dispatch) {
   ofs << "extern \"C\" {\n";
   ofs << "  __attribute__((import_name(\"sysio_assert_code\"))) void sysio_assert_code(uint32_t, uint64_t);";
   ofs << "  void sysio_set_contract_name(uint64_t n);\n";
   ofs << "}\n"; // close extern "C" for intrinsic declarations
   if (has_pre_dispatch)
      ofs << "extern \"C\" bool pre_dispatch(sysio::name, sysio::name, sysio::name);\n";
   if (has_post_dispatch)
      ofs << "extern \"C\" void post_dispatch(sysio::name, sysio::name, sysio::name);\n";
   ofs << "extern \"C\" {\n";
   for (auto& wa : wasm_actions) {
      ofs << "  void " << wa.handler << "(uint64_t r, uint64_t c);\n";
   }
   for (auto& wn : wasm_notifies) {
      ofs << "  void " << wn.handler << "(uint64_t r, uint64_t c);\n";
   }
   ofs << "  __attribute__((export_name(\"apply\"), visibility(\"default\")))\n";
   ofs << "  void apply(uint64_t r, uint64_t c, uint64_t a) {\n";
   ofs << "    sysio_set_contract_name(r);\n";
   if (has_pre_dispatch)
      ofs << "    if (!pre_dispatch(sysio::name{r}, sysio::name{c}, sysio::name{a})) return;\n";
   ofs << "    if (c == r) {\n";
   if (wasm_actions.size()) {
      ofs << "      switch (a) {\n";
      for (auto& wa : wasm_actions) {
         ofs << "      case \"" << wa.name << "\"_n.value:\n";
         ofs << "        " << wa.handler << "(r, c);\n";
         ofs << "        break;\n";
      }
      ofs << "      default:\n";
      if (has_post_dispatch) {
         ofs << "        if (r != \"sysio\"_n.value) sysio_assert_code(false, 1);\n";
         ofs << "        else post_dispatch(sysio::name{r}, sysio::name{c}, sysio::name{a});\n";
      } else {
         ofs << "        if (r != \"sysio\"_n.value) sysio_assert_code(false, 1);\n";
      }
      ofs << "      }\n";
   }
   ofs << "    } else {\n";
   if (wasm_notifies.size()) {
      std::string action;
      for (auto& wn : wasm_notifies) {
         if (wn.name != action) {
            if (action.empty()) {
               ofs << "      if (a == \"" << wn.name << "\"_n.value) {\n";
            } else {
               ofs << "        }\n";
               ofs << "      }\n";
               ofs << "      else if (a == \"" << wn.name << "\"_n.value) {\n";
            }
            ofs << "        switch (c) {\n";
            action = wn.name;
         }
         if (wn.contract != "*")
            ofs << "        case \"" << wn.contract << "\"_n.value:\n";
         else
            ofs << "        default:\n";
         ofs << "          " << wn.handler << "(r, c);\n";
         ofs << "          break;\n";
      }
      ofs << "        }\n";
      ofs << "      }\n";
      if (has_post_dispatch)
         ofs << "      else { post_dispatch(sysio::name{r}, sysio::name{c}, sysio::name{a}); }\n";
   } else if (has_post_dispatch) {
      ofs << "      post_dispatch(sysio::name{r}, sysio::name{c}, sysio::name{a});\n";
   }
   ofs << "    }\n";
   ofs << "  }\n";
   ofs << "}\n";
}

// Generate a standalone dispatch.cpp file (for link-mode builds via cdt-cpp).
static void generate_sysio_dispatch(const std::string& output, const std::set<wasm_action>& wasm_actions,
                                    const std::set<wasm_notify>& wasm_notifies,
                                    bool has_pre_dispatch, bool has_post_dispatch) {
   try {
      std::ofstream ofs(output);
      if (!ofs)
         throw std::runtime_error("cannot open " + output);
      ofs << "#include <cstdint>\n"
          << "#include <sysio/name.hpp>\n";
      write_sysio_dispatch(ofs, wasm_actions, wasm_notifies, has_pre_dispatch, has_post_dispatch);
      ofs.close();
   } catch (...) {
      std::cerr << "Failed to generate sysio dispatcher\n";
   }
}

static std::vector<std::string> split_then_prepend(const std::string& s, char delim, std::string prefix) {
   std::vector<std::string> result;
   std::stringstream        ss(s);
   std::string              item;

   while (std::getline(ss, item, delim)) {
      if (item.size())
         result.push_back(prefix + item);
   }

   return result;
}

static std::vector<std::string> desc_files;
static std::vector<std::string> input_files;
static std::vector<std::string> resource_dirs;
static std::vector<std::string> compiler_options;
static std::string              contract_name;
static bool                     explicit_contract = false;
static std::string              output_dir = ".";

static std::string abi_version;
static int         abi_version_major           = 1;
static int         abi_version_minor           = 3;
static bool        no_abigen                   = false;
static std::string abi_output_path;
// Link-time finalize split (see main()):
//   emit_desc_only -- per-TU compile pass: emit this TU's .desc/.actions.cpp and a
//                     small finalize manifest, then stop. No shared .abi/dispatch,
//                     no sibling-.desc scan (so no parallel-build races).
//   finalize_mode  -- the single link-time pass (run by cdt-ld): skip compilation,
//                     scan every .desc, and publish the complete .abi + a strong
//                     standalone dispatch exactly once.
static bool        emit_desc_only              = false;
static bool        finalize_mode               = false;
// In --finalize mode, cdt-ld passes the exact descriptor list (one --desc-file per linked
// object) and the output path for the generated dispatcher. When desc files are given
// explicitly we use them verbatim and skip the output-directory scan -- so only descriptors
// for objects actually linked are merged (no stale/sibling .desc, and sources spread across
// subdirectories are handled).
static std::vector<std::string> explicit_desc_files;
static std::string              dispatch_output_path;
static bool        verbose                     = false;
static bool        suppress_ricardian_warnings = true;
static bool        is_wasm                     = false;
static std::string smart_contract_trace_level;
static std::string              protobuf_dir;
static std::vector<std::string> protobuf_files;

static int exec_subprogram(std::string prog, const std::vector<std::string>& options, bool show_commands) {
   if (prog.size() && prog[0] != '/') {
      prog = sysio::cdt::whereami::where() + "/" + prog;
   }

#if defined(__APPLE__)
   struct stat sb;
   if (lstat(prog.c_str(), &sb) != -1) {
      std::vector<char> buf(sb.st_size + 1);
      ssize_t nbytes = readlink(prog.c_str(), buf.data(), buf.size());
      if (nbytes != -1) {
         prog.assign(buf.data(), nbytes);
      }
   }
#endif

   if (show_commands) {
      std::cout << prog;
      for (const auto& s : options)
         std::cout << " " << s;
      std::cout << "\n";
   }

   std::vector<llvm::StringRef> args;
   args.push_back(prog);
   args.insert(args.end(), options.begin(), options.end());
   return llvm::sys::ExecuteAndWait(prog.c_str(), args);
}

static void print_usage(const char* prog) {
   std::cout << "Usage: " << prog << " [options] <input files...>\n"
             << "\nOptions:\n"
             << "  --contract NAME             Contract name\n"
             << "  --output-dir DIR            Output directory (default: .)\n"
             << "  --abi-version VERSION        ABI version (e.g. 1.3)\n"
             << "  --no-abigen                 Disable ABI generation\n"
             << "  --cxx OPTIONS               Additional C++ compiler options\n"
             << "  -I, --include DIR           C++ include directory (repeatable)\n"
             << "  -D DEFS                     C preprocessor macros (semicolon-separated)\n"
             << "  -R DIR                      Resource directory (repeatable)\n"
             << "  --smart-contract-trace-level LEVEL  Trace level\n"
             << "  -v, --verbose               Verbose output\n"
             << "  --version                   Display version\n"
             << "  -h, --help                  Print this help\n";
}

static void parse_args(int argc, const char** argv) {
   std::string cxx_arg;

   for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-h" || arg == "--help") {
         print_usage(argv[0]);
         exit(0);
      } else if (arg == "--version") {
         std::cout << "cdt-codegen version " << CDT_VERSION << "\n";
         exit(0);
      } else if (arg == "--no-abigen") {
         no_abigen = true;
      } else if (arg == "--abi-output" && i + 1 < argc) {
         abi_output_path = argv[++i];
      } else if (arg == "--desc-file" && i + 1 < argc) {
         explicit_desc_files.push_back(argv[++i]);
      } else if (arg == "--dispatch-output" && i + 1 < argc) {
         dispatch_output_path = argv[++i];
      } else if (arg == "-v" || arg == "--verbose") {
         verbose = true;
      } else if (arg == "--contract" && i + 1 < argc) {
         contract_name = argv[++i];
         explicit_contract = true;
      } else if (arg == "--output-dir" && i + 1 < argc) {
         output_dir = argv[++i];
      } else if (arg == "--abi-version" && i + 1 < argc) {
         abi_version = argv[++i];
         float tmp;
         abi_version_major = std::stoi(abi_version);
         abi_version_minor = (int)(std::modf(std::stof(abi_version), &tmp) * 10);
      } else if (arg == "--cxx" && i + 1 < argc) {
         cxx_arg = argv[++i];
      } else if ((arg == "-I" || arg == "--include") && i + 1 < argc) {
         auto args = split_then_prepend(argv[++i], ';', "-I");
         compiler_options.insert(compiler_options.end(), args.begin(), args.end());
      } else if (arg == "-D" && i + 1 < argc) {
         auto args = split_then_prepend(argv[++i], ';', "-D");
         compiler_options.insert(compiler_options.end(), args.begin(), args.end());
      } else if (arg == "-R" && i + 1 < argc) {
         resource_dirs.push_back(argv[++i]);
      } else if (arg == "--smart-contract-trace-level" && i + 1 < argc) {
         smart_contract_trace_level = argv[++i];
      } else if (arg == "--protobuf-dir" && i + 1 < argc) {
         protobuf_dir = argv[++i];
      } else if (arg == "--protobuf-files" && i + 1 < argc) {
         // Semicolon-separated list of .proto files
         std::stringstream ss(argv[++i]);
         std::string item;
         while (std::getline(ss, item, ';')) {
            if (!item.empty())
               protobuf_files.push_back(item);
         }
      } else if (arg == "--emit-desc-only") {
         emit_desc_only = true;
      } else if (arg == "--finalize") {
         finalize_mode = true;
      } else if (arg[0] == '-') {
         std::cerr << "Unknown option: " << arg << "\n";
         print_usage(argv[0]);
         exit(1);
      } else {
         input_files.push_back(arg);
      }
   }

   // The finalize pass does not compile anything (it only merges existing .desc files),
   // so it needs no --cxx options.
   if (!finalize_mode && cxx_arg.empty()) {
      const char* env = getenv("SYSIO_CXX_OPTIONS");
      if (env)
         cxx_arg = env;
      if (cxx_arg.empty()) {
         std::cerr << "You must explicitly specify --cxx option or set environment variable SYSIO_CXX_OPTIONS\n";
         exit(1);
      }
   }
   auto args = split_then_prepend(cxx_arg, ' ', "");
   compiler_options.insert(compiler_options.end(), args.begin(), args.end());
   is_wasm = cxx_arg.find("--target=wasm") != std::string::npos;

   if (compiler_options.empty() || std::none_of(compiler_options.begin(), compiler_options.end(),
                                                [](const std::string& s) { return s.substr(0, 2) == "-I"; })) {
      compiler_options.push_back("-I" + sysio::cdt::whereami::where() + "/../include");
   }

   if (contract_name.empty()) {
      if (input_files.size() == 1) {
         auto fn       = input_files[0];
         fn            = fn.substr(fn.rfind('/') + 1);
         contract_name = fn.substr(0, fn.rfind('.'));
      } else {
         std::cerr << "missing --contract argument\n";
         exit(1);
      }
   }
}

static void gen_actions(const std::string& input) {
   std::string              _output = output_dir + "/" + input.substr(input.rfind('/') + 1);
   std::vector<std::string> local_args;
   // Copy compiler options, filtering out -fplugin and plugin-related -Xclang flags
   // that may have been inherited from compiler_options. cdt-codegen loads
   // its own plugins with specific args below.
   for (size_t i = 0; i < compiler_options.size(); ++i) {
      const auto& opt = compiler_options[i];
      if (opt.find("-fplugin=") == 0) continue;
      // Skip -Xclang -plugin-arg-* -Xclang <value> sequences
      if (opt == "-Xclang" && i + 1 < compiler_options.size() &&
          compiler_options[i+1].find("-plugin-arg-") == 0) {
         i += 3; // skip -Xclang, -plugin-arg-*, -Xclang, <value>
         continue;
      }
      // Skip -Xclang -add-plugin -Xclang <name> sequences
      if (opt == "-Xclang" && i + 1 < compiler_options.size() &&
          compiler_options[i+1] == "-add-plugin") {
         i += 3; // skip -Xclang, -add-plugin, -Xclang, <name>
         continue;
      }
      local_args.push_back(opt);
   }
   local_args.push_back("-Wno-unknown-attributes");
   local_args.push_back("-fsyntax-only");
   local_args.emplace_back("-fplugin=" + sysio::cdt::whereami::where() + "/sysio_attrs" SHARED_LIB_SUFFIX);
   local_args.push_back("-fplugin=" + sysio::cdt::whereami::where() + "/sysio_codegen" SHARED_LIB_SUFFIX);

   std::string codegen_opts;
   codegen_opts += "output=" + _output;

   // Only pass contract name to the codegen plugin when explicitly specified.
   // When auto-derived from filename, the name may not match the actual
   // [[sysio::contract("...")]] attribute, so let the plugin accept any contract.
   if (explicit_contract && contract_name.size()) {
      codegen_opts += ",contract=" + contract_name;
   }
   if (smart_contract_trace_level.size()) {
      codegen_opts += ",smart-contract-trace-level=" + smart_contract_trace_level;
   }

   local_args.push_back("-Xclang");
   local_args.push_back("-plugin-arg-sysio_codegen");
   local_args.push_back("-Xclang");
   local_args.push_back(codegen_opts);

   std::string abigen_opts;
   // Always pass contract name to abigen for correct ABI filtering.
   // Even when auto-derived from filename, abigen needs it to select the
   // right contract class (avoiding picking up unrelated contracts from headers).
   if (contract_name.size()) {
      abigen_opts += "contract=" + contract_name;
   }
   if (abigen_opts.size())
      abigen_opts += ",";

   if (abi_version.size()) {
      abigen_opts += "abi_version=" + abi_version;
   } else if (no_abigen) {
      abigen_opts += "no_abigen";
   } else {
      abigen_opts += "abi_version=1.3";
   }

   if (suppress_ricardian_warnings) {
      if (abigen_opts.size())
         abigen_opts += ",";
      abigen_opts += "suppress_ricardian_warnings";
   }
   if (resource_dirs.size()) {
      for (const auto& r : resource_dirs) {
         if (abigen_opts.size())
            abigen_opts += ",";
         abigen_opts += "R=" + r;
      }
   }

   if (is_wasm) {
      abigen_opts += ",is_wasm=true";
   }

   local_args.push_back("-Xclang");
   local_args.push_back("-plugin-arg-sysio_abigen");
   local_args.push_back("-Xclang");
   local_args.push_back(abigen_opts);
   local_args.push_back("-c");
   local_args.push_back(input);

   if (auto ret = exec_subprogram("clang++", local_args, verbose)) {
      exit(ret);
   }

   // The abigen plugin writes to <output>.desc. Rename it with a contract-name
   // prefix so that directory scanning can distinguish desc files belonging to
   // different contracts that share the same output directory.
   auto raw_desc = _output + ".desc";
   auto basename = input.substr(input.rfind('/') + 1);
   auto desc_file = output_dir + "/" + contract_name + "." + basename + ".desc";
   if (exists(raw_desc.c_str())) {
      rename(raw_desc.c_str(), desc_file.c_str());
      // Stamp the originating source path into the .desc so a later build can
      // detect it is stale when the source .cpp has been removed or moved.
      // Without this marker, a stale .desc gets merged into the contract ABI
      // and produces "ABI structs malformed : <name> already defined" errors.
      // Only attempt the JSON round-trip when abigen actually produced content;
      // a failed abigen run leaves an empty .desc that ojson::parse would reject.
      if (file_size(desc_file.c_str()) > 0) {
         std::ifstream ifs(desc_file);
         auto desc = ojson::parse(ifs);
         ifs.close();
         desc["____source_file"] = input;
         std::ofstream ofs(desc_file);
         ofs << desc.to_string();
      }
      desc_files.push_back(desc_file);
   }
}

int main(int argc, const char** argv) {
   std::set<wasm_action> wasm_actions;
   std::set<wasm_notify> wasm_notifies;
   bool                  dispatcher_was_found = false;
   bool                  has_pre_dispatch     = false;
   bool                  has_post_dispatch    = false;

   parse_args(argc, argv);

   try {
      // The per-TU compile pass emits this TU's descriptor; the link-time finalize pass
      // (run by cdt-ld) skips compilation and only publishes the merged outputs.
      if (!finalize_mode) {
         for (auto& input : input_files) {
            gen_actions(input);
         }
      }

      // Compile-only contract members stop here: the abigen plugin has written this TU's
      // .desc and .actions.cpp. The shared <contract>.abi and dispatch are NOT produced
      // per-TU -- cdt-ld finalizes them once, after every .desc exists, by invoking
      // `cdt-codegen --finalize`. This eliminates the parallel-build races: no process
      // reads a sibling's .desc during compilation, and the shared outputs are written
      // exactly once. (cdt-cpp writes the finalize manifest that hands cdt-ld the args.)
      if (emit_desc_only)
         return 0;

      // The link-time finalize pass (cdt-ld) supplies the exact descriptor set via repeated
      // --desc-file options -- one per object actually linked -- so use that list verbatim. It
      // is authoritative: it excludes descriptors from removed sources and includes those in
      // any source subdirectory, neither of which a single-directory scan handles correctly.
      if (!explicit_desc_files.empty()) {
         desc_files = explicit_desc_files;
      } else {
         // Fallback (e.g. cdt-cpp link-mode): scan the output directory for .desc files from
         // previous compilations of other TUs in the same contract. Desc files are prefixed
         // with the contract name (e.g. "sysio.system.peer_keys.cpp.desc") to avoid merging
         // unrelated contracts that might share the same output directory.
         std::string prefix = contract_name + ".";
         std::string suffix = ".desc";
         std::set<std::string> known(desc_files.begin(), desc_files.end());
         if (DIR* dir = opendir(output_dir.c_str())) {
            while (struct dirent* ent = readdir(dir)) {
               std::string name = ent->d_name;
               if (name.size() > prefix.size() + suffix.size() &&
                   name.substr(0, prefix.size()) == prefix &&
                   name.substr(name.size() - suffix.size()) == suffix) {
                  std::string path = output_dir + "/" + name;
                  if (known.find(path) == known.end() &&
                      exists(path.c_str()) && file_size(path.c_str()) > 0) {
                     desc_files.push_back(path);
                  }
               }
            }
            closedir(dir);
         }
      }

      // Directory iteration order is filesystem-dependent. Keep .desc merge
      // order stable so ABI output is reproducible across platforms.
      std::sort(desc_files.begin(), desc_files.end());
      desc_files.erase(std::unique(desc_files.begin(), desc_files.end()), desc_files.end());

      ojson abi;

      for (const auto& desc_name : desc_files) {
         if (exists(desc_name.c_str())) {
            if (file_size(desc_name.c_str()) == 0)
               continue;
            std::ifstream ifs(desc_name);
            auto          desc = ojson::parse(ifs);
            ifs.close();

            // If the .desc records its originating source file and that
            // source no longer exists, the .desc is stale (from a removed
            // or renamed .cpp). Merging it in produces duplicate struct
            // definitions, so skip and delete it to keep the build dir clean.
            if (desc.has_key("____source_file")) {
               const std::string src = desc["____source_file"].as_string();
               if (!src.empty() && !exists(src.c_str())) {
                  unlink(desc_name.c_str());
                  continue;
               }
            }

            abi = ABIMerger(abi, abi_version_major, abi_version_minor).merge(desc);

            for (auto wa : desc["wasm_actions"].array_range()) {
               wasm_action act;
               act.name    = wa["name"].as_string();
               act.handler = wa["handler"].as_string();
               wasm_actions.insert(act);
            }
            for (auto wn : desc["wasm_notifies"].array_range()) {
               wasm_notify noti;
               noti.contract = wn["contract"].as_string();
               noti.name     = wn["name"].as_string();
               noti.handler  = wn["handler"].as_string();
               wasm_notifies.insert(noti);
            }

            if (!dispatcher_was_found) {
               for (auto we : desc["wasm_entries"].array_range()) {
                  auto name = we.as_string();
                  if (name == "apply") {
                     dispatcher_was_found = true;
                     break;
                  }
               }
            }
            if (desc.has_key("has_pre_dispatch") && desc["has_pre_dispatch"].as_bool())
               has_pre_dispatch = true;
            if (desc.has_key("has_post_dispatch") && desc["has_post_dispatch"].as_bool())
               has_post_dispatch = true;
         }
      }

      // Validate table_id uniqueness across all tables and secondary indexes.
      // Two different tables/indices sharing the same table_id would corrupt data.
      if (abi.has_key("tables") && abi["tables"].size() > 1) {
         std::map<uint64_t, std::string> seen_ids; // table_id -> owner name
         for (const auto& tbl : abi["tables"].array_range()) {
            if (tbl.has_key("table_id")) {
               auto tid = tbl["table_id"].as<uint64_t>();
               auto tname = tbl["name"].as<std::string>();
               auto [it, inserted] = seen_ids.emplace(tid, tname);
               if (!inserted) {
                  throw std::runtime_error(
                     "table_id collision: '" + it->second + "' and '" + tname +
                     "' both have table_id " + std::to_string(tid) +
                     ". Rename one of the tables to avoid the collision.");
               }
               if (tbl.has_key("secondary_indexes")) {
                  for (const auto& si : tbl["secondary_indexes"].array_range()) {
                     if (si.has_key("table_id")) {
                        auto sid = si["table_id"].as<uint64_t>();
                        auto sname = tname + "." + si["name"].as<std::string>();
                        auto [sit, sins] = seen_ids.emplace(sid, sname);
                        if (!sins) {
                           throw std::runtime_error(
                              "table_id collision: '" + sit->second + "' and '" + sname +
                              "' both have table_id " + std::to_string(sid) +
                              ". Rename one of the tables/indexes to avoid the collision.");
                        }
                     }
                  }
               }
            }
         }
      }

      // Collect pb_types referenced by actions from desc files
      std::set<std::string> referenced_pb_types;
      for (const auto& desc_name : desc_files) {
         if (exists(desc_name.c_str()) && file_size(desc_name.c_str()) > 0) {
            std::ifstream ifs2(desc_name);
            auto desc = ojson::parse(ifs2);
            ifs2.close();
            if (desc.has_key("pb_types")) {
               for (auto& pb_type : desc["pb_types"].array_range()) {
                  referenced_pb_types.insert(pb_type.as_string());
               }
            }
         }
      }

      if (!no_abigen) {
         if (abi.empty()) {
            // No [[sysio::contract]] class found — contract may define apply() directly.
            // Skip ABI generation silently; this is not an error.
            if (verbose && explicit_contract) {
               std::cerr << "note: no [[sysio::contract]] class found for '" << contract_name
                         << "', skipping ABI generation\n";
            }
         } else {
            // Embed protobuf FileDescriptorSet into ABI if protobuf files are specified
            if (protobuf_files.size()) {
               gpbc::DiskSourceTree source_tree;
               source_tree.MapPath("", protobuf_dir);
               source_tree.MapPath("", sysio::cdt::whereami::where() + "/../include");

               ProtobufErrorCollector err_collector;
               gpbc::Importer importer(&source_tree, &err_collector);

               for (auto& proto_file : protobuf_files) {
                  importer.Import(proto_file.c_str());
               }

               auto pool = importer.pool();

               for (auto& type : referenced_pb_types) {
                  if (!pool->FindMessageTypeByName(type)) {
                     std::cerr << "unable to find the definition of the protobuf type: '" << type << "',\n"
                                 "please make sure the corresponding protobuf file is correctly specified\n";
                     return -1;
                  }
               }

               gpb::FileDescriptorSet fds;
               for (auto& proto_file : protobuf_files) {
                  auto descriptor = pool->FindFileByName(proto_file);
                  auto file = fds.add_file();
                  descriptor->CopyTo(file);

                  // Remove zpp_options.proto from dependencies (internal use only)
                  for (int i = file->dependency_size() - 1; i >= 0; --i) {
                     if (file->dependency(i).find("zpp_options.proto") != std::string::npos ||
                         file->dependency(i).find("zpp/zpp_options.proto") != std::string::npos) {
                        for (int j = i; j < file->dependency_size() - 1; ++j) {
                           file->mutable_dependency()->SwapElements(j, j + 1);
                        }
                        file->mutable_dependency()->RemoveLast();
                     }
                  }
               }

               std::string protobuf_types_json;
               auto status = gpb::util::MessageToJsonString(fds, &protobuf_types_json);
               if (!status.ok()) {
                  std::cerr << "failed to convert protobuf types to JSON: " << status.message() << "\n";
                  return -1;
               }

               abi["protobuf_types"] = ojson::parse(protobuf_types_json);

               // Bump ABI version to 1.3 when protobuf_types section is present
               if (abi_version_major == 1 && abi_version_minor < 3) {
                  abi_version_minor = 3;
                  abi["version"] = "sysio::abi/1.3";
               }
            } else if (referenced_pb_types.size()) {
               std::cerr << "protobuf types are used but no protobuf files are specified for contract " << contract_name
                         << ", please use `contract_use_protobuf()` cmake function to specify the protobuf files it depends on\n";
               return -1;
            }

            std::string   filename = abi_output_path.empty()
                                   ? output_dir + "/" + contract_name + ".abi"
                                   : abi_output_path;
            std::ofstream ofs(filename);
            if (!ofs) {
               std::cerr << "cannot open " + filename + "\n";
               return -1;
            }
            std::stringstream abi_json;
            abi_json << pretty_print(abi);
            ofs << abi_json.str();
            ofs.close();
         }
      }

      // Delete any dispatcher from a previous run BEFORE deciding whether to regenerate, so the
      // file's presence reliably means "generated this run". Without this, an incremental build
      // in which the contract gained its own apply() (or lost all its actions) would leave a
      // stale <contract>.dispatch.cpp on disk that cdt-ld would then compile and link -- a
      // duplicate or stale strong apply().
      const std::string dispatch_file = !dispatch_output_path.empty()
                                      ? dispatch_output_path
                                      : output_dir + "/" + contract_name + ".dispatch.cpp";
      unlink(dispatch_file.c_str());

      // Only generate dispatch if there are actions/notifies to dispatch
      // AND the source doesn't already define its own apply() (e.g. via SYSIO_DISPATCH macro).
      if ((!wasm_actions.empty() || !wasm_notifies.empty()) && !dispatcher_was_found) {
         // Generate a standalone dispatcher with a single strong apply(). For compile-only
         // contract builds this runs in the link-time finalize pass and cdt-ld compiles and
         // links the result; link-mode builds (cdt-cpp drives compile+link) emit it here and
         // compile it themselves.
         generate_sysio_dispatch(dispatch_file, wasm_actions, wasm_notifies, has_pre_dispatch, has_post_dispatch);
      }
      return 0;
   } catch (std::runtime_error& err) {
      std::cerr << err.what() << '\n';
      return -1;
   }
}
