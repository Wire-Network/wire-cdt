// Legacy db_*_i64 C API tests replaced with KV intrinsic tests.
// The KV C API is declared in <sysio/kv.h>.

#include <sysio/kv.h>
#include <stddef.h>

void test_db( void ) {
   // Primary KV operations (table_id 0)
   int32_t status = kv_set(0, 1, "k", 1, "v", 1, kv_set_upsert);
   (void)status;
   kv_get(0, 0, "k", 1, 0, NULL, 0);
   status = kv_erase(0, "k", 1);
   (void)status;
   {
      // A 24-byte key (table_id 1)
      char key24[24] = {0};
      status = kv_set(1, 1, key24, 24, "val", 3, kv_set_insert);
      (void)status;
      kv_get(1, 0, key24, 24, 0, NULL, 0);
      status = kv_erase(1, key24, 24);
      (void)status;
   }

   // Primary iterators
   uint32_t key_size = 0;
   uint32_t value_size = 0;
   int32_t h = kv_it_create(0, 0, "p", 1, NULL, 0);
   kv_it_next(h, NULL, 0, NULL, 0, &key_size, &value_size);
   kv_it_prev(h, NULL, 0, NULL, 0, &key_size, &value_size);
   kv_it_lower_bound(h, "k", 1, NULL, 0, NULL, 0, &key_size, &value_size);
   kv_it_read(h, 0, NULL, 0, NULL, 0, &key_size, &value_size);
   kv_it_destroy(h);

   // Secondary index operations
   status = kv_idx_store(100, 1, "p", 1, "s", 1);
   (void)status;
   status = kv_idx_update(100, 0, "p", 1, "s", 1, "t", 1);
   (void)status;
   status = kv_idx_erase(100, "p", 1, "t", 1);
   (void)status;
   int32_t sh = kv_idx_create(100, 0, NULL, 0, "s", 1);
   kv_idx_next(sh, NULL, 0, NULL, 0, &key_size, &value_size);
   kv_idx_prev(sh, NULL, 0, NULL, 0, &key_size, &value_size);
   kv_idx_lower_bound(sh, "s", 1, NULL, 0, NULL, 0, &key_size, &value_size);
   kv_idx_read(sh, NULL, 0, NULL, 0, &key_size, &value_size);
   kv_idx_destroy(sh);
   int32_t found = kv_idx_find(100, 0, NULL, 0, "s", 1);
   (void)found;
}
