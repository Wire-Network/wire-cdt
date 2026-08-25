class [[sysio::contract]] launcher_guard {
 public:
  template<typename Name, typename DataStream>
  explicit constexpr launcher_guard(const Name&, const Name&, const DataStream&) {}

  [[sysio::action]] void ping() {}
};
