/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 */

#include <limits>
#include <string>

#include <sysio/sysio.hpp>
#include <sysio/tester.hpp>

using std::numeric_limits;
using std::string;

using sysio::name;

constexpr uint64_t u64max = numeric_limits<uint64_t>::max(); // 18446744073709551615ULL

// Definitions in `sysio.cdt/libraries/sysio/name.hpp`
SYSIO_TEST_BEGIN(name_type_test_ctr_num)
   //// constexpr name()
   CHECK_EQUAL( name{}.value, 0ULL )

   //// constexpr explicit name(uint64_t)
   CHECK_EQUAL( name{0ULL}.value, 0ULL )
   CHECK_EQUAL( name{1ULL}.value, 1ULL )
   CHECK_EQUAL( name{u64max}.value, u64max )
   //// constexpr explicit name(name::raw)
   CHECK_EQUAL( name{name::raw{0ULL}}.value, 0ULL )
   CHECK_EQUAL( name{name::raw{1ULL}}.value, 1ULL )
   CHECK_EQUAL( name{name::raw{u64max}}.value, u64max )

   // test that constexpr constructor is evaluated at compile time
   static_assert( name{0ULL}.value == 0ULL );   
   static_assert( name{name::raw{1ULL}}.value == 1ULL );
SYSIO_TEST_END

SYSIO_TEST_BEGIN(name_type_test_ctr_str_lit)
   //// constexpr explicit name(string_view)
   // Note:
   // These are the exact `uint64_t` value representations of the given string
   CHECK_EQUAL( name{"1"}.value, 576460752303423488ULL )
   CHECK_EQUAL( name{"5"}.value, 2882303761517117440ULL )
   CHECK_EQUAL( name{"a"}.value, 3458764513820540928ULL )
   CHECK_EQUAL( name{"z"}.value, 17870283321406128128ULL )

   CHECK_EQUAL( name{"abc"}.value, 3589368903014285312ULL )
   CHECK_EQUAL( name{"123"}.value, 614178399182651392ULL )

   CHECK_EQUAL( name{".abc"}.value,          112167778219196416ULL )
   CHECK_EQUAL( name{".........abc"}.value,  102016ULL )
   CHECK_EQUAL( name{"123."}.value,          614178399182651392ULL )
   CHECK_EQUAL( name{"123........."}.value,  614178399182651392ULL )
   CHECK_EQUAL( name{".a.b.c.1.2.3."}.value, 108209673814966320ULL )
   CHECK_EQUAL( name{"abc.123"}.value, 3589369488740450304ULL )
   CHECK_EQUAL( name{"123.abc"}.value, 614181822271586304ULL )

   CHECK_EQUAL( name{"12345abcdefgj"}.value, 614251623682315983ULL )
   CHECK_EQUAL( name{"hijklmnopqrsj"}.value, 7754926748989239183ULL )
   CHECK_EQUAL( name{"tuvwxyz.1234j"}.value, 14895601873741973071ULL )

   CHECK_EQUAL( name{"111111111111j"}.value, 595056260442243615ULL )
   CHECK_EQUAL( name{"555555555555j"}.value, 2975281302211218015ULL )
   CHECK_EQUAL( name{"aaaaaaaaaaaaj"}.value, 3570337562653461615ULL )
   CHECK_EQUAL( name{"zzzzzzzzzzzzj"}.value, u64max )

   // test that constexpr constructor is evaluated at compile time
   static_assert( name{"1"}.value == 576460752303423488ULL );
SYSIO_TEST_END

SYSIO_TEST_BEGIN(name_type_test_str_not_allowed)
   CHECK_ASSERT( "character is not in allowed character set for names", ([]() {name{"-1"};}) )
   CHECK_ASSERT( "character is not in allowed character set for names", ([]() {name{"0"};}) )
   CHECK_ASSERT( "character is not in allowed character set for names", ([]() {name{"6"};}) )
   CHECK_ASSERT( "thirteenth character in name cannot be a letter that comes after j", ([]() {name{"111111111111k"};}) )
   CHECK_ASSERT( "thirteenth character in name cannot be a letter that comes after j", ([]() {name{"zzzzzzzzzzzzk"};}) )
   CHECK_ASSERT( "string is too long to be a valid name", ([]() {name{"12345abcdefghj"};}) )
SYSIO_TEST_END


