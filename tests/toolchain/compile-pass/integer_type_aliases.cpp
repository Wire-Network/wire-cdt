/// Verify identical extended integer type semantics in WASM and native modules.
/// In particular, pin the brace-initialization expressions used by sysio.opreg.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <type_traits>

#if defined(uint128_t) || defined(int128_t)
#error "Extended integer names must be types, not preprocessor macros"
#endif

static_assert(std::is_same_v<uint128_t, unsigned __int128>);
static_assert(std::is_same_v<int128_t, __int128>);
static_assert(sizeof(uint128_t) == 16 && sizeof(int128_t) == 16);

constexpr uint128_t max_debt = ~uint128_t{0};
constexpr uint128_t max_payment = uint128_t{std::numeric_limits<uint64_t>::max()};
constexpr uint128_t owed = uint128_t{1} << 64;
static_assert(owed <= max_debt - owed);
static_assert(std::min(owed, max_payment) == max_payment);
static_assert(max_debt + uint128_t{1} == uint128_t{0});
static_assert(int128_t{-1} < int128_t{0});
static_assert(uint128_t(int128_t{-1}) == max_debt);

/// Qualified and nested aliases must retain ordinary C++ name lookup semantics.
struct integer_aliases {
   using uint128_t = ::uint128_t;
   using int128_t = ::int128_t;
};
static_assert(std::is_same_v<integer_aliases::uint128_t, ::uint128_t>);
static_assert(std::is_same_v<integer_aliases::int128_t, ::int128_t>);
