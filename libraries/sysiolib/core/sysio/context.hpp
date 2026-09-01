#pragma once

#include "name.hpp"

namespace sysio {
   namespace internal_use_do_not_use {
      /// volatile MUST match the definition in sysiolib.cpp -- differing cv-qualification on
      /// the same entity is ill-formed (no diagnostic required). It links today only because
      /// extern "C" names carry no type and no translation unit sees both spellings.
      extern "C" volatile uint64_t sysio_contract_name;
   }

   inline name current_context_contract() { return name{uint64_t{internal_use_do_not_use::sysio_contract_name}}; }
}
