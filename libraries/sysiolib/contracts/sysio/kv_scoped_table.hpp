#pragma once
/**
 * kv::scoped_table — scoped KV table producing byte-identical primary keys
 * to kv_multi_index: [scope:8B BE][K encoded].
 *
 * Usage:
 *   struct pk_key { uint64_t sym_code; SYSLIB_SERIALIZE(pk_key, (sym_code)) };
 *   struct account { asset balance; SYSLIB_SERIALIZE(account, (balance)) };
 *   using accounts = kv::scoped_table<"accounts"_n, pk_key, account>;
 *
 *   // In action:
 *   accounts accts(get_self(), owner.value);  // scope = owner
 *   accts.emplace("alice"_n, pk_key{sym.code().raw()}, account{bal});
 *   auto it = accts.find(pk_key{sym.code().raw()});
 *
 *   // Scope iteration:
 *   for (auto sit = accounts::scope_lower_bound(code, 0);
 *        sit != accounts::scope_end(); ++sit)
 *      print(*sit, " ");
 */

#include <sysio/kv_table.hpp>
#include <limits>

namespace sysio { namespace kv {

template<name::raw TableName, typename K, typename V, typename... Indices>
class scoped_table : public table_impl<TableName, K, V, true, Indices...> {
   using base = table_impl<TableName, K, V, true, Indices...>;
   static constexpr uint32_t _table_id = sysio::kv::compute_table_id(static_cast<uint64_t>(TableName));

public:
   scoped_table(sysio::name code, uint64_t scope) : base(code, scope) {}
   uint64_t get_scope() const { return this->_scope; }

   // --- Scope iteration ---
   // Iterates distinct scope values by walking primary keys and skipping
   // to the next scope prefix on each advance.  No new intrinsics needed.

   struct scope_iterator {
      using iterator_category = std::input_iterator_tag;
      using value_type        = uint64_t;
      using difference_type   = std::ptrdiff_t;
      using pointer           = const uint64_t*;
      using reference         = uint64_t;

      scope_iterator() = default;

      uint64_t operator*() const { sysio::check(_valid, "deref end scope_iterator"); return _scope; }

      scope_iterator& operator++() {
         if (!_valid) return *this;
         if (_scope == std::numeric_limits<uint64_t>::max()) {
            _valid = false;
            return *this;
         }
         uint64_t next = _scope + 1;
         char next_key[kv_scope_size];
         kv::encode_be64(next_key, next);
         int32_t status = ::kv_it_lower_bound(_handle, next_key, kv_scope_size);
         if (status != 0) { _valid = false; return *this; }
         load_scope();
         return *this;
      }

      scope_iterator operator++(int) = delete;

      friend bool operator==(const scope_iterator& a, const scope_iterator& b) {
         return a._valid == b._valid && (!a._valid || a._scope == b._scope);
      }


      scope_iterator(scope_iterator&& o) noexcept
         : _handle(std::move(o._handle)), _valid(o._valid), _scope(o._scope)
      { o._valid = false; }
      scope_iterator& operator=(scope_iterator&& o) noexcept {
         if (this != &o) {
            _handle = std::move(o._handle); _valid = o._valid; _scope = o._scope;
            o._valid = false;
         }
         return *this;
      }
      scope_iterator(const scope_iterator&) = delete;
      scope_iterator& operator=(const scope_iterator&) = delete;

   private:
      friend class scoped_table;
      kv::detail::it_handle _handle;
      bool     _valid = false;
      uint64_t _scope = 0;

      scope_iterator(uint32_t h, bool valid) : _handle(h), _valid(valid) {
         if (_valid) load_scope();
      }

      void load_scope() {
         char key_buf[kv_scope_size];
         uint32_t actual = 0;
         if (::kv_it_key(_handle, 0, key_buf, kv_scope_size, &actual) != 0 || actual < kv_scope_size) {
            _valid = false; return;
         }
         _scope = kv::decode_be64(key_buf);
      }
   };

   /// Lower-bound scope iteration: finds the first scope >= \p scope.
   static scope_iterator scope_lower_bound(sysio::name code, uint64_t scope) {
      uint64_t c = code.value ? code.value : sysio::current_receiver().value;
      uint32_t h = ::kv_it_create(_table_id, c, nullptr, 0);
      char scope_key[kv_scope_size];
      kv::encode_be64(scope_key, scope);
      int32_t status = ::kv_it_lower_bound(h, scope_key, kv_scope_size);
      return scope_iterator(h, status == 0);
   }

   static scope_iterator scope_end() {
      return scope_iterator();
   }
};

} } // namespace sysio::kv
