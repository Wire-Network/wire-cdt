#include <sysio/sysio.hpp>
#include <sysio/pb.hpp>
#include <test/test.pb.hpp>

namespace test {

class [[sysio::contract]] pb_tests : public sysio::contract {
public:
   using sysio::contract::contract;

   [[sysio::action]]
   sysio::pb<ActResult> hiproto(const sysio::pb<ActData>& msg) {
      sysio::check(static_cast<int32_t>(msg.id) == 1, "validate msg.id");
      sysio::check(static_cast<int32_t>(msg.type) == 2, "validate msg.type");
      sysio::check(msg.note == "hello", "validate msg.note");

      ActResult result;
      result.value = 42;
      result.str_value = "result_string";
      return result;
   }

   [[sysio::action]]
   void pbaction(const sysio::pb<ActData>& msg) {
      sysio::check(static_cast<int32_t>(msg.id) > 0, "id must be positive");
      sysio::print("Received protobuf action with id=", static_cast<int32_t>(msg.id));
   }

   // Multi-param action: generates a wrapper struct with two protobuf fields
   [[sysio::action]]
   void pbmulti(const sysio::pb<ActData>& data, const sysio::pb<ActResult>& result) {
      sysio::check(static_cast<int32_t>(data.id) > 0, "data.id must be positive");
      sysio::check(static_cast<int32_t>(result.value) > 0, "result.value must be positive");
      sysio::print("Multi-param protobuf action");
   }
};

} // namespace test
