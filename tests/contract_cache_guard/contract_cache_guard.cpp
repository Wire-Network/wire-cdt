class [[sysio::contract]] contract_cache_guard {
 public:
  template<typename Name, typename DataStream>
  explicit constexpr contract_cache_guard(const Name&, const Name&, const DataStream&) {}

  [[sysio::action]] void ping() {}
};
