#pragma once
#include "../../capi/sysio/kv.h"

#include <cstdint>

// RAII wrappers for KV iterator handles.
// Parameterized on destroy function — one WASM code copy per instantiation.

namespace sysio { namespace kv { namespace detail {

template<void(*DestroyFn)(int32_t)>
struct kv_handle {
   int32_t h = -1;
   kv_handle() = default;
   explicit kv_handle(int32_t v) : h(v) {}
   ~kv_handle() { if (h >= 0) DestroyFn(h); }
   kv_handle(kv_handle&& o) noexcept : h(o.h) { o.h = -1; }
   kv_handle& operator=(kv_handle&& o) noexcept {
      if (this != &o) { reset(); h = o.h; o.h = -1; }
      return *this;
   }
   kv_handle(const kv_handle&) = delete;
   kv_handle& operator=(const kv_handle&) = delete;
   void reset(int32_t v = -1) { if (h >= 0) DestroyFn(h); h = v; }
   operator int32_t() const { return h; }
};

using it_handle  = kv_handle<::kv_it_destroy>;
using idx_handle = kv_handle<::kv_idx_destroy>;

}}} // namespace sysio::kv::detail
