#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>
#include <clang/Sema/SemaDiagnostic.h>
#include <llvm/IR/Attributes.h>

using namespace clang;

// SYSIO_ATTR macro: Registers a custom sysio attribute that gets converted to
// a standard AnnotateAttr. This allows using [[sysio::action]], [[sysio::contract]], etc.
// in contract code without modifying the Clang AST.
//
// When multiple sysio attributes are applied to the same declaration, each gets
// its own AnnotateAttr. The clang_wrapper reads all AnnotateAttr instances.
#define SYSIO_ATTR(NAME, GNU, CXX11, NUM, ONUM, SUBJECTS) \
   namespace sysio_plugin { \
      struct NAME ## AttrInfo : public ParsedAttrInfo { \
        unsigned _NumArgs = NUM; \
        NAME ## AttrInfo() { \
          OptArgs = ONUM; \
          static constexpr Spelling S[] = {{ParsedAttr::AS_GNU, #GNU}, \
                                           {ParsedAttr::AS_CXX11, #CXX11}}; \
          Spellings = S; \
        } \
        bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, \
                                  const Decl *D) const override { \
          if (SUBJECTS) { \
            S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str) \
              << Attr << "declarations"; \
            return false; \
          } \
          return true; \
        } \
        AttrHandling handleDeclAttribute(Sema &S, Decl *D, \
                                         const ParsedAttr &Attr) const override { \
          StringRef Str = ""; \
          if (Attr.getNumArgs() > 0) { \
            Expr *ArgExpr = Attr.getArgAsExpr(0); \
            clang::StringLiteral *Literal = \
                dyn_cast<clang::StringLiteral>(ArgExpr->IgnoreParenCasts()); \
            if (Literal) { \
              Str = Literal->getString(); \
            } else { \
              S.Diag(ArgExpr->getExprLoc(), diag::err_attribute_argument_type) \
                  << Attr.getAttrName() << AANT_ArgumentString; \
              return AttributeNotApplied; \
            } \
          } \
          else if (spellingIndexToSemanticSpelling(Attr)) { \
             auto& SM = S.getSourceManager(); \
             auto AttrRange = SM.getExpansionRange(Attr.getRange()); \
             auto LangOpts = S.Context.getLangOpts(); \
             auto offset = Lexer::getSourceText(SM.getExpansionRange(AttrRange.getEnd()), SM, LangOpts).size(); \
             auto Begin = AttrRange.getEnd().getLocWithOffset(offset); \
             if (Lexer::getSourceText(CharSourceRange(SourceRange(Begin), true), SM, LangOpts) == "(") { \
                /* Clang does not parse arguments for a C++11-spelled plugin attribute, so this */ \
                /* is read from SOURCE TEXT -- and the compiler splices adjacent string */ \
                /* literals while a source read does not. Wrapping a long name across two lines */ \
                /* is ordinary C++ and exactly what a `_i` name is long enough to want: */ \
                /* */ \
                /*   [[sysio::table("user_preferences_" "history")]] */ \
                /*   multi_index<"user_preferences_history"_i, row> */ \
                /* */ \
                /* published `user_preferences_` at table_id 32944, the id of the FULL name. */ \
                /* The name in the ABI addressed nothing and the id was unreachable by name. */ \
                /* Refused rather than joined: one literal is what the reader and the compiler */ \
                /* are guaranteed to agree on. */ \
                auto ArgLoc = Begin.getLocWithOffset(1); \
                Token Tok, Next; \
                if (!Lexer::getRawToken(ArgLoc, Tok, SM, LangOpts, true) && \
                    Tok.getKind() == tok::string_literal && \
                    !Lexer::getRawToken(Tok.getEndLoc(), Next, SM, LangOpts, true) && \
                    Next.getKind() == tok::r_paren) { \
                   Str = StringRef(SM.getCharacterData(Tok.getLocation()), Tok.getLength()); \
                } else { \
                   S.Diag(ArgLoc, S.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, \
                      "a sysio attribute argument must be one string literal; adjacent literals " \
                      "are joined by the compiler but not by the ABI generator, which would name " \
                      "the table after the first alone")); \
                   return AttributeNotApplied; \
                } \
             } else if (_NumArgs) { \
               S.Diag(Attr.getLoc(), diag::err_attribute_argument_type) \
                   << Attr.getAttrName() << "attribute takes one argument"; \
               return AttributeNotApplied; \
             } \
          } \
          auto arg = Str.str(); \
          if (arg.size() > 1 && arg[0] == '\"') arg = arg.substr(1, arg.size()-2); \
          std::string annotation; \
          if (arg.empty()) { \
            annotation = #GNU; \
          } else { \
            annotation = std::string(#GNU) + "(" + arg + ")"; \
          } \
          D->addAttr(AnnotateAttr::Create(S.Context, annotation, nullptr, 0, Attr)); \
          return AttributeApplied; \
        } \
      }; \
   } \
   static ParsedAttrInfoRegistry::Add<sysio_plugin::NAME ## AttrInfo> NAME(#GNU, "");


