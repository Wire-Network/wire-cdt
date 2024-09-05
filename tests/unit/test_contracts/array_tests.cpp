#include <sysio/sysio.hpp>
#include <sysio/transaction.hpp>
#include <sysio/name.hpp>

using namespace sysio;

class [[sysio::contract]] array_tests : public contract {
   public:
      using contract::contract;

   TABLE tests {
      uint64_t                    id;
      std::array<uint8_t,32>      str;
      uint64_t primary_key()      const { return id; }
   };

   struct info {
      int age;
      std::string name;
   };

   typedef multi_index<name("tests"), tests> tests_table;
   typedef std::array<std::string,4> array_string_4;
   struct my_struct {
      uint32_t id;
      std::array<array_string_4,2> aastr;

      bool operator==(const my_struct& b) const {
         return id == b.id &&
                aastr == b.aastr;
      }
   };

   // test inside using std::array
   [[sysio::action]]
   void testin(std::string message) {
      tests_table _tests(get_self(), get_self().value);

      std::array<uint8_t, 32> str = {'a','a','a','a','a','a','a','a',
                                        'a','a','a','a','a','a','a','a',
                                        'a','a','a','a','a','a','a','a',
                                        'a','a','a','a','a','a','a','a' };
      int len =  message.length() < 32 ? message.length() : 32;
      for(int i = 0; i < len ; ++i){
         str[i] = (uint8_t)message[i];
      }

      std::array<uint8_t, 32> str2 = str;
      sysio::cout << "size of std::array str is : " << str.size() << "\n";
      for(int i = 0; i < 32; ++i){
         sysio::cout << str[i] << " ";
      }
      sysio::cout << "\n";
      for(int i = 0; i < 32; ++i){
         sysio::cout << str2[i] << " ";
      }
      sysio::cout << "\n";
      std::array<info, 2> info_arr;
      info_arr[0].age = 20;
      info_arr[0].name = "abc";
      info_arr[1].age = 21;
      info_arr[1].name = "cde";
      for(int i = 0; i < 2; ++i){
         sysio::cout << info_arr[i].age << " " << info_arr[i].name << "\n";
      }
   }

   // test parameter  using std::array
   // not supported so far
   [[sysio::action]]
   void testpa(std::array<int,4> input){
      std::array<int,4> arr = input;
      for(int i = 0; i < 4; ++i){
         sysio::cout << arr[i] << " ";
      }
      sysio::cout << "\n";
   }

   // test return using std::array, not supported so far
   [[sysio::action]]
   // clio -v push action sysio testre '[[1,2,3,4]]' -p sysio@active
   std::array<int,4> testre(std::array<int,4> input){
      std::array<int,4> arr = input;
      for(auto & v : arr) v += 1;
      return arr;
   }

   // test return using std::vector
   [[sysio::action]]
   // clio -v push action sysio testrev '[[1,2,3,4]]' -p sysio@active
   std::vector<int> testrev(std::vector<int> input){
      std::vector<int> vec = input;
      for(auto & v : vec) v += 1;
      return vec;
   }

   // test nested array
   [[sysio::action]]
   void testne() {
      std::array<tests,2> nest;
      std::array<uint8_t, 32> str = {'a','a','a','a','a','a','a','a',
                                     'a','a','a','a','a','a','a','a',
                                     'a','a','a','a','a','a','a','a',
                                     'a','a','a','a','a','a','a','a' };

      nest[0].id = 1;
      nest[0].str = str;
      nest[1].id = 2;
      nest[1].str = str;
      for(int i = 0; i < nest.size(); ++i){
         sysio::cout << nest[i].id << "   " ;
         for(int j = 0; j < nest[i].str.size(); ++j) {
            sysio::cout << nest[i].str[j] + i << " ";
         }
         sysio::cout << "\n";
      }
      std::array<std::array<std::string, 5>, 3> nest2;
      for(int i = 0; i < nest2.size(); ++i){
         for(int j = 0; j < nest2[i].size(); ++j) {
            nest2[i][j] = "test nested ";
            sysio::cout << nest2[i][j] << " ";
         }
         sysio::cout << "\n";
      }
   }

   // test complex data
   [[sysio::action]]
   void testcom(name user) {
      require_auth(user);
      tests_table _tests(get_self(), get_self().value);

      std::array<uint8_t, 32> str = {'a','a','a','a','a','a','a','a',
                                     'a','a','a','a','a','a','a','a',
                                     'a','a','a','a','a','a','a','a',
                                     'a','a','a','a','a','a','a','a' };
      _tests.emplace(user, [&](auto& t) {
         t.id = user.value + std::time(0);  // primary key can't be same
         t.str = str;
      });
      auto it = _tests.begin();
      auto ite = _tests.end();
      while(it != ite){
         sysio::cout << "id = " << it->id << "\n";
         for(int i = 0; i < it->str.size(); ++i) {
            sysio::cout << it->str[i] << " ";
         }
         sysio::cout << "\n";
         ++it;
      }
   }

};
