#include <hash_id_example.hpp>

[[sysio::action]]
void hash_id_example::setpref(name user, std::string category, std::string value) {
   require_auth(user);
   // upsert — insert or update
   prefs.set(get_self(), {user, category}, {value, 0});
}

[[sysio::action]]
void hash_id_example::getpref(name user, std::string category) {
   auto val = prefs.try_get({user, category});
   if (val) {
      print("Preference: ", val->value, " (updated ", val->updated_at, ")\n");
   } else {
      print("No preference set for ", user, "/", category, "\n");
   }
}

[[sysio::action]]
void hash_id_example::setflags(bool notif, bool dark) {
   require_auth(get_self());
   flags.set({notif, dark}, get_self());
}

[[sysio::action]]
void hash_id_example::getflags() {
   auto f = flags.get_or_default({true, false});
   print("notifications=", f.enable_notifications, " dark_mode=", f.enable_dark_mode, "\n");
}
