#pragma once
#include <cstdint>

// RAII wrappers for KV iterator handles.
// Parameterized on destroy function — one WASM code copy per instantiation.

extern "C" {
   __attribute__((sysio_wasm_import))
   void kv_it_destroy(uint32_t handle);
   __attribute__((sysio_wasm_import))
   void kv_idx_destroy(uint32_t handle);
}

namespace sysio { namespace kv { namespace detail {

template<void(*DestroyFn)(uint32_t)>
struct kv_handle {
   int32_t h = -1;
   kv_handle() = default;
   explicit kv_handle(int32_t v) : h(v) {}
   explicit kv_handle(uint32_t v) : h(static_cast<int32_t>(v)) {}
   ~kv_handle() { if (h >= 0) DestroyFn(static_cast<uint32_t>(h)); }
   kv_handle(kv_handle&& o) noexcept : h(o.h) { o.h = -1; }
   kv_handle& operator=(kv_handle&& o) noexcept {
      if (this != &o) { reset(); h = o.h; o.h = -1; }
      return *this;
   }
   kv_handle(const kv_handle&) = delete;
   kv_handle& operator=(const kv_handle&) = delete;
   void reset(int32_t v = -1) { if (h >= 0) DestroyFn(static_cast<uint32_t>(h)); h = v; }
   void reset(uint32_t v) { reset(static_cast<int32_t>(v)); }
   int32_t release() { int32_t v = h; h = -1; return v; }
   operator int32_t() const { return h; }
};

using it_handle  = kv_handle<::kv_it_destroy>;
using idx_handle = kv_handle<::kv_idx_destroy>;

}}} // namespace sysio::kv::detail
