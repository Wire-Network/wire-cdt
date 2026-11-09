// The chain does not support context-free actions, so sysiolib declares no context-free API: no
// send_context_free_inline or get_context_free_data intrinsic, and no action::send_context_free.
// A contract that still uses one fails to compile instead of failing to deploy.
#include <sysio/sysio.hpp>
#include <sysio/transaction.hpp>

#include <vector>

void uses_context_free_api() {
   sysio::action act{std::vector<sysio::permission_level>{}, "test"_n, "hi"_n, std::tuple<>{}};
   act.send_context_free();
   char buf[8];
   sysio::get_context_free_data(0, buf, sizeof(buf));
   sysio::internal_use_do_not_use::send_context_free_inline(buf, sizeof(buf));
}
