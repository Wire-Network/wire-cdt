#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/QualTypeNames.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Rewrite/Core/Rewriter.h>

#include <fstream>
#include "tokenize.hpp"
#include "gen.hpp"
#include "abigen.hpp"
#include "tracegen.hpp"

using namespace clang;
using namespace sysio::cdt;

std::string output;
std::set<std::string> actions;
std::set<std::string> notify_handlers;

class sysio_codegen_visitor : public RecursiveASTVisitor<sysio_codegen_visitor>, public generation_utils {
private:
   FileID    main_fid;
   StringRef main_name;
   Rewriter  rewriter;
   CompilerInstance* ci;
   bool      warn_action_read_only = false;
   std::stringstream ss;

public:

   using call_map_t = std::map<FunctionDecl*, std::vector<CallExpr*>>;
   using indirect_func_map_t = std::map<NamedDecl*, FunctionDecl*>;

   std::set<CXXMethodDecl*>    read_only_actions;
   call_map_t                  func_calls;
   indirect_func_map_t         indi_func_map;

   explicit sysio_codegen_visitor(CompilerInstance* CI): ci(CI) {
      get_error_emitter().set_compiler_instance(CI);
   }

   void set_warn_action_read_only(bool w) { warn_action_read_only = w; }

   void set_main_fid(FileID fid) {
      main_fid = fid;
   }

   static llvm::SmallString<PATH_MAX> to_absolute_path(llvm::StringRef path) {
      llvm::SmallString<PATH_MAX> absolute_path = llvm::StringRef(path);
      llvm::sys::fs::make_absolute(absolute_path);
      return absolute_path;
   }

   void save(std::string name) {
      std::string buf = ss.str();
      std::ofstream of(name);
      auto absolute_main_name=to_absolute_path(main_name);
      of << "#pragma clang diagnostic ignored \"-Weverything\"" << "\n";
      of << "#include \"" << std::string_view(absolute_main_name.data(), absolute_main_name.size()) << "\"\n";

      if (buf.size()) {
         of << "#include <sysio/datastream.hpp>\n"
            << "#include <sysio/name.hpp>\n"
            << buf;
      }
   }

   void set_main_name(StringRef mn) {
     main_name = mn;
   }

   template <typename F, typename D>
   void create_dispatch(const std::string& attr, const std::string& func_name, F&& get_str, D decl) {
      constexpr static uint32_t max_stack_size = 512;
      std::string nm = decl->getNameAsString()+"_"+decl->getParent()->getNameAsString();
      if (is_sysio_contract(decl, contract_name)) {
         ss << "\n\nextern \"C\" {\n";
         ss << "  [[clang::import_name(\"action_data_size\")]]\n";
         ss << "  uint32_t action_data_size();\n";
         ss << "  [[clang::import_name(\"read_action_data\")]]\n";
         ss << "  uint32_t read_action_data(void*, uint32_t);\n";
         const auto& return_ty = decl->getReturnType().getAsString();
         if (return_ty != "void") {
            ss << "  [[clang::import_name(\"set_action_return_value\")]]\n";
            ss << "  void set_action_return_value(void*, size_t);\n";
         }
         ss << "  __attribute__((weak))\n";
         ss << "  void " << func_name << nm << "(unsigned long long r, unsigned long long c) {\n";
         ss << "    size_t as = ::action_data_size();\n";
         ss << "    auto free_memory = [as](void* buf) { if (as >= " << max_stack_size << ") free(buf);};\n";
         ss << "    std::unique_ptr<void, decltype(free_memory)> buff{nullptr, free_memory};\n";
         ss << "    if (as > 0) {\n";
         ss << "      buff.reset(as >= " << max_stack_size << " ? malloc(as) : alloca(as));\n";
         ss << "      ::read_action_data(buff.get(), as);\n";
         ss << "    }\n";
         ss << "    sysio::datastream<const char*> ds{(char*)buff.get(), as};\n";
         int i=0;
         for (auto param : decl->parameters()) {
            clang::LangOptions lang_opts;
            lang_opts.CPlusPlus = true;
            lang_opts.Bool = true;
            clang::PrintingPolicy policy(lang_opts);
            auto qt = param->getOriginalType().getNonReferenceType();
            qt.removeLocalConst();
            qt.removeLocalVolatile();
            qt.removeLocalRestrict();
            std::string tn = clang::TypeName::getFullyQualifiedName(qt, ci->getASTContext(), policy);
            ss << "    " << tn << " arg" << i << "; ds >> arg" << i << ";\n";
            i++;
         }
         const auto& call_action = [&]() {
            ss << decl->getParent()->getQualifiedNameAsString() << "{sysio::name{r},sysio::name{c},ds}." << decl->getNameAsString() << "(";
            for (int i=0; i < decl->parameters().size(); i++) {
               ss << "arg" << i;
               if (i < decl->parameters().size()-1)
                  ss << ", ";
            }
            ss << ");\n";
         };
         ss << "    ";
         if (return_ty != "void") {
            ss << "const auto& result = ";
         }
         call_action();
         if (return_ty != "void") {
            ss << "    const auto& packed_result = sysio::pack(result);\n";
            ss << "    set_action_return_value((void*)packed_result.data(), packed_result.size());\n";
         }
         ss << "  }\n";
         ss << "}\n";

      }
   }

