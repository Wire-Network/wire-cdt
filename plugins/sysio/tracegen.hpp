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

using namespace clang;
using namespace sysio::cdt;

class sysio_tracegen_visitor : public RecursiveASTVisitor<sysio_tracegen_visitor>, public generation_utils {
private:
   CompilerInstance* ci;
   bool      warn_action_read_only = false;
   std::stringstream ss;

public: // smart contract tracking
   enum class smart_contract_trace_level_t : int {
      none = 0,
      statement,
      full
   };
   std::string main_file;
   SourceManager *sm = nullptr;
   int smart_contract_trace_level = (int)smart_contract_trace_level_t::none;

   bool in_const_func = false;
   clang::Stmt *func_body = nullptr;

   struct check_point_t {
      enum type_t { invalid = 0,
                     func_enter, // insert print right after function enter
                     single_back, // insert print at the back of location
                     single_front, // insert print at the front of location
                     };
      int type = invalid;
      std::string func; // function name for function enter  & exit track, or lable name
      std::string variable; // track variable instead of line & column
      uint32_t line1 = 0, col1 = 0; // ending position, used if the check point is to capture expression change.
   };

   std::map<uint64_t, std::vector<check_point_t> >  check_points;
   bool insert_check_point(uint32_t line0, uint32_t col0, uint32_t line1, uint32_t col1, int start_type, std::string func_ = "", std::string variable_ = "") {
      if (in_const_func) return false;
      uint64_t line_col0 = ((((uint64_t)line0) << 32) | col0);
      uint64_t line_col1 = ((((uint64_t)line1) << 32) | col1);
      if (line_col0 > line_col1) return false;
      if (!line0 || !col0 || !line1 || !col1) return false;

      if (variable_.length()) {
         check_points[line_col0].push_back(check_point_t{.type = start_type, .func = "", .variable = variable_, .line1 = line1, .col1 = col1});
         return true;
      }
      if (check_points[line_col0].size() == 0) {
         check_points[line_col0].push_back(check_point_t{.type = start_type, .func = func_, .variable = "", .line1 = line1, .col1 = col1});
         return true;
      }
      return false;
   }


public:

   explicit sysio_tracegen_visitor(CompilerInstance* CI, std::string main_file_, int smart_contract_trace_level_): ci(CI) {

      get_error_emitter().set_compiler_instance(CI);

      sm = &(CI->getSourceManager());
      smart_contract_trace_level = smart_contract_trace_level_;
      main_file = main_file_;
   }

   std::string printLocation(clang::SourceLocation loc) {
      clang::PresumedLoc plocs = sm->getPresumedLoc(loc);
      std::stringstream ss;
      ss << plocs.getLine() << ":" << plocs.getColumn();
      return ss.str();
   }

   bool insert_check_point_by_location(const char *mark, clang::SourceLocation locstart, clang::SourceLocation locend, int type, std::string func_name = "", std::string var_name = "") {
      bool invalid = false;
      if (locstart.isFileID() && locend.isFileID()) {
         clang::PresumedLoc plocs = sm->getPresumedLoc(locstart);
         clang::PresumedLoc ploce = sm->getPresumedLoc(locend);
         if (plocs.isInvalid() || ploce.isInvalid()) return false;
         if (plocs.getLine() > ploce.getLine()) return false;

         std::string file_name = plocs.getFilename();
         if (file_name == main_file) {
            if (insert_check_point(plocs.getLine(), plocs.getColumn(), ploce.getLine(), ploce.getColumn(), type, func_name, var_name)) {
               std::cout << "inserted checkpoint at " << locstart.printToString(*sm) << "(" << (strlen(mark) ? mark : var_name.c_str()) << ")" << std::endl;
               return true;
            }
         }
      }
      return false;
   }

