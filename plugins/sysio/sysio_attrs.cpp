#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>
#include <clang/Sema/SemaDiagnostic.h>
#include <llvm/IR/Attributes.h>
#include <optional>

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
          bool arg_supplied = false; \
          if (Attr.getNumArgs() > 0) { \
            arg_supplied = true; \
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
             /* Clang does not parse arguments for a C++11-spelled plugin attribute -- */ \
             /* getNumArgs() is 0 even for `[[sysio::table("x")]]`, and stays 0 with a */ \
             /* required argument declared -- so the argument is read from source. TOKENS, */ \
             /* not characters: an argument clause may be separated from the attribute name */ \
             /* by whitespace or a comment, and probing the next character for `(` read */ \
             /* `[[sysio::table /* c *\/ ("name")]]` as bare and dropped the name. */ \
             auto& SM = S.getSourceManager(); \
             auto LangOpts = S.Context.getLangOpts(); \
             auto AttrRange = SM.getExpansionRange(Attr.getRange()); \
             auto opening = [&](SourceLocation NameLoc) -> std::optional<Token> { \
                auto t = Lexer::findNextToken(NameLoc, SM, LangOpts); \
                if (t && t->getKind() == tok::l_paren) return t; \
                return std::nullopt; \
             }; \
             auto open = opening(AttrRange.getEnd()); \
             /* An attribute written inside a macro has an expansion range ending at the */ \
             /* invocation, so nothing follows it there; the argument is still in the macro */ \
             /* body, at the spelling location. */ \
             if (!open && Attr.getRange().getBegin().isMacroID()) \
                open = opening(SM.getSpellingLoc(Attr.getRange().getEnd())); \
             if (open) { \
                arg_supplied = true; \
                auto tok = Lexer::findNextToken(open->getLocation(), SM, LangOpts); \
                auto after = tok ? Lexer::findNextToken(tok->getLocation(), SM, LangOpts) \
                                 : std::nullopt; \
                /* Read as text, so it has to be text that survives being read: the compiler */ \
                /* cooks escapes and splices adjacent literals and this does not, and each */ \
                /* disagreement named a table at another string's table_id. */ \
                const bool one_plain_literal = \
                   tok && tok->getKind() == tok::string_literal && \
                   after && after->getKind() == tok::r_paren && \
                   !StringRef(SM.getCharacterData(tok->getLocation()), tok->getLength()).contains('\\'); \
                if (!one_plain_literal) { \
                   if (Attr.getRange().getBegin().isMacroID()) { \
                     S.Diag(open->getLocation(), S.getDiagnostics().getCustomDiagID( \
                        DiagnosticsEngine::Error, \
                        "sysio attribute argument must be one plain string literal written in " \
                        "place; a macro parameter cannot be used, since the argument is read as " \
                        "source text")); \
                   } else { \
                     S.Diag(open->getLocation(), S.getDiagnostics().getCustomDiagID( \
                        DiagnosticsEngine::Error, \
                        "sysio attribute argument must be one plain string literal -- no escape " \
                        "sequences, no adjacent literals; it is read exactly as written")); \
                   } \
                   return AttributeNotApplied; \
                } \
                Str = StringRef(SM.getCharacterData(tok->getLocation()), tok->getLength()); \
             } else if (_NumArgs) { \
               S.Diag(Attr.getLoc(), diag::err_attribute_argument_type) \
                   << Attr.getAttrName() << "attribute takes one argument"; \
               return AttributeNotApplied; \
             } \
          } \
          auto arg = Str.str(); \
          if (arg.size() > 1 && arg[0] == '\"') arg = arg.substr(1, arg.size()-2); \
          /* An empty argument encodes identically to no argument at all, so every later check */ \
          /* reads it as a bare attribute and the validation meant for a written name is never */ \
          /* reached. Checked here, after BOTH spellings, because only the parse knows whether */ \
          /* an argument was written -- confined to the C++11 branch it missed */ \
          /* `__attribute__((sysio_table("")))` entirely. */ \
          if (arg_supplied && arg.empty()) { \
            S.Diag(Attr.getLoc(), S.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, \
               "sysio attribute argument may not be empty; omit the argument instead")); \
            return AttributeNotApplied; \
          } \
          /* The argument survives as text: it is encoded into an AnnotateAttr as `NAME(arg)` */ \
          /* and split back out on [\s,]+. So whitespace, a comma, a paren or a quote does not */ \
          /* round-trip -- `[[sysio::table("has space")]]` published `has`, truncated by the */ \
          /* encoding rather than by anything the author could see. Refused rather than */ \
          /* mangled; these names travel out through the ABI to wire-sysio, SHiP and Hyperion. */ \
          if (arg.find_first_of(" \t\r\n,()\"\\") != std::string::npos) { \
            S.Diag(Attr.getLoc(), S.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, \
               "sysio attribute argument may not contain whitespace, a comma, a parenthesis, a " \
               "quote or a backslash")); \
            return AttributeNotApplied; \
          } \
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
