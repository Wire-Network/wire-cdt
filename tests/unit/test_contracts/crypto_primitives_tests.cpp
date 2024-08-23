#include <sysio/sysio.hpp>
#include <sysio/crypto.hpp>
#include <sysio/crypto_ext.hpp>

using namespace sysio;

class [[sysio::contract]] crypto_primitives_tests : public contract{
   public:
      using contract::contract;

      [[sysio::action]]
      void sha3test(std::string val, sysio::checksum256 sha3_dg) {
         auto hash = sysio::sha3(val.c_str(), val.size());

         sysio::check(hash == sha3_dg, "SHA3 doesn't match expected");
         sysio::assert_sha3(val.c_str(), val.size(), sha3_dg);
      }

      [[sysio::action]]
      void keccaktest(std::string val, sysio::checksum256 sha3_dg) {
         auto hash = sysio::keccak(val.c_str(), val.size());

         sysio::check(hash == sha3_dg, "Keccak doesn't match expected");
         sysio::assert_keccak(val.c_str(), val.size(), sha3_dg);
      }

      template <typename T>
      void addtest_helper(T& p1, T& p2, std::vector<char>& expected_x, std::vector<char>& expected_y) {
         auto result = sysio::alt_bn128_add(p1, p2);
         sysio::check(result.x == expected_x, "alt_bn128_add: result x does not match");
         sysio::check(result.y == expected_y, "alt_bn128_add: result y does not match");
      }

      // test add where points are constructed from x and y
      [[sysio::action]]
      void addtest(std::vector<char>& x1, std::vector<char>& y1, std::vector<char>& x2, std::vector<char>& y2, std::vector<char>& expected_x, std::vector<char>& expected_y) {
         // point
         sysio::g1_point point1 {x1, y1};
         sysio::g1_point point2 {x2, y2};
         addtest_helper( point1, point2, expected_x, expected_y );

         // view
         sysio::g1_point_view point_view1 {x1.data(), x1.size(), y1.data(), y1.size()};
         sysio::g1_point_view point_view2 {x2.data(), x2.size(), y2.data(), y2.size()};
         addtest_helper( point_view1, point_view2, expected_x, expected_y );

         sysio::g1_point_view point_view1_1 {point1};
         sysio::g1_point_view point_view2_1 {point2};
         addtest_helper( point_view1_1, point_view2_1, expected_x, expected_y );
      }

      // test add where points are constructed from other points
      [[sysio::action]]
      void addtest1(std::vector<char>& p1, std::vector<char>& p2, std::vector<char>& expected_x, std::vector<char>& expected_y) {
         // point
         sysio::g1_point point1 {p1};
         sysio::g1_point point2 {p2};
         addtest_helper( point1, point2, expected_x, expected_y );

         // view
         sysio::g1_point_view point_view1 {p1};
         sysio::g1_point_view point_view2 {p2};
         addtest_helper( point_view1, point_view2, expected_x, expected_y );

         sysio::g1_point_view point_view1_1 {point1};
         sysio::g1_point_view point_view2_1 {point2};
         addtest_helper( point_view1_1, point_view2_1, expected_x, expected_y );
      }

      template <typename T>
      void multest_helper(T& g1, sysio::bigint& s, std::vector<char>& expected_x, std::vector<char>& expected_y) {
         auto result = sysio::alt_bn128_mul(g1, s);
         sysio::check(result.x == expected_x, "alt_bn128_mul: Result x does not match");
         sysio::check(result.y == expected_y, "alt_bn128_mul: Result y does not match");
      }

      [[sysio::action]]
      void multest(std::vector<char>& g1_x, std::vector<char>& g1_y, std::vector<char>& scalar, std::vector<char>& expected_x, std::vector<char>& expected_y) {
         sysio::bigint s {scalar};

         // point
         sysio::g1_point g1_point {g1_x, g1_y};
         multest_helper(g1_point, s, expected_x, expected_y);

         // view
         sysio::g1_point_view g1_view {g1_x.data(), g1_x.size(), g1_y.data(), g1_y.size()};
         multest_helper(g1_view, s, expected_x, expected_y);

         sysio::g1_point_view g1_view_1 {g1_point};
         multest_helper(g1_view_1, s, expected_x, expected_y);
      }

