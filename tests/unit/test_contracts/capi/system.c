#include <sysio/system.h>
#include <stddef.h>

void test_system( void ) {
   sysio_assert(0, NULL);
   sysio_assert_message(0, NULL, 0);
   sysio_assert_code(0, 0);
   sysio_exit(0);
   current_time();
   is_feature_activated(NULL);
   get_sender();
}
