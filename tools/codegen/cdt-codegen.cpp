#include <cstdint>
#include <sysio/abi.hpp>
#include <sysio/abimerge.hpp>
#include <sysio/whereami/whereami.hpp>

#include <fstream>
#include <set>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>
#include <llvm/Support/Program.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#include <jsoncons/json.hpp>
#pragma GCC diagnostic pop

using jsoncons::ojson;

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

// Write dispatch code (apply entry point) to an output stream.
// When weak=true, apply() gets __attribute__((weak)) so a user-defined apply()
// (e.g. from SYSIO_DISPATCH) takes priority at link time.
// When weak=false (standalone dispatch.cpp for link-mode builds), no weak attr.
static void write_sysio_dispatch(std::ostream& ofs, const std::set<wasm_action>& wasm_actions,
                                 const std::set<wasm_notify>& wasm_notifies, bool weak) {
   ofs << "extern \"C\" {\n";
   ofs << "  __attribute__((import_name(\"sysio_assert_code\"))) void sysio_assert_code(uint32_t, uint64_t);";
   ofs << "  void sysio_set_contract_name(uint64_t n);\n";
   for (auto& wa : wasm_actions) {
      ofs << "  void " << wa.handler << "(uint64_t r, uint64_t c);\n";
   }
   for (auto& wn : wasm_notifies) {
      ofs << "  void " << wn.handler << "(uint64_t r, uint64_t c);\n";
   }
   if (weak)
      ofs << "  __attribute__((weak, export_name(\"apply\"), visibility(\"default\")))\n";
   else
      ofs << "  __attribute__((export_name(\"apply\"), visibility(\"default\")))\n";
   ofs << "  void apply(uint64_t r, uint64_t c, uint64_t a) {\n";
   ofs << "    sysio_set_contract_name(r);\n";
   ofs << "    if (c == r) {\n";
   if (wasm_actions.size()) {
      ofs << "      switch (a) {\n";
      for (auto& wa : wasm_actions) {
         ofs << "      case \"" << wa.name << "\"_n.value:\n";
         ofs << "        " << wa.handler << "(r, c);\n";
         ofs << "        break;\n";
      }
      ofs << "      default:\n"
          << "        if ( r != \"sysio\"_n.value) sysio_assert_code(false, 1);\n"
          << "      }\n";
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
   }
   ofs << "    }\n";
   ofs << "  }\n";
   ofs << "}\n";
}

// Generate a standalone dispatch.cpp file (for link-mode builds via cdt-cpp).
static void generate_sysio_dispatch(const std::string& output, const std::set<wasm_action>& wasm_actions,
                                    const std::set<wasm_notify>& wasm_notifies) {
   try {
      std::ofstream ofs(output);
      if (!ofs)
         throw std::runtime_error("cannot open " + output);
      ofs << "#include <cstdint>\n"
          << "#include <sysio/name.hpp>\n";
      write_sysio_dispatch(ofs, wasm_actions, wasm_notifies, false);
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
static bool        embed_dispatch              = false;
static bool        verbose                     = false;
static bool        suppress_ricardian_warnings = true;
static bool        is_wasm                     = false;
static std::string smart_contract_trace_level;

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
      } else if (arg == "--embed-dispatch") {
         embed_dispatch = true;
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
      } else if (arg[0] == '-') {
         std::cerr << "Unknown option: " << arg << "\n";
         print_usage(argv[0]);
         exit(1);
      } else {
         input_files.push_back(arg);
      }
   }

   if (cxx_arg.empty()) {
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

   // Only pass contract name to plugins when explicitly specified.
   // When auto-derived from filename, let plugins accept any contract class.
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

   auto desc_file = _output + ".desc";

   if (exists(desc_file.c_str())) {
      desc_files.push_back(desc_file);
   }
}

int main(int argc, const char** argv) {
   std::set<wasm_action> wasm_actions;
   std::set<wasm_notify> wasm_notifies;
   bool                  dispatcher_was_found = false;

   parse_args(argc, argv);

   try {
      for (auto& input : input_files) {
         gen_actions(input);
      }

      ojson abi;

      for (const auto& desc_name : desc_files) {
         if (exists(desc_name.c_str())) {
            if (file_size(desc_name.c_str()) == 0)
               continue;
            std::ifstream ifs(desc_name);
            auto          desc = ojson::parse(ifs);
            ifs.close();

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

      // Only generate dispatch if there are actions/notifies to dispatch
      // AND the source doesn't already define its own apply() (e.g. via SYSIO_DISPATCH macro).
      if ((!wasm_actions.empty() || !wasm_notifies.empty()) && !dispatcher_was_found) {
         if (embed_dispatch) {
            // Embed weak dispatch code into each .actions.cpp file.
            // This avoids a separate dispatch.o and the wasm-ld --relocatable merge,
            // which can't handle weak/strong symbol resolution.
            // At final link time, wasm-ld properly resolves weak vs strong apply().
            for (auto& input : input_files) {
               std::string actions_file = output_dir + "/" + input.substr(input.rfind('/') + 1) + ".actions.cpp";
               if (exists(actions_file.c_str())) {
                  std::ofstream ofs(actions_file, std::ios::app);
                  if (ofs) {
                     ofs << "\n// --- Auto-generated weak dispatch ---\n";
                     write_sysio_dispatch(ofs, wasm_actions, wasm_notifies, true);
                     ofs.close();
                  }
               }
            }
         } else {
            // Standalone dispatch.cpp for link-mode builds (cdt-cpp handles compilation).
            auto main_file = output_dir + "/" + contract_name + ".dispatch.cpp";
            generate_sysio_dispatch(main_file, wasm_actions, wasm_notifies);
         }
      }
      return 0;
   } catch (std::runtime_error& err) {
      std::cerr << err.what() << '\n';
      return -1;
   }
}
