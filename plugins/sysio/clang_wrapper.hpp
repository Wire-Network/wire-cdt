#pragma once
#include <clang/AST/Attr.h>
#include <clang/AST/DeclBase.h>
#include "tokenize.hpp"
#include <optional>
#include <map>
#include <string>

namespace sysio_plugin { namespace clang_wrapper {
   class Attr {
      public:
         Attr() {}
         Attr(std::string arg): arg(arg) {}

         llvm::StringRef getName() const { return arg; }
         std::string getNameAsString() const { return arg; }

      private:
         std::string arg;
   };

   template<typename T>
   class Decl {
      public:
         Decl(T decl): decl(decl) {
            if (!decl || !decl->hasAttrs())
               return;
            // In LLVM 18, each sysio attribute gets its own AnnotateAttr.
            // Iterate over ALL AnnotateAttr instances to collect all attributes.
            for (auto* annotate : decl->template specific_attrs<clang::AnnotateAttr>()) {
               auto tokens = sysio_plugin::tokenize(remove_quotes(annotate->getAnnotation().str()));
               for (const auto& token: tokens) {
                  std::string str = token;
                  std::string arg;
                  auto l = str.find('(');
                  if (l != std::string::npos) {
                     auto r = str.find(')');
                     arg = str.substr(l+1, r-(l+1));
                     str = str.substr(0, l);
                  }
                  attrs.emplace(str, Attr(arg));
               }
            }
         };

         auto operator->() { return decl; }
         const auto operator->() const { return decl; }
         auto operator*() { return decl; }
         const auto operator*() const { return decl; }
         operator bool() { return decl != nullptr; }

         auto getParent() const {
            auto p = llvm::dyn_cast<clang::CXXRecordDecl>(decl->getParent());
            return Decl<decltype(p)>(p);
         }

         bool isSysioAction() const {
            return attrs.find("sysio_action") != attrs.end();
         }

         bool isSysioContract() const {
            return attrs.find("sysio_contract") != attrs.end();
         }

         bool isSysioTable() const {
            return attrs.find("sysio_table") != attrs.end();
         }

         bool isSysioType() const {
            return attrs.find("sysio_type") != attrs.end();
         }

         bool isSysioIgnore() const {
            return attrs.find("sysio_ignore") != attrs.end();
         }

         bool isSysioNotify() const {
            return attrs.find("sysio_on_notify") != attrs.end();
         }

         bool hasSysioRicardian() const {
            return false;
         }

         bool isSysioWasmEntry() const {
            auto* wasm_entry = decl->template getAttr<clang::WebAssemblyExportNameAttr>();
            return !!wasm_entry;
         }

         bool isSysioReadOnly() const {
            return attrs.find("sysio_read_only") != attrs.end();
         }

         const Attr* getSysioActionAttr() const {
            return isSysioAction() ? &attrs.at("sysio_action") : nullptr;
         }

         const Attr* getSysioContractAttr() const {
            return isSysioContract() ? &attrs.at("sysio_contract") : nullptr;
         }

         const Attr* getSysioTableAttr() const {
            return isSysioTable() ? &attrs.at("sysio_table") : nullptr;
         }

         const Attr* getSysioTypeAttr() const {
            return isSysioType() ? &attrs.at("sysio_type") : nullptr;
         }

         const Attr* getSysioNotifyAttr() const {
            return isSysioNotify() ? &attrs.at("sysio_on_notify") : nullptr;
         }

         const Attr* getSysioRicardianAttr() const {
            static const Attr empty{""};
            return &empty;
         }

         const auto* getSysioWasmEntry() const {
            auto* wasm_entry = decl->template getAttr<clang::WebAssemblyExportNameAttr>();
            return wasm_entry;
         }

         std::optional<std::string> getAttribute(const char* name) const {
            auto it = attrs.find(name);
            return it != attrs.end() ? std::make_optional(it->second.getNameAsString()) : std::optional<std::string>();
         }

      private:
         T decl;
         std::map<std::string,Attr> attrs;
   };

   template<typename T>
   Decl<T> wrap_decl(T decl) {
      return {decl};
   }
} }
