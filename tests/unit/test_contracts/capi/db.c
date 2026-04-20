// Legacy db_*_i64 C API tests replaced with KV intrinsic tests.
// The KV C API is declared in <sysio/kv.h>.

#include <sysio/kv.h>
#include <stddef.h>

void test_db( void ) {
   // Primary KV operations (format=0 raw)
   kv_set(0, 0, "k", 1, "v", 1);
   kv_get(0, 0, "k", 1, NULL, 0);
   kv_erase(0, "k", 1);
   kv_contains(0, 0, "k", 1);

   // Primary KV operations (format=1 standard 24-byte key)
   {
      char key24[24] = {0};
      kv_set(1, 0, key24, 24, "val", 3);
      kv_get(1, 0, key24, 24, NULL, 0);
      kv_contains(1, 0, key24, 24);
      kv_erase(1, key24, 24);
   }

   // Primary iterators
   uint32_t h = kv_it_create(0, 0, "p", 1);
   kv_it_status(h);
   kv_it_next(h);
   kv_it_prev(h);
   kv_it_lower_bound(h, "k", 1);
   uint32_t actual = 0;
   kv_it_key(h, 0, NULL, 0, &actual);
   kv_it_value(h, 0, NULL, 0, &actual);
   kv_it_destroy(h);

   // Secondary index operations (primary_id threaded through from kv_set)
   kv_idx_store(0, 100, 1, "s", 1);
   kv_idx_update(0, 100, 1, "s", 1, "t", 1);
   kv_idx_remove(100, 1, "s", 1);
   int32_t sh = kv_idx_find_secondary(0, 100, "s", 1);
   int32_t lb = kv_idx_lower_bound(0, 100, "s", 1);
   (void)lb;
   kv_idx_next(sh);
   kv_idx_prev(sh);
   kv_idx_key(sh, 0, NULL, 0, &actual);
   kv_idx_primary_key(sh, 0, NULL, 0, &actual);
   kv_idx_destroy(sh);
}