SYSIO_TEST_BEGIN(name_type_test_char_to_value)
   // --------------------------------------------
   // static constexpr uint8_t char_to_value(char)
   char c{'.'};
   uint8_t expected_value{}; // Will increment to the expected correct value in the set [0,32)
   CHECK_EQUAL( name::char_to_value(c), expected_value )
   ++expected_value;

   for(c = '1'; c <= '5'; ++c) {
      CHECK_EQUAL( name::char_to_value(c), expected_value )
      ++expected_value;
   }

   for(c = 'a'; c <= 'z'; ++c) {
      CHECK_EQUAL( name::char_to_value(c), expected_value )
      ++expected_value;
   }
SYSIO_TEST_END

SYSIO_TEST_BEGIN(name_type_test_allowed_chars)
   CHECK_ASSERT( "character is not in allowed character set for names", ([]() {name::char_to_value(char{'-'});}) )
   CHECK_ASSERT( "character is not in allowed character set for names", ([]() {name::char_to_value(char{'/'});}) )
   CHECK_ASSERT( "character is not in allowed character set for names", ([]() {name::char_to_value(char{'6'});}) )
   CHECK_ASSERT( "character is not in allowed character set for names", ([]() {name::char_to_value(char{'A'});}) )
   CHECK_ASSERT( "character is not in allowed character set for names", ([]() {name::char_to_value(char{'Z'});}) )
   CHECK_ASSERT( "character is not in allowed character set for names", ([]() {name::char_to_value(char{'`'});}) )
   CHECK_ASSERT( "character is not in allowed character set for names", ([]() {name::char_to_value(char{'{'});}) );
SYSIO_TEST_END