// Sysio attributes
SYSIO_ATTR(SysioIgnore,    sysio_ignore,      sysio::ignore,      0, 1, (!isa<CXXRecordDecl>(D) && !isa<RecordDecl>(D)))
SYSIO_ATTR(SysioNotify,    sysio_on_notify,   sysio::on_notify,   1, 0, (!isa<CXXMethodDecl>(D)))
SYSIO_ATTR(SysioRicardian, sysio_ricardian,   sysio::ricardian,   1, 0, (!isa<CXXRecordDecl>(D) && !isa<CXXMethodDecl>(D)))
SYSIO_ATTR(SysioContract,  sysio_contract,    sysio::contract,    0, 1, (!isa<CXXRecordDecl>(D) && !isa<CXXMethodDecl>(D)))
SYSIO_ATTR(SysioAction,    sysio_action,      sysio::action,      0, 1, (!isa<CXXRecordDecl>(D) && !isa<CXXMethodDecl>(D)))
SYSIO_ATTR(SysioTable,     sysio_table,       sysio::table,       0, 1, (!isa<CXXRecordDecl>(D)))
SYSIO_ATTR(SysioKvKey,     sysio_kv_key,      sysio::kv_key,      0, 1, (!isa<CXXRecordDecl>(D)))
SYSIO_ATTR(SysioWasmAction, sysio_wasm_action, sysio::wasm_action, 0, 1, (!isa<FunctionDecl>(D)))
SYSIO_ATTR(SysioWasmNotify, sysio_wasm_notify, sysio::wasm_notify, 0, 1, (!isa<FunctionDecl>(D)))
SYSIO_ATTR(SysioWasmAbi,   sysio_wasm_abi,    sysio::wasm_abi,    0, 1, (!isa<FunctionDecl>(D)))
SYSIO_ATTR(SysioReadOnly,  sysio_read_only,   sysio::read_only,   0, 0, (!isa<FunctionDecl>(D)))
SYSIO_ATTR(SysioType,      sysio_type,        sysio::type,        1, 0, (!isa<FieldDecl>(D)))

// sysio_wasm_entry: converts to WebAssemblyExportNameAttr (standard Clang)
namespace sysio_plugin {
   struct SysioWasmEntryAttrInfo : public ParsedAttrInfo {
      SysioWasmEntryAttrInfo() {
         NumArgs = 0;
         static constexpr Spelling S[] = {{ParsedAttr::AS_GNU, "sysio_wasm_entry"},
                                          {ParsedAttr::AS_CXX11, "sysio::wasm_entry"}};
         Spellings = S;
      }

      bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                                const Decl *D) const override {
         if (!isa<FunctionDecl>(D)) {
            S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
                    << Attr << "functions";
            return false;
         }
         return true;
      }

      AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                       const ParsedAttr &Attr) const override {
         StringRef Str = "";
         if (auto* named = llvm::dyn_cast<NamedDecl>(D)) {
            Str = named->getName();
         }
         D->addAttr(WebAssemblyExportNameAttr::Create(S.Context, Str, Attr));
         return AttributeApplied;
      }
   };

   struct SysioWasmImportAttrInfo : public ParsedAttrInfo {
      SysioWasmImportAttrInfo() {
         NumArgs = 0;
         static constexpr Spelling S[] = {{ParsedAttr::AS_GNU, "sysio_wasm_import"},
                                          {ParsedAttr::AS_CXX11, "sysio::wasm_import"}};
         Spellings = S;
      }

      bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                                const Decl *D) const override {
         if (!isa<FunctionDecl>(D)) {
            S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
                    << Attr << "functions";
            return false;
         }
         return true;
      }

      AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                       const ParsedAttr &Attr) const override {
         StringRef Str = "";
         if (auto* named = llvm::dyn_cast<NamedDecl>(D)) {
            Str = named->getName();
         }
         D->addAttr(WebAssemblyImportNameAttr::Create(S.Context, Str, Attr));
         return AttributeApplied;
      }
   };
}

static ParsedAttrInfoRegistry::Add<sysio_plugin::SysioWasmEntryAttrInfo> SysioWasmEntry("sysio_wasm_entry", "");
static ParsedAttrInfoRegistry::Add<sysio_plugin::SysioWasmImportAttrInfo> SysioWasmImport("sysio_wasm_import", "");