   void create_action_dispatch(clang_wrapper::Decl<CXXMethodDecl*> decl) {
      auto func = [](clang_wrapper::Decl<CXXMethodDecl*> d) { return generation_utils::get_action_name(d); };
      create_dispatch("sysio_wasm_action", "__sysio_action_", func, decl);
   }

   void create_action_dispatch(CXXMethodDecl* decl) {
      auto func = [](CXXMethodDecl* d) { return generation_utils::get_action_name(d); };
      create_dispatch("sysio_wasm_action", "__sysio_action_", func, decl);
   }

   void create_notify_dispatch(clang_wrapper::Decl<CXXMethodDecl*> decl) {
      auto func = [](clang_wrapper::Decl<CXXMethodDecl*> d) { return generation_utils::get_notify_pair(d); };
      create_dispatch("sysio_wasm_notify", "__sysio_notify_", func, decl);
   }

   void create_notify_dispatch(CXXMethodDecl* decl) {
      auto func = [](CXXMethodDecl* d) { return generation_utils::get_notify_pair(d); };
      create_dispatch("sysio_wasm_notify", "__sysio_notify_", func, decl);
   }

   virtual bool VisitCXXMethodDecl(CXXMethodDecl* _decl) {
      auto decl = clang_wrapper::wrap_decl(_decl);
      std::string name = decl->getNameAsString();
      static std::set<std::string> _action_set; //used for validations
      static std::set<std::string> _notify_set; //used for validations
      if (decl.isSysioAction() && is_sysio_contract(decl, contract_name)) {
         name = generation_utils::get_action_name(decl);
         validate_name(name, [&](auto s) {
            CDT_ERROR("codegen_error", decl->getLocation(), std::string("action name (")+s+") is not a valid sysio name");
         });

         if (!_action_set.count(name))
            _action_set.insert(name);
         else {
            auto itr = _action_set.find(name);
            CDT_CHECK_ERROR(*itr == name, "codegen_error", decl->getLocation(), "action declaration doesn't match previous declaration");
         }
         std::string full_action_name = "__sysio_action_" + decl->getNameAsString() + ((decl->getParent()) ? "_"+decl->getParent()->getNameAsString() : "");
         if (actions.count(full_action_name) == 0) {
            create_action_dispatch(decl);
            abigen::get().add_wasm_action(decl, full_action_name);
         }
         actions.insert(full_action_name); // insert the method action, so we don't create the dispatcher twice

         if (decl.isSysioReadOnly()) {
            read_only_actions.insert(*decl);
         }
      } else if (decl.isSysioNotify() && is_sysio_contract(decl, contract_name)) {
         name = generation_utils::get_notify_pair(decl);
         auto first = name.substr(0, name.find("::"));
         if (first != "*")
            validate_name(first, [&](auto s) {
               CDT_ERROR("codegen_error", decl->getLocation(), std::string("name (")+s+") is invalid");
            });
         auto second = name.substr(name.find("::")+2);
         validate_name(second, [&](auto s) {
            CDT_ERROR("codegen_error", decl->getLocation(), std::string("name (")+s+") is invalid");
         });

         if (!_notify_set.count(name))
            _notify_set.insert(name);
         else {
            auto itr = _notify_set.find(name);
            CDT_CHECK_ERROR(*itr == name, "codegen_error", decl->getLocation(), "action declaration doesn't match previous declaration");
         }

         std::string full_notify_name = "__sysio_notify_" + decl->getNameAsString() + ((decl->getParent()) ? "_"+decl->getParent()->getNameAsString() : "");
         if (notify_handlers.count(full_notify_name) == 0) {
            create_notify_dispatch(decl);
            abigen::get().add_wasm_notify(decl, full_notify_name);
         }
         notify_handlers.insert(full_notify_name); // insert the method action, so we don't create the dispatcher twice
      }

      return true;
   }

