// Verifies that the dispatching code is self-contained

class [[sysio::contract]] minimal_tests {
 public:
   template<typename N, typename DS>
   explicit constexpr minimal_tests(const N&, const N&, const DS&) {}
   [[sysio::action]] void test1() {}
};