SYSIO_TEST_BEGIN(name_type_test_str_len)
   // -------------------------------
   // constexpr uint8_t length()cosnt
   CHECK_EQUAL( name{""}.length(), 0 )
   CHECK_EQUAL( name{"e"}.length(), 1 )
   CHECK_EQUAL( name{"eo"}.length(), 2 )
   CHECK_EQUAL( name{"eos"}.length(), 3 )
   CHECK_EQUAL( name{"eosi"}.length(), 4 )
   CHECK_EQUAL( name{"sysio"}.length(), 5 )
   CHECK_EQUAL( name{"sysioa"}.length(), 6 )
   CHECK_EQUAL( name{"sysioac"}.length(), 7 )
   CHECK_EQUAL( name{"sysioacc"}.length(), 8 )
   CHECK_EQUAL( name{"sysioacco"}.length(), 9 )
   CHECK_EQUAL( name{"sysioaccou"}.length(), 10 )
   CHECK_EQUAL( name{"sysioaccoun"}.length(), 11 )
   CHECK_EQUAL( name{"sysioaccount"}.length(), 12 )
   CHECK_EQUAL( name{"sysioaccountj"}.length(), 13 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(name_type_test_str_too_long)
   CHECK_ASSERT( "string is too long to be a valid name", ([]() {name{"12345abcdefghj"}.length();}) )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(name_type_test_suffix)
   // ----------------------------
   // constexpr name suffix()const
   CHECK_EQUAL( name{".sysioaccounj"}.suffix(), name{"sysioaccounj"} )
   CHECK_EQUAL( name{"e.osioaccounj"}.suffix(), name{"osioaccounj"} )
   CHECK_EQUAL( name{"eo.sioaccounj"}.suffix(), name{"sioaccounj"} )
   CHECK_EQUAL( name{"eos.ioaccounj"}.suffix(), name{"ioaccounj"} )
   CHECK_EQUAL( name{"eosi.oaccounj"}.suffix(), name{"oaccounj"} )
   CHECK_EQUAL( name{"sysio.accounj"}.suffix(), name{"accounj"} )
   CHECK_EQUAL( name{"sysioa.ccounj"}.suffix(), name{"ccounj"} )
   CHECK_EQUAL( name{"sysioac.counj"}.suffix(), name{"counj"} )
   CHECK_EQUAL( name{"sysioacc.ounj"}.suffix(), name{"ounj"} )
   CHECK_EQUAL( name{"sysioacco.unj"}.suffix(), name{"unj"} )
   CHECK_EQUAL( name{"sysioaccou.nj"}.suffix(), name{"nj"} )
   CHECK_EQUAL( name{"sysioaccoun.j"}.suffix(), name{"j"} )

   CHECK_EQUAL( name{"e.o.s.i.o.a.c"}.suffix(), name{"c"} )
   CHECK_EQUAL( name{"eos.ioa.cco"}.suffix(), name{"cco"} )
SYSIO_TEST_END


SYSIO_TEST_BEGIN(name_type_test_prefix)
   // ----------------------------
   // constexpr name prefix()const

   CHECK_EQUAL( name{".sysioaccounj"}.prefix(), name{} )
   CHECK_EQUAL( name{"e.osioaccounj"}.prefix(), name{"e"} )
   CHECK_EQUAL( name{"eo.sioaccounj"}.prefix(), name{"eo"} )
   CHECK_EQUAL( name{"eos.ioaccounj"}.prefix(), name{"eos"} )
   CHECK_EQUAL( name{"eosi.oaccounj"}.prefix(), name{"eosi"} )
   CHECK_EQUAL( name{"sysio.accounj"}.prefix(), name{"sysio"} )
   CHECK_EQUAL( name{"sysioa.ccounj"}.prefix(), name{"sysioa"} )
   CHECK_EQUAL( name{"sysioac.counj"}.prefix(), name{"sysioac"} )
   CHECK_EQUAL( name{"sysioacc.ounj"}.prefix(), name{"sysioacc"} )
   CHECK_EQUAL( name{"sysioacco.unj"}.prefix(), name{"sysioacco"} )
   CHECK_EQUAL( name{"sysioaccou.nj"}.prefix(), name{"sysioaccou"} )
   CHECK_EQUAL( name{"sysioaccoun.j"}.prefix(), name{"sysioaccoun"} )
   CHECK_EQUAL( name{"sysioaccounj."}.prefix(), name{"sysioaccounj"} )
   CHECK_EQUAL( name{"sysioaccountj"}.prefix(), name{"sysioaccountj"} )

   CHECK_EQUAL( name{"e.o.s.i.o.a.c"}.prefix(), name{"e.o.s.i.o.a"} )
   CHECK_EQUAL( name{"eos.ioa.cco"}.prefix(), name{"eos.ioa"} )

   CHECK_EQUAL( name{"a.my.account"}.prefix(), name{"a.my"} )
   CHECK_EQUAL( name{"a.my.account"}.prefix().prefix(), name{"a"} )

   static_assert( name{"e.osioaccounj"}.prefix() == name{"e"} );
SYSIO_TEST_END

SYSIO_TEST_BEGIN(name_type_test_raw)
   // -----------------------------
   // constexpr operator raw()const
   CHECK_EQUAL( name{"1"}.operator name::raw(), static_cast<name::raw>(576460752303423488ULL) )
   CHECK_EQUAL( name{"5"}.operator name::raw(), static_cast<name::raw>(2882303761517117440ULL) )
   CHECK_EQUAL( name{"a"}.operator name::raw(), static_cast<name::raw>(3458764513820540928ULL) )
   CHECK_EQUAL( name{"z"}.operator name::raw(), static_cast<name::raw>(17870283321406128128ULL) )

   CHECK_EQUAL( name{"abc"}.operator name::raw(), static_cast<name::raw>(3589368903014285312ULL) )
   CHECK_EQUAL( name{"123"}.operator name::raw(), static_cast<name::raw>(614178399182651392ULL) )

   CHECK_EQUAL( name{".abc"}.operator name::raw(), static_cast<name::raw>(112167778219196416ULL) )
   CHECK_EQUAL( name{".........abc"}.operator name::raw(), static_cast<name::raw>(102016ULL) )
   CHECK_EQUAL( name{"123."}.operator name::raw(), static_cast<name::raw>(614178399182651392ULL) )
   CHECK_EQUAL( name{"123........."}.operator name::raw(), static_cast<name::raw>(614178399182651392ULL) )
   CHECK_EQUAL( name{".a.b.c.1.2.3."}.operator name::raw(), static_cast<name::raw>(108209673814966320ULL) )

   CHECK_EQUAL( name{"abc.123"}.operator name::raw(), static_cast<name::raw>(3589369488740450304ULL) )
   CHECK_EQUAL( name{"123.abc"}.operator name::raw(), static_cast<name::raw>(614181822271586304ULL) )

   CHECK_EQUAL( name{"12345abcdefgj"}.operator name::raw(), static_cast<name::raw>(614251623682315983ULL) )
   CHECK_EQUAL( name{"hijklmnopqrsj"}.operator name::raw(), static_cast<name::raw>(7754926748989239183ULL) )
   CHECK_EQUAL( name{"tuvwxyz.1234j"}.operator name::raw(), static_cast<name::raw>(14895601873741973071ULL) )

   CHECK_EQUAL( name{"111111111111j"}.operator name::raw(), static_cast<name::raw>(595056260442243615ULL) )
   CHECK_EQUAL( name{"555555555555j"}.operator name::raw(), static_cast<name::raw>(2975281302211218015ULL) )
   CHECK_EQUAL( name{"aaaaaaaaaaaaj"}.operator name::raw(), static_cast<name::raw>(3570337562653461615ULL) )
   CHECK_EQUAL( name{"zzzzzzzzzzzzj"}.operator name::raw(), static_cast<name::raw>(u64max) )
SYSIO_TEST_END


SYSIO_TEST_BEGIN(name_type_test_op_bool)
   // ---------------------------------------
   // constexpr explicit operator bool()const
   // Note that I must be explicit about calling the operator because it is defined as `explicit`
   CHECK_EQUAL( name{0}.operator bool(), false )
   CHECK_EQUAL( name{1}.operator bool(), true )
   CHECK_EQUAL( !name{0}.operator bool(), true )
   CHECK_EQUAL( !name{1}.operator bool(), false )

   CHECK_EQUAL( name{""}.operator bool(), false )
   CHECK_EQUAL( name{"1"}.operator bool(), true )
   CHECK_EQUAL( !name{""}.operator bool(), true )
   CHECK_EQUAL( !name{"1"}.operator bool(), false )

   static_assert( name{0}.operator bool() == false );
SYSIO_TEST_END

SYSIO_TEST_BEGIN(name_type_test_memcmp)
   // ----------------------------------------
   // char* write_as_string(char*, char*)const
   constexpr uint8_t buffer_size{32};
   char buffer[buffer_size]{};

   string str{"1"};
   name{str}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
   name{str = "5"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
   name{str = "a"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
   name{str = "z"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )

   name{str = "abc"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
   name{str = "123"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )


   // Note:
   // Any '.' characters at the end of a name are ignored
   name{str = ".abc"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
   name{str = ".........abc"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
   name{str = "123."}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp("123", buffer, 3), 0 )
   name{str = "123........."}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp("123", buffer, 3), 0 )
   name{str = ".a.b.c.1.2.3."}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(".a.b.c.1.2.3", buffer, 12), 0 )

   name{str = "abc.123"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
   name{str = "123.abc"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )


   name{str = "12345abcdefgj"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
   name{str = "hijklmnopqrsj"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
   name{str = "tuvwxyz.1234j"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )

   name{str = "111111111111j"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
   name{str = "555555555555j"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
   name{str = "aaaaaaaaaaaaj"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
   name{str = "zzzzzzzzzzzzj"}.write_as_string( buffer, buffer + sizeof(buffer) );
   CHECK_EQUAL( memcmp(str.c_str(), buffer, strlen(str.c_str())), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(name_type_test_to_str)
   // -----------------------
   // string to_string()const
   CHECK_EQUAL( name{"1"}.to_string(), "1" )
   CHECK_EQUAL( name{"5"}.to_string(), "5" )
   CHECK_EQUAL( name{"a"}.to_string(), "a" )
   CHECK_EQUAL( name{"z"}.to_string(), "z" )

   CHECK_EQUAL( name{"abc"}.to_string(), "abc" )
   CHECK_EQUAL( name{"123"}.to_string(), "123" )

   CHECK_EQUAL( name{".abc"}.to_string(), ".abc" )
   CHECK_EQUAL( name{".........abc"}.to_string(), ".........abc" )
   CHECK_EQUAL( name{"123."}.to_string(), "123" )
   CHECK_EQUAL( name{"123........."}.to_string(), "123" )
   CHECK_EQUAL( name{".a.b.c.1.2.3."}.to_string(), ".a.b.c.1.2.3" )

   CHECK_EQUAL( name{"abc.123"}.to_string(), "abc.123" )
   CHECK_EQUAL( name{"123.abc"}.to_string(), "123.abc" )

   CHECK_EQUAL( name{"12345abcdefgj"}.to_string(), "12345abcdefgj" )
   CHECK_EQUAL( name{"hijklmnopqrsj"}.to_string(), "hijklmnopqrsj" )
   CHECK_EQUAL( name{"tuvwxyz.1234j"}.to_string(), "tuvwxyz.1234j" )

   CHECK_EQUAL( name{"111111111111j"}.to_string(), "111111111111j" )
   CHECK_EQUAL( name{"555555555555j"}.to_string(), "555555555555j" )
   CHECK_EQUAL( name{"aaaaaaaaaaaaj"}.to_string(), "aaaaaaaaaaaaj" )
   CHECK_EQUAL( name{"zzzzzzzzzzzzj"}.to_string(), "zzzzzzzzzzzzj" )
SYSIO_TEST_END


SYSIO_TEST_BEGIN(name_type_test_equal)
   // ----------------------------------------------------------
   // friend constexpr bool operator==(const name&, const name&)
   CHECK_EQUAL( name{"1"} == name{"1"}, true )
   CHECK_EQUAL( name{"5"} == name{"5"}, true )
   CHECK_EQUAL( name{"a"} == name{"a"}, true )
   CHECK_EQUAL( name{"z"} == name{"z"}, true )

   CHECK_EQUAL( name{"abc"} == name{"abc"}, true )
   CHECK_EQUAL( name{"123"} == name{"123"}, true )

   CHECK_EQUAL( name{".abc"} == name{".abc"}, true )
   CHECK_EQUAL( name{".........abc"} == name{".........abc"}, true )
   CHECK_EQUAL( name{"123."} == name{"123"}, true )
   CHECK_EQUAL( name{"123........."} == name{"123"}, true )
   CHECK_EQUAL( name{".a.b.c.1.2.3."} == name{".a.b.c.1.2.3"}, true )

   CHECK_EQUAL( name{"abc.123"} == name{"abc.123"}, true )
   CHECK_EQUAL( name{"123.abc"} == name{"123.abc"}, true )

   CHECK_EQUAL( name{"12345abcdefgj"} == name{"12345abcdefgj"}, true )
   CHECK_EQUAL( name{"hijklmnopqrsj"} == name{"hijklmnopqrsj"}, true )
   CHECK_EQUAL( name{"tuvwxyz.1234j"} == name{"tuvwxyz.1234j"}, true )

   CHECK_EQUAL( name{"111111111111j"} == name{"111111111111j"}, true )
   CHECK_EQUAL( name{"555555555555j"} == name{"555555555555j"}, true )
   CHECK_EQUAL( name{"aaaaaaaaaaaaj"} == name{"aaaaaaaaaaaaj"}, true )
   CHECK_EQUAL( name{"zzzzzzzzzzzzj"} == name{"zzzzzzzzzzzzj"}, true )

   // test constexpr
   static_assert( name{"1"} == name{"1"} ); 
SYSIO_TEST_END

SYSIO_TEST_BEGIN(name_type_test_not_equal)
   // -----------------------------------------------------------
   // friend constexpr bool operator!=(const name&, const name&)
   CHECK_EQUAL( name{"1"} != name{}, true )
   CHECK_EQUAL( name{"5"} != name{}, true )
   CHECK_EQUAL( name{"a"} != name{}, true )
   CHECK_EQUAL( name{"z"} != name{}, true )

   CHECK_EQUAL( name{"abc"} != name{}, true )
   CHECK_EQUAL( name{"123"} != name{}, true )

   CHECK_EQUAL( name{".abc"} != name{}, true )
   CHECK_EQUAL( name{".........abc"} != name{}, true )
   CHECK_EQUAL( name{"123."} != name{}, true )
   CHECK_EQUAL( name{"123........."} != name{}, true )
   CHECK_EQUAL( name{".a.b.c.1.2.3."} != name{}, true )

   CHECK_EQUAL( name{"abc.123"} != name{}, true )
   CHECK_EQUAL( name{"123.abc"} != name{}, true )

   CHECK_EQUAL( name{"12345abcdefgj"} != name{}, true )
   CHECK_EQUAL( name{"hijklmnopqrsj"} != name{}, true )
   CHECK_EQUAL( name{"tuvwxyz.1234j"} != name{}, true )

   CHECK_EQUAL( name{"111111111111j"} != name{}, true )
   CHECK_EQUAL( name{"555555555555j"} != name{}, true )
   CHECK_EQUAL( name{"aaaaaaaaaaaaj"} != name{}, true )
   CHECK_EQUAL( name{"zzzzzzzzzzzzj"} != name{}, true )

   // test constexpr
   static_assert( name{"1"} != name{"2"} ); 
SYSIO_TEST_END

SYSIO_TEST_BEGIN(name_type_test_less_than)
   // ---------------------------------------------------------
   // friend constexpr bool operator<(const name&, const name&)
   CHECK_EQUAL( name{} < name{"1"}, true )
   CHECK_EQUAL( name{} < name{"5"}, true )
   CHECK_EQUAL( name{} < name{"a"}, true )
   CHECK_EQUAL( name{} < name{"z"}, true )

   CHECK_EQUAL( name{} < name{"abc"}, true )
   CHECK_EQUAL( name{} < name{"123"}, true )

   CHECK_EQUAL( name{} < name{".abc"}, true )
   CHECK_EQUAL( name{} < name{".........abc"}, true )
   CHECK_EQUAL( name{} < name{"123."}, true )
   CHECK_EQUAL( name{} < name{"123........."}, true )
   CHECK_EQUAL( name{} < name{".a.b.c.1.2.3."}, true )

   CHECK_EQUAL( name{} < name{"abc.123"}, true )
   CHECK_EQUAL( name{} < name{"123.abc"}, true )

   CHECK_EQUAL( name{} < name{"12345abcdefgj"}, true )
   CHECK_EQUAL( name{} < name{"hijklmnopqrsj"}, true )
   CHECK_EQUAL( name{} < name{"tuvwxyz.1234j"}, true )

   CHECK_EQUAL( name{} < name{"111111111111j"}, true )
   CHECK_EQUAL( name{} < name{"555555555555j"}, true )
   CHECK_EQUAL( name{} < name{"aaaaaaaaaaaaj"}, true )
   CHECK_EQUAL( name{} < name{"zzzzzzzzzzzzj"}, true )

   // test constexpr
   static_assert( name{} < name{"1"} ); 
SYSIO_TEST_END


SYSIO_TEST_BEGIN(name_type_test_op_n)
   // ------------------------------------
   // inline constexpr name operator""_n()
   CHECK_EQUAL( name{}, ""_n )

   CHECK_EQUAL( name{"1"}, "1"_n )
   CHECK_EQUAL( name{"5"}, "5"_n )
   CHECK_EQUAL( name{"a"}, "a"_n )
   CHECK_EQUAL( name{"z"}, "z"_n )

   CHECK_EQUAL( name{"abc"}, "abc"_n )
   CHECK_EQUAL( name{"123"}, "123"_n )

   CHECK_EQUAL( name{".abc"}, ".abc"_n )
   CHECK_EQUAL( name{".........abc"}, ".........abc"_n )
   CHECK_EQUAL( name{"123."}, "123."_n )
   CHECK_EQUAL( name{"123........."}, "123........."_n )
   CHECK_EQUAL( name{".a.b.c.1.2.3."}, ".a.b.c.1.2.3."_n )

   CHECK_EQUAL( name{"abc.123"}, "abc.123"_n )
   CHECK_EQUAL( name{"123.abc"}, "123.abc"_n )

   CHECK_EQUAL( name{"12345abcdefgj"}, "12345abcdefgj"_n )
   CHECK_EQUAL( name{"hijklmnopqrsj"}, "hijklmnopqrsj"_n )
   CHECK_EQUAL( name{"tuvwxyz.1234j"}, "tuvwxyz.1234j"_n )

   CHECK_EQUAL( name{"111111111111j"}, "111111111111j"_n )
   CHECK_EQUAL( name{"555555555555j"}, "555555555555j"_n )
   CHECK_EQUAL( name{"aaaaaaaaaaaaj"}, "aaaaaaaaaaaaj"_n )
   CHECK_EQUAL( name{"zzzzzzzzzzzzj"}, "zzzzzzzzzzzzj"_n )

   // test constexpr
   static_assert( name{"1"} == "1"_n ); 
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if( argc >= 2 && std::strcmp( argv[1], "-v" ) == 0 ) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(name_type_test_ctr_num)
   SYSIO_TEST(name_type_test_ctr_str_lit)
   SYSIO_TEST(name_type_test_str_not_allowed)
   SYSIO_TEST(name_type_test_char_to_value)
   SYSIO_TEST(name_type_test_allowed_chars)
   SYSIO_TEST(name_type_test_str_len)
   SYSIO_TEST(name_type_test_suffix)
   SYSIO_TEST(name_type_test_prefix)
   SYSIO_TEST(name_type_test_raw)
   SYSIO_TEST(name_type_test_op_bool)
   SYSIO_TEST(name_type_test_memcmp)
   SYSIO_TEST(name_type_test_to_str)
   SYSIO_TEST(name_type_test_equal)
   SYSIO_TEST(name_type_test_not_equal)
   SYSIO_TEST(name_type_test_less_than)
   SYSIO_TEST(name_type_test_op_n)

   return has_failed();
}