   void process_indi_callee(FunctionDecl* fd, CallExpr *call) {
      if (Expr *expr = call->getCallee()) {
         while (auto* ice = dyn_cast<ImplicitCastExpr>(expr)) {
            expr = ice->getSubExpr();
         }
         if (auto* dre = dyn_cast<DeclRefExpr>(expr)) {
            if (indi_func_map.count(dre->getFoundDecl()) != 0) {
               func_calls[fd].push_back(call);
            }
         } else if (auto* me = dyn_cast<MemberExpr>(expr)) {
            if (indi_func_map.count(me->getMemberDecl()) != 0) {
               func_calls[fd].push_back(call);
            }
         }
      }
   }

   FunctionDecl* get_rhs_fd(Expr *rhs) const {
      while (auto* ice = dyn_cast<ImplicitCastExpr>(rhs)) {
         rhs = ice->getSubExpr();
      }
      if (auto* rhs_dre = dyn_cast<DeclRefExpr>(rhs)) {
         if (auto* fd = dyn_cast<FunctionDecl>(rhs_dre->getFoundDecl())) {
            return fd;
         }
      }
      return nullptr;
   }

   void update_indi_func_map(NamedDecl *nd, FunctionDecl *fd) {
      if (func_calls.count(fd) != 0) {
         indi_func_map[nd] = fd;
      } else if (indi_func_map.count(nd)) {
         indi_func_map.erase(nd);
      }
   }

   void process_decl_init(NamedDecl *nd, Expr *init) {
      if (FunctionDecl *fd = get_rhs_fd(init)) {
         if (func_calls.count(fd) != 0) {
            indi_func_map[nd] = fd;
         }
      }
   }

   void process_function(FunctionDecl* func_decl) {
      if (func_decl->isThisDeclarationADefinition() && func_decl->hasBody()) {
         // Insert an empty entry before traversing the body to prevent infinite
         // recursion on self-recursive functions (e.g. tdestroy).  The count()
         // check below will see this entry and skip the recursive call.
         func_calls.emplace(func_decl, std::vector<CallExpr*>{});
         Stmt *stmts = func_decl->getBody();
         for (auto it = stmts->child_begin(); it != stmts->child_end(); ++it) {
            if (Stmt *s = *it) {
               if (auto* ec = dyn_cast<ExprWithCleanups>(s)) {
                  s = ec->getSubExpr();
                  while (auto* ice = dyn_cast<ImplicitCastExpr>(s))
                     s = ice->getSubExpr();
               }

               if (auto* call = dyn_cast<CallExpr>(s)) {
                  if (FunctionDecl *fd = call->getDirectCallee()) {
                     if (func_calls.count(fd) == 0) {
                        process_function(fd);
                     }
                     if (!func_calls[fd].empty()) {
                        func_calls[func_decl].push_back(call);
                        break;
                     }
                  } else {
                     process_indi_callee(func_decl, call);
                  }
               } else if (auto* ds = dyn_cast<DeclStmt>(s)) {
                  auto process_decl = [this]( DeclStmt *s ) {
                     for (auto it = s->decl_begin(); it != s->decl_end(); ++it) {
                        if (auto* vd = dyn_cast<VarDecl>(*it)) {
                           if (Expr *init = vd->getInit()) {
                              process_decl_init(vd, init);
                           }
                        }
                     }
                  };
                  process_decl(ds);
               } else if (auto* bo = dyn_cast<BinaryOperator>(s)) {
                  auto process_assignment = [this]( BinaryOperator *b ) {
                     Expr *lhs = nullptr, *rhs = nullptr;
                     if ((lhs = b->getLHS()) && (rhs = b->getRHS())) {
                        if (FunctionDecl *fd = get_rhs_fd(rhs)) {
                           if (auto* lhs_dre = dyn_cast<DeclRefExpr>(lhs)) {
                              update_indi_func_map(lhs_dre->getFoundDecl(), fd);
                           } else if (auto* lhs_me = dyn_cast<MemberExpr>(lhs)) {
                              update_indi_func_map(lhs_me->getMemberDecl(), fd);
                           }
                        }
                     }
                  };
                  process_assignment(bo);
               }
            }
         }
      }
   }