      template <typename G1_T, typename G2_T>
      void pairtest_helper(G1_T& g1_a, G2_T& g2_a, G1_T& g1_b, G2_T& g2_b, int32_t expected) {
         std::vector<std::pair<G1_T, G2_T>> pairs { {g1_a, g2_a}, {g1_b, g2_b} };
         auto rc = sysio::alt_bn128_pair(pairs);
         sysio::check(rc == expected, "alt_bn128_pair::return code not match");
      }

      // test pairtest where points are constructed from x and y
      [[sysio::action]]
      void pairtest(std::vector<char>& g1_a_x, std::vector<char>& g1_a_y, std::vector<char>& g2_a_x, std::vector<char>& g2_a_y, std::vector<char>& g1_b_x, std::vector<char>& g1_b_y, std::vector<char>& g2_b_x, std::vector<char>& g2_b_y, int32_t expected) {
         // point
         sysio::g1_point g1_a {g1_a_x, g1_a_y};
         sysio::g2_point g2_a {g2_a_x, g2_a_y};
         sysio::g1_point g1_b {g1_b_x, g1_b_y};
         sysio::g2_point g2_b {g2_b_x, g2_b_y};
         pairtest_helper( g1_a, g2_a, g1_b, g2_b, expected );

         // view
         sysio::g1_point_view g1_view_a {g1_a_x.data(), g1_a_x.size(), g1_a_y.data(), g1_a_y.size()};
         sysio::g2_point_view g2_view_a {g2_a_x.data(), g2_a_x.size(), g2_a_y.data(), g2_a_y.size()};
         sysio::g1_point_view g1_view_b {g1_b_x.data(), g1_b_x.size(), g1_b_y.data(), g1_b_y.size()};
         sysio::g2_point_view g2_view_b {g2_b_x.data(), g2_b_x.size(), g2_b_y.data(), g2_b_y.size()};
         pairtest_helper( g1_view_a, g2_view_a, g1_view_b, g2_view_b, expected );

         sysio::g1_point_view g1_view_a_1 { g1_a };
         sysio::g2_point_view g2_view_a_1 { g2_a };
         sysio::g1_point_view g1_view_b_1 { g1_b };
         sysio::g2_point_view g2_view_b_1 { g2_b };
         pairtest_helper( g1_view_a_1, g2_view_a_1, g1_view_b_1, g2_view_b_1, expected );
      }

      // test pairtest where points are constructed from other points
      [[sysio::action]]
      void pairtest1(std::vector<char>& g1a, std::vector<char>& g2a, std::vector<char>& g1b, std::vector<char>& g2b, int32_t expected) {
         // point
         sysio::g1_point g1_a { g1a };
         sysio::g2_point g2_a { g2a };
         sysio::g1_point g1_b { g1b };
         sysio::g2_point g2_b { g2b };
         pairtest_helper( g1_a, g2_a, g1_b, g2_b, expected );

         // view
         sysio::g1_point_view g1_view_a { g1a };
         sysio::g2_point_view g2_view_a { g2a };
         sysio::g1_point_view g1_view_b { g1b };
         sysio::g2_point_view g2_view_b { g2b };
         pairtest_helper( g1_view_a, g2_view_a, g1_view_b, g2_view_b, expected );
      }

      [[sysio::action]]
      void modexptest(std::vector<char>& base, std::vector<char>& exp, std::vector<char>& modulo, int32_t expected_rc, const std::vector<char>& expected_result) {
         sysio::bigint base_val {base};
         sysio::bigint exp_val {exp};
         sysio::bigint modulo_val {modulo};
         sysio::bigint result( modulo.size(), '\0' );

         auto rc = sysio::mod_exp(base_val, exp_val, modulo_val, result);
         sysio::check(rc == expected_rc, "return code does not match");
         sysio::check(result == expected_result, "Result does not match");
      }

      [[sysio::action]]
      void blake2ftest(uint32_t rounds, const std::vector<char>& state, const std::vector<char>& msg, const std::vector<char>& t0_offset, const std::vector<char>& t1_offset, bool final, int32_t expected_rc, const std::vector<char>& expected_result) {
         std::vector<char> result(sysio::blake2f_result_size);

         auto rc = sysio::blake2_f(rounds, state, msg, t0_offset, t1_offset, final, result);
         sysio::check(rc == expected_rc, "return code does not match");

         std::vector<char> actual_result(result.data(), result.data() + result.size());
         sysio::check(actual_result == expected_result, "Result does not match");
      }
};
