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
         char     key[kv_scope_size];
         uint32_t key_size = 0;
         uint32_t value_size = 0;
         hold_scope(::kv_it_lower_bound(_handle, next_key, kv_scope_size, key, kv_scope_size, nullptr, 0,
                                        &key_size, &value_size), key, key_size);
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

      /// Take over a handle and read the scope of the row it was opened on. A short buffer takes
      /// the leading bytes of the key, which are the scope, and the value is skipped.
      explicit scope_iterator(int32_t h) : _handle(h) {
         char     key[kv_scope_size];
         uint32_t key_size = 0;
         uint32_t value_size = 0;
         hold_scope(::kv_it_read(_handle, 0, key, kv_scope_size, nullptr, 0, &key_size, &value_size), key, key_size);
      }

      void hold_scope(int32_t status, const char* key, uint32_t key_size) {
         _valid = status == kv_status_ok && key_size >= kv_scope_size;
         if (_valid) _scope = kv::decode_be64(key);
      }
   };

   /// Lower-bound scope iteration: finds the first scope >= \p scope.
   static scope_iterator scope_lower_bound(sysio::name code, uint64_t scope) {
      uint64_t c = code.value ? code.value : kv::detail::receiving_account().value;
      char scope_key[kv_scope_size];
      kv::encode_be64(scope_key, scope);
      return scope_iterator(::kv_it_create(_table_id, c, nullptr, 0, scope_key, kv_scope_size));
   }

   static scope_iterator scope_end() {
      return scope_iterator();
   }
};

} } // namespace sysio::kv