   virtual bool VisitFunctionDecl(FunctionDecl* func_decl) {
      if (func_calls.count(func_decl) == 0 && is_write_host_func(func_decl)) {
         func_calls[func_decl] = {(CallExpr*)func_decl};
      } else {
         process_function(func_decl);
      }
      return true;
   }

   virtual bool VisitDecl(clang::Decl* decl) {
      auto _decl = clang_wrapper::wrap_decl(decl);
      if (auto* fd = dyn_cast<clang::FunctionDecl>(decl)) {
         if (fd->getNameInfo().getAsString() == "apply" && _decl.isSysioWasmEntry())
            abigen::get().add_wasm_entries(_decl);
         if (fd->isExternC() && fd->isThisDeclarationADefinition()) {
            auto name = fd->getNameInfo().getAsString();
            if (name == "pre_dispatch")  abigen::get().set_has_pre_dispatch();
            if (name == "post_dispatch") abigen::get().set_has_post_dispatch();
         }
      } else {
         auto process_global_var = [this]( clang::Decl* d ) {
            if (auto* vd = dyn_cast<VarDecl>(d)) {
               if (vd->hasGlobalStorage()) {
                  if (Expr *init = vd->getInit()) {
                     process_decl_init(vd, init);
                  }
               }
            }
         };
         process_global_var(decl); }
      return true;
   }

   virtual bool VisitCXXRecordDecl(CXXRecordDecl* _decl) {
      auto decl = clang_wrapper::wrap_decl(_decl);
      if (decl.isSysioContract()) {
         auto process_data_member = [this]( CXXRecordDecl* rd ) {
            for (auto it = rd->decls_begin(); it != rd->decls_end(); ++it) {
               if (auto* f = dyn_cast<FieldDecl>(*it) ) {
                  if (Expr *init = f->getInClassInitializer()) {
                     process_decl_init(f, init);
                  }
               }
            }
         };
         process_data_member(*decl);
      }
      return true;
   }

   void process_read_only_actions() const {
      for (auto const& ra : read_only_actions) {
         auto it = func_calls.find(ra);
         // process_function() inserts every walked function into func_calls with an
         // initially-empty vector before traversing its body, so a non-end iterator does
         // not by itself mean the action transitively calls a write host function — only
         // a non-empty vector does. Without this guard the validator fires on every
         // read-only action whose body has been processed, regardless of its content.
         if (it != func_calls.end() && !it->second.empty()) {
            std::string msg = "read-only action cannot call write host function";
            if (warn_action_read_only) {
               CDT_WARN("codegen_warning", ra->getLocation(), msg);
            } else {
               CDT_ERROR("codegen_error", ra->getLocation(), msg);
            }
         }
      }
   }
};

class sysio_codegen_consumer : public ASTConsumer {
private:
   std::string main_file;
   CompilerInstance* ci;
   int smart_contract_trace_level;

public:
   std::unique_ptr<sysio_codegen_visitor> visitor;

