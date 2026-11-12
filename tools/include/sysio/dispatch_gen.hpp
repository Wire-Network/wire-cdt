#pragma once

#include <string_view>

/// Identifiers the generated dispatch code refers to, shared by the `sysio_codegen` plugin (the per-action
/// wrappers in `<contract>.actions.cpp`) and `cdt-codegen` (the `apply()` router in `<contract>.dispatch.cpp`).
/// Each one names a declaration in `sysiolib`'s `<sysio/dispatcher.hpp>`.
namespace sysio::cdt::dispatch_gen {
   /// The sysiolib header that declares everything the generated dispatch code calls.
   constexpr std::string_view dispatcher_header = "sysio/dispatcher.hpp";

   /// Called by a generated action wrapper once it has unpacked the action's arguments.
   constexpr std::string_view check_action_data_consumed_function = "sysio::check_action_data_consumed";

   /// Called by the generated `apply()` for an action name the contract does not define.
   constexpr std::string_view reject_undefined_action_function = "sysio::reject_undefined_action";
} // namespace sysio::cdt::dispatch_gen