   // find out statments in lambda expression body
   void visit_expr(clang::Expr *expr_, const ASTContext& ctx) {
      if (!expr_) return;
      //std::cout << " " << expr_->getStmtClassName() << " ";
      if (clang::ExprWithCleanups *expr = llvm::dyn_cast<clang::ExprWithCleanups>(expr_)) {
         visit_expr(expr->getSubExpr(), ctx);
      } else if (clang::CallExpr *expr = llvm::dyn_cast<clang::CallExpr>(expr_)) {
         visit_expr(expr->getCallee(), ctx);
         for (auto itr = expr->arg_begin(); itr != expr->arg_end(); ++itr) {
            visit_expr(*itr, ctx);
         }
      } else if (clang::LambdaExpr *expr = llvm::dyn_cast<clang::LambdaExpr>(expr_)) {
         visit_stmt(expr->getBody(), ctx);
      } else if (clang::MaterializeTemporaryExpr *expr = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(expr_)) {
         clang::Expr *temp_expr = expr->getSubExpr();
         visit_expr(temp_expr, ctx);
      } else if (clang::BinaryOperator *expr = llvm::dyn_cast<clang::BinaryOperator>(expr_)) {
         // TODO: binary operation value tracking
         clang::Expr* left = expr->getLHS();
         clang::Expr* right = expr->getRHS();
	      visit_expr(left, ctx);
	      visit_expr(right, ctx);
      } else if (clang::CastExpr *expr = llvm::dyn_cast<clang::CastExpr>(expr_)) {
	      visit_expr(expr->getSubExpr(), ctx);
      }	else {
	      // TODO: support more expr in the future
         //std::cout << "ignore " << expr_->getStmtClassName() << " expr\n";
      }
   }

   void visit_stmt(clang::Stmt *stmt_, const ASTContext& ctx) {
      if (!stmt_) return;
      if (stmt_->getStmtClass() == clang::Stmt::CompoundStmtClass) {
         clang::CompoundStmt *cpst = (clang::CompoundStmt *)stmt_;
         for (auto body_itr = cpst->body_begin(); body_itr != cpst->body_end(); ++body_itr) {
            clang::Stmt *s = *body_itr;
            if (!s) continue;
            const char *label_name = "";
            // remove label & case recursively
            while (true) {
               if (auto *ls = llvm::dyn_cast<clang::LabelStmt>(s)) {
                  label_name = ls->getName();
                  s = ls->getSubStmt();
               }
               else if (auto *sc = llvm::dyn_cast<clang::SwitchCase>(s))
                  s = sc->getSubStmt();
               else break;
            }
            if (s->getStmtClass() != clang::Stmt::CompoundStmtClass) {
               insert_check_point_by_location((s->getStmtClassName() + std::string(" ")).c_str(),
                  s->getBeginLoc(), s->getEndLoc(), check_point_t::single_front, label_name);
            }
            visit_stmt(s, ctx);
         }
      } else if (stmt_->getStmtClass() == clang::Stmt::IfStmtClass) {
         clang::IfStmt *stmt = (clang::IfStmt *)stmt_;
         visit_stmt(stmt->getThen(), ctx);
         visit_stmt(stmt->getElse(), ctx);
      } else if (stmt_->getStmtClass() == clang::Stmt::WhileStmtClass) {
         clang::WhileStmt *stmt = (clang::WhileStmt *)stmt_;
         visit_stmt(stmt->getBody(), ctx);
      } else if (stmt_->getStmtClass() == clang::Stmt::DoStmtClass) {
         clang::DoStmt *stmt = (clang::DoStmt *)stmt_;
         visit_stmt(stmt->getBody(), ctx);
      } else if (stmt_->getStmtClass() == clang::Stmt::ForStmtClass) {
         clang::ForStmt *stmt = (clang::ForStmt *)stmt_;
         visit_stmt(stmt->getBody(), ctx);
      } else if (stmt_->getStmtClass() == clang::Stmt::CXXForRangeStmtClass) {
         clang::CXXForRangeStmt *stmt = (clang::CXXForRangeStmt *)stmt_;
         visit_stmt(stmt->getBody(), ctx);
      } else if (stmt_->getStmtClass() == clang::Stmt::SwitchStmtClass) {
         clang::SwitchStmt *stmt = (clang::SwitchStmt *)stmt_;
         visit_stmt(stmt->getBody(), ctx);
      } else if (stmt_->getStmtClass() == clang::Stmt::CompoundStmtClass) {
         clang::CompoundStmt *stmt = (clang::CompoundStmt *)stmt_;
         visit_stmt(stmt, ctx);
      } else if (stmt_->getStmtClass() == clang::Stmt::ReturnStmtClass) {
     	 clang::ReturnStmt *stmt = (clang::ReturnStmt *)stmt_;
         if (stmt->getRetValue()) {
	    visit_expr(stmt->getRetValue(), ctx);
         }
      } else if (stmt_->getStmtClass() == clang::Stmt::DeclStmtClass) {
         clang::DeclStmt *stmt = (clang::DeclStmt *)stmt_;
         for (clang::DeclGroupRef::iterator itr = stmt->decl_begin(); itr != stmt->decl_end(); ++itr) {
            clang::VarDecl *n_decl = llvm::dyn_cast<clang::VarDecl>(*itr);
            if (n_decl) {
               std::string pname = n_decl->getName().str();
               std::cout << pname << ",";
               if (n_decl->hasInit()) {
                  visit_expr(n_decl->getInit(), ctx);
               }
            }
         }
      } else if (clang::Expr *expr = llvm::dyn_cast<clang::Expr>(stmt_)) {
         visit_expr(expr, ctx);
      }
   }

