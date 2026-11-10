#pragma once

#include <string_view>

/// Identifiers the generated per-action wrappers in `<contract>.actions.cpp` refer to, written by the
/// `sysio_codegen` plugin. Each one names a declaration in `sysiolib`'s `<sysio/dispatcher.hpp>`.
namespace sysio::cdt::dispatch_gen {
   /// The sysiolib header that declares everything the generated wrappers call.
   constexpr std::string_view dispatcher_header = "sysio/dispatcher.hpp";

   /// Called by a generated action wrapper once it has unpacked the action's arguments.
   constexpr std::string_view check_action_data_consumed_function = "sysio::check_action_data_consumed";
} // namespace sysio::cdt::dispatch_gen
