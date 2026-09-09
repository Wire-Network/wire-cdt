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
             auto opens = [&](SourceLocation L) { \
                return Lexer::getSourceText(CharSourceRange(SourceRange(L), true), SM, LangOpts) == "("; \
             }; \
             /* An attribute written inside a macro has an EXPANSION range that ends at the */ \
             /* macro invocation, so the probe above finds no `(` and the attribute reads as */ \
             /* bare -- silently dropping its name. `#define NAMED_TABLE [[sysio::table("x")]]` */ \
             /* published the decoded hash of its `_i` parameter instead of `x`. The argument */ \
             /* is still there at the SPELLING location, inside the macro body, so look for it */ \
             /* there before concluding the attribute has no argument. */ \
             if (!opens(Begin) && Attr.getRange().getBegin().isMacroID()) { \
                auto SpellEnd = SM.getSpellingLoc(Attr.getRange().getEnd()); \
                auto soffset = Lexer::getSourceText(CharSourceRange(SourceRange(SpellEnd), true), SM, LangOpts).size(); \
                auto SpellBegin = SpellEnd.getLocWithOffset(soffset); \
                if (opens(SpellBegin)) \
                   Begin = SpellBegin; \
             } \
             if (opens(Begin)) { \
                /* A C++11-spelled attribute does not get its argument parsed into an Expr, so */ \
                /* the argument is read as SOURCE TEXT -- the token's own spelling, not the */ \
                /* compiler's cooked value. The two disagree in exactly two ways, and both */ \
                /* named a table at another string's table_id: an escape reached the ABI */ \
                /* uncooked, `[[sysio::table("config\x31")]]` publishing `config\x31` at */ \
                /* cooked `config1`'s id; and adjacent literals were truncated to the first, */ \
                /* `("con" "catenated")` publishing `con` at the concatenation's id. */ \
                /* Refused rather than cooked: these are names that reach wire-sysio, SHiP and */ \
                /* Hyperion, and a name worth having is one you can write plainly. */ \
                auto ArgLoc = Begin.getLocWithOffset(1); \
                Token Tok, Next; \
                bool one_plain_literal = \
                   !Lexer::getRawToken(ArgLoc, Tok, SM, LangOpts, true) && \
                   Tok.getKind() == tok::string_literal && \
                   !Lexer::getRawToken(Tok.getEndLoc(), Next, SM, LangOpts, true) && \
                   Next.getKind() == tok::r_paren; \
                if (one_plain_literal) { \
                   /* The token's own spelling, taken by length rather than by re-lexing a */ \
                   /* range, so the whole literal is in hand for the check below. */ \
                   Str = StringRef(SM.getCharacterData(Tok.getLocation()), Tok.getLength()); \
                   one_plain_literal = !Str.contains('\\'); \
                } \
                if (!one_plain_literal) { \
                   /* In a function-like macro the argument here is the PARAMETER, and the */ \
                   /* literal the caller passed is not reachable from it -- say so, since the */ \
                   /* author did write a plain literal, just not where this can read it. */ \
                   if (Attr.getRange().getBegin().isMacroID()) { \
                     S.Diag(ArgLoc, S.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, \
                        "sysio attribute argument must be one plain string literal written in " \
                        "place; a macro parameter cannot be used, since the argument is read as " \
                        "source text")); \
                   } else { \
                     S.Diag(ArgLoc, S.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, \
                        "sysio attribute argument must be one plain string literal -- no escape " \
                        "sequences, no adjacent literals; it is read exactly as written")); \
                   } \
                   return AttributeNotApplied; \
                } \
                /* An empty argument encodes identically to no argument at all, so every later */ \
                /* check reads it as a bare attribute and the validation meant for a written */ \
                /* name is never reached. Refused here, where the difference is still visible. */ \
                if (Str.size() <= 2) { \
                   S.Diag(ArgLoc, S.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, \
                      "sysio attribute argument may not be empty; omit the argument instead")); \
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