   void visitDecl_for_tracking(clang::Decl* decl, clang::FunctionDecl* fd) {
      if (smart_contract_trace_level >= (int)smart_contract_trace_level_t::statement
         && !(fd->isConstexpr())
         && fd->hasBody()
         && fd->getBody()->getStmtClass() == clang::Stmt::CompoundStmtClass) {
         std::string func_name = fd->getNameInfo().getAsString();
         clang::Stmt *stmt = fd->getBody();
         clang::CompoundStmt *cpst = (clang::CompoundStmt *)stmt;

         if (stmt->getBeginLoc() != stmt->getEndLoc() // avoid some special functions like "=default;", "=delete;"
            && insert_check_point_by_location(
               ("CompoundStmt(FuncEnter " + func_name + ") ").c_str(),
               stmt->getBeginLoc(),
               stmt->getEndLoc(),
               check_point_t::func_enter, func_name)) {

            // ParmVarDecl : VarDecl
            // VarDecl : DeclaratorDecl, Redeclarable<VarDecl>
            // DeclaratorDecl (has inner source location) : ValueDecl
            // ValueDecl : NamedDecl
            // NameDecl (has getName()) : Decl

            clang::PresumedLoc body_loc = sm->getPresumedLoc(stmt->getBeginLoc());
            clang::FunctionDecl* correct_fd = nullptr;

            uint64_t param_body_diff = UINT64_MAX;

            // walk through all function declarations and find the one with the body
            for (auto fd_itr = fd->redecls_begin(); fd_itr != fd->redecls_end(); ++fd_itr) {
               if (fd_itr->param_size() == 0) continue;

               clang::ParmVarDecl *param = fd_itr->getParamDecl(0);
               clang::SourceLocation srcloc = param->getInnerLocStart();
               clang::PresumedLoc ploc = sm->getPresumedLoc(srcloc);

               if (body_loc.getFilename() == ploc.getFilename()) {
                  uint64_t body_loc_raw = (((uint64_t)(body_loc.getLine())) << 32) + body_loc.getColumn();
                  uint64_t param_loc_raw = (((uint64_t)(ploc.getLine())) << 32) + ploc.getColumn();
                  if (body_loc_raw > param_loc_raw && body_loc_raw - param_loc_raw < param_body_diff) {
                     param_body_diff = body_loc_raw - param_loc_raw;
                     correct_fd = *fd_itr;
                  }
               }
            }

            if (correct_fd) {
               int param_total = 0;
               clang::ArrayRef<clang::ParmVarDecl *> params = correct_fd->parameters();
               // walk through parameters
               for (clang::ParmVarDecl *param : params) {
                  clang::QualType type = param->getType();
                  std::string tname = type.getAsString();
                  std::string pname = param->getName().str();
                  std::cout << pname << ":" << tname << ",";
                  if (tname.find("...") == std::string::npos) { // skip param with packed type like "func(T... accounts)"
                     insert_check_point_by_location("", stmt->getBeginLoc(), stmt->getEndLoc(), check_point_t::single_back, "", pname);
                     ++param_total;
                  }
               }
               if (param_total) {
                  std::cout << std::endl;
               }
            }

            visit_stmt(cpst, decl->getASTContext());
         }
      }
   }

   virtual bool VisitDecl(clang::Decl* decl) {
      auto _decl = clang_wrapper::wrap_decl(decl);
      if (auto* fd = dyn_cast<clang::FunctionDecl>(decl)) {
         visitDecl_for_tracking(decl, fd);
      }
      return true;
   }

