#pragma once

#include "name.hpp"

namespace sysio {
   namespace internal_use_do_not_use {
      extern "C" uint64_t sysio_contract_name;
   }

   inline name current_context_contract() { return name{internal_use_do_not_use::sysio_contract_name}; }
}