   explicit sysio_codegen_consumer(CompilerInstance* CI, StringRef file, int smart_contract_trace_level_)
      : visitor(std::make_unique<sysio_codegen_visitor>(CI)), main_file(file), ci(CI), smart_contract_trace_level(smart_contract_trace_level_) {}

   void HandleTranslationUnit(ASTContext& Context) override {
      auto& src_mgr = Context.getSourceManager();
      auto& f_mgr = src_mgr.getFileManager();
      auto main_fe = f_mgr.getOptionalFileRef(main_file);
      if (main_fe) {
         auto fid = src_mgr.getOrCreateFileID(*main_fe, SrcMgr::CharacteristicKind::C_User);
         visitor->set_main_fid(fid);

         visitor->set_main_name(main_fe->getName());
         visitor->TraverseDecl(Context.getTranslationUnitDecl());
         visitor->process_read_only_actions();

         // Only generate output files when an output path was provided.
         // When loaded during normal compilation (no -plugin-arg), we only
         // do validation (process_read_only_actions above), not code generation.
         if (!output.empty()) {
            std::string output_debug_file_name; // need to be outside because visitor refer to it
            if (smart_contract_trace_level) {
               std::unique_ptr<sysio_tracegen_visitor> trace_visitor(std::make_unique<sysio_tracegen_visitor>(ci, main_file, smart_contract_trace_level));
               trace_visitor->TraverseDecl(Context.getTranslationUnitDecl());
               output_debug_file_name = std::string(main_fe->getName()) + ".debug";
               if (size_t inserted_count = trace_visitor->inject_debugging_code(main_fe->getName().str(), output_debug_file_name)) {
                  visitor->set_main_name(output_debug_file_name);
                  std::cout << "debug file " << output_debug_file_name << " generated with " << inserted_count << " checkpoint(s)\n";
               }
            }

            visitor->save(output + ".actions.cpp");
         }
      }
   }
};

class sysio_codegen_frontend_action: public PluginASTAction {
private:
   std::string contract_name;
   int smart_contract_trace_level = (int)sysio_tracegen_visitor::smart_contract_trace_level_t::none;
   bool warn_action_read_only = false;

public:
   std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& CI, StringRef file) override {
      if (smart_contract_trace_level > (int)sysio_tracegen_visitor::smart_contract_trace_level_t::none) {
         std::cout << "sysio_codegen_frontend_action: file=" << std::string(file) << " smart_contract_trace_level=" << smart_contract_trace_level << std::endl;
      }
      auto consumer = std::make_unique<sysio_codegen_consumer>(&CI, file, smart_contract_trace_level);
      consumer->visitor->set_contract_name(contract_name);
      consumer->visitor->set_warn_action_read_only(warn_action_read_only);
      return consumer;
   }

   bool ParseArgs(const CompilerInstance& CI, const std::vector<std::string>& args) override {
      if (args.empty())
         return true;

      for (const auto& arg : sysio_plugin::tokenize(args[0])) {
         if (sysio::cdt::starts_with(arg, "output=")) {
            output = arg.substr(arg.find("=")+1);
         } else if (sysio::cdt::starts_with(arg, "contract=")) {
            contract_name = arg.substr(arg.find("=")+1);
         } else if (arg == "warn-action-read-only") {
            warn_action_read_only = true;
         } else if (sysio::cdt::starts_with(arg, "smart-contract-trace-level=")) {
            std::string value = arg.substr(arg.find("=")+1);
            if (::sscanf(value.c_str(), "%d", &smart_contract_trace_level) != 1) {
               std::cerr << "failed to parse smart-contract-trace-level" << std::endl;
               return false;
            }
         } else {
            return false;
         }
      }
      return true;
   }

   ActionType getActionType() override {
      return AddBeforeMainAction;
   }
};

static FrontendPluginRegistry::Add<sysio_codegen_frontend_action> sysio_codegen("sysio_codegen", "");
static FrontendPluginRegistry::Add<sysio_abigen_frontend_action> sysio_abigen("sysio_abigen", "");