   // return number of checkpoints inserted
   size_t inject_debugging_code(std::string main_fe_name, std::string output_debug_filename) {

      size_t insert_count = 0;
      std::stringstream out_data;
      llvm::SmallString<64> abs_file_path(main_fe_name);
      llvm::sys::fs::make_absolute(abs_file_path);
      std::string short_fn = abs_file_path.c_str();
      if (short_fn.find_last_of("/") != std::string::npos) {
         short_fn = short_fn.substr(short_fn.find_last_of("/") + 1);
      }
      FILE *fin = fopen(abs_file_path.c_str(), "rb");
      if (!fin) {
         std::cerr << "failed to open " << abs_file_path.c_str() << ". smart contract tracing will be disabled.\n";
         out_data << "#include \"" << abs_file_path.c_str() << "\"\n";
         return 0;
      } else {
         out_data << "#include <stdio.h>\n"; // need printf
         if (smart_contract_trace_level >= (int)sysio_tracegen_visitor::smart_contract_trace_level_t::full) {
            out_data << R"(
#include <optional>
#include <vector>
#include <type_traits>
#include <sysio/print.hpp>
#include <sysio/asset.hpp>
#include <sysio/name.hpp>
#include <sysio/crypto.hpp>
#include <sysio/producer_schedule.hpp>
#include <sysio/for_each_field.hpp>

inline char cdt_debug_is_optional(...);
template <typename T>
inline double cdt_debug_is_optional(const std::optional<T> *t);

inline char cdt_debug_is_vector(...);
template <typename T>
inline double cdt_debug_is_vector(const std::vector<T> *t);

template <typename T>
struct _cdt_debug_traits {
   struct _cdt_debug_field_helper {
      template <typename A, typename B>
      void operator()(const A&, const B&);
   };

   static char test0(const void *, ...);
   template <typename U> static double test0(const U *u, decltype(sysio::print(*u)) * _=nullptr);
   static char test1(const void *,...);
   template <typename U> static double test1(const U *u, decltype(sysio::print(u->to_string())) * _=nullptr);
   static char test2(const void *, ...);
   template <typename U> static double test2(const U *u, decltype(sysio::print(u->to_hex())) * _=nullptr);
   static char test3(const void *, ...);
   template <typename U> static double test3(const U *u,
      decltype(sysio_for_each_field((std::decay_t<U>*)u, std::declval<_cdt_debug_field_helper>())) * _=nullptr);
   enum { value = (uint32_t)(sizeof(test0((T*)nullptr)) == sizeof(double)) +
                  ((uint32_t)(sizeof(test1((T*)nullptr)) == sizeof(double)) * 2) +
                  ((uint32_t)(sizeof(test2((T*)nullptr)) == sizeof(double)) * 4) +
                  ((uint32_t)(sizeof(test3((T*)nullptr)) == sizeof(double)) * 8) };
};

inline void _cdt_debug_print_var1(const char *varname,  const sysio::public_key *t) {
   if (!t) sysio::print(varname, (varname[0] ? "=null" : "null"));
   sysio::print(varname, (varname[0] ? "=" : ""), sysio::public_key_to_string(*t));
}
inline void _cdt_debug_print_var1(const char *varname,  const sysio::private_key *t) {
   if (!t) sysio::print(varname, (varname[0] ? "=null" : "null"));
   sysio::print(varname, (varname[0] ? "=" : ""), sysio::private_key_to_string(*t));
}
inline void _cdt_debug_print_var1(const char *varname,  const sysio::signature *t) {
   if (!t) sysio::print(varname, (varname[0] ? "=null" : "null"));
   sysio::print(varname, (varname[0] ? "=" : ""), sysio::signature_to_string(*t));
}
inline void _cdt_debug_print_var1(const char *varname,  const sysio::key_weight *t) {
   if (!t) sysio::print(varname, (varname[0] ? "=null" : "null"));
   sysio::print(varname, (varname[0] ? "={" : "{"));
   sysio::print(".key=", sysio::public_key_to_string(t->key));
   sysio::print(",.weight=", (unsigned int)t->weight);
   sysio::print("}");
}

template <typename T>
inline void _cdt_debug_print_var1(const char *varname, const T *t) {
   if constexpr ((_cdt_debug_traits<T>::value & 1) != 0) {
      sysio::print(varname, (varname[0] ? "=" : ""));
      sysio::print(*t);
   } else if constexpr ((_cdt_debug_traits<T>::value & 2) != 0) {
      sysio::print(varname, (varname[0] ? "=" : ""));
      sysio::print(t->to_string());
   } else if constexpr ((_cdt_debug_traits<T>::value & 4) != 0) {
      sysio::print(varname, (varname[0] ? "=" : ""));
      sysio::print(t->to_hex());
   } else if constexpr ((_cdt_debug_traits<T>::value & 8) != 0) {
      sysio::print(varname, (varname[0] ? "={" : "{"));
      bool comma = false;
      sysio_for_each_field((std::decay_t<T>*)t, [&](const char* member_name, auto member) {
         if constexpr (std::is_member_object_pointer_v<decltype(member(t))>) {
            sysio::print(comma ? ",." : ".");
            _cdt_debug_print_var1(member_name, &(t->*member(t)));
            comma = true;
         }
      });
      sysio::print("}");
   } else if constexpr (std::is_enum<T>::value) {
      int64_t iv = (int64_t)t;
      sysio::print(varname, (varname[0] ? "=" : ""), iv);
   } else if constexpr (sizeof(cdt_debug_is_optional(t)) == sizeof(double)) {
      if (t->has_value()) {
         const auto &val = **t;
         _cdt_debug_print_var1(varname, &val);
      }
      else sysio::print(varname, (varname[0] ? "=null" : "null"));
   } else if constexpr (sizeof(cdt_debug_is_vector(t)) == sizeof(double)) {
      if (t->size() == 0) {
         sysio::print(varname, (varname[0] ? "=[]" : "[]"));
      } else {
         sysio::print(varname, (varname[0] ? "=[" : "["));
         for (size_t i = 0; i < t->size(); ++i) {
            const auto &val = (*t)[i];
            _cdt_debug_print_var1("", &val);
            if (i < t->size() - 1) {
               if (i == 9) { // max 10 items
                  sysio::print(",...");
                  break;
               }
               sysio::print(",");
            }
         }
         sysio::print("]");
      }
   } else {
      printf((varname[0] ? "%s=@0x%p" : "%s@0x%p"), varname, (const void *)t);
   }
}

template <typename T> inline void _cdt_debug_print_var(const char *varname, const T &t) {
   _cdt_debug_print_var1(varname, &t);
}

)";
         }

         uint32_t line = 1, col = 1;
         while (true) {
            char c = fgetc(fin);
            if (feof(fin)) break;
            uint64_t line_col = ((uint64_t)line << 32) | col;
            if (c == '\r') continue;
            if (c == '\n') {
               out_data << c;
               ++line;
               col = 1;
               continue;
            }

            auto insert_code = [&](const sysio_tracegen_visitor::check_point_t &cp) {
               if (cp.variable.length()) {
                  if (smart_contract_trace_level == (int)sysio_tracegen_visitor::smart_contract_trace_level_t::full) {
                     out_data << "_cdt_debug_print_var(\"," + cp.variable + "\"," + cp.variable + ");";
                  }
               } else {
                  out_data << "printf(\"@" << short_fn << ":" << line << ":" << col;
                  if (cp.func != "") {
                     out_data << "(@" << cp.func << ")";
                  }
                  out_data << "@\");";
               }
               ++insert_count;
            };

            if (check_points.find(line_col) != check_points.end()) {
               const std::vector<sysio_tracegen_visitor::check_point_t> &list = check_points[line_col];
               for (size_t i = 0; i < list.size(); ++i) {
                  if (list[i].type == sysio_tracegen_visitor::check_point_t::single_front) {
                     insert_code(list[i]);
                  }
               }
               out_data << c;
               for (int i = 0; i < list.size(); ++i) {
                  if (list[i].type == sysio_tracegen_visitor::check_point_t::func_enter) {
                     std::stringstream tmp_struct_ss;
                     tmp_struct_ss << "_cdt_debug_func_" << line;
                     std::string tmp_struct_name = tmp_struct_ss.str();

                     // insert a tmp class to capture function exit
                     out_data << "struct " << tmp_struct_name
                              << "{~" << tmp_struct_name
                              << "(){printf(\"@" << short_fn << "(~" << list[i].func << ")@\");}"
                              << "} "
                              << tmp_struct_name << "_1;";

                     // insert function enter
                     out_data << "printf(\"@" << short_fn << ":" << line << ":" << col << "(@" << list[i].func << "\");";
                     ++insert_count;

                     // insert function pararmeter
                     while ((++i) < list.size() && list[i].variable.length()) {
                        insert_code(list[i]);
                     }

                     // insert function enter suffix
                     out_data << "printf(\")@\");";
                     --i;
                  } else if (list[i].type == sysio_tracegen_visitor::check_point_t::single_back) {
                     insert_code(list[i]);
                  }
               }
            } else {
               out_data << c;
            }
            ++col;
         }
         fclose(fin);
         out_data << "\n\n";
      }

      std::ofstream debug_file_stream(output_debug_filename.c_str());
      debug_file_stream << out_data.str();
      debug_file_stream.close();
      return insert_count;
   }

};
