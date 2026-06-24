#pragma once
#include <sysio/sysio.hpp>

/**
 * Regression contract for cdt-ld's link-time ABI/dispatch finalize across MULTIPLE source
 * subdirectories.
 *
 * The two actions are defined in two different subdirectories -- @ref multidir_contract::seta in
 * @c multidir/part_a/ and @ref multidir_contract::setb in @c multidir/part_b/. CMake mirrors the
 * source layout under @c CMakeFiles/multidir_contract.dir/, so the two objects (and the per-object
 * finalize sidecars cdt-cpp writes next to them) live in different directories. The dispatcher
 * output path recorded in those sidecars must therefore be a TARGET-level location; deriving it
 * from each object's own directory makes the sidecars disagree and cdt-ld aborts the link. This
 * contract fails to build if that regresses, and the accompanying script test additionally asserts
 * that the merged ABI contains BOTH subdirectories' actions.
 */
class [[sysio::contract("multidir_contract")]] multidir_contract : public sysio::contract {
public:
   using sysio::contract::contract;

   /// Action whose handler is defined in @c multidir/part_a/multidir_a.cpp.
   [[sysio::action]] void seta(uint64_t v);

   /// Action whose handler is defined in @c multidir/part_b/multidir_b.cpp.
   [[sysio::action]] void setb(uint64_t v);
};

/**
 * Helper defined in @c multidir/part_b/ and called from @c multidir/part_a/. Its only purpose is to
 * create a genuine cross-subdirectory link edge, so the test proves both objects are linked
 * together (not merely that each compiles).
 *
 * @param v value to double
 * @return @p v multiplied by two
 */
uint64_t multidir_doubled(uint64_t v);
