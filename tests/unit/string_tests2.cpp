/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 */

#include <sysio/tester.hpp>
#include <sysio/datastream.hpp>
#include <sysio/string.hpp>

using std::fill;
using std::move;

using sysio::datastream;
using sysio::string;

// Definitions found in `sysio.cdt/libraries/sysiolib/core/sysio/string.hpp`

SYSIO_TEST_BEGIN(string_test_ins_null)
   string eostr{"abcdefg"};
   const char* null_man{nullptr};
   CHECK_ASSERT( "sysio::string::insert", [&]() {eostr.insert(0, null_man, 1);} )
   CHECK_ASSERT( "sysio::string::insert", [&]() {eostr.insert(-1, "ooo", 1);} )
SYSIO_TEST_END

//// string& insert(const size_t pos, const string& str)
SYSIO_TEST_BEGIN(string_test_ins_to_blank)
   string eostr{};
   const string str{"ooo"};
   eostr.insert(0, str);
   CHECK_EQUAL( eostr.size(), 3 )
   CHECK_EQUAL( eostr.capacity(), 6 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "ooo"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_at_bgn_single)
   string eostr{"abc"};
   const string str{"d"};
   eostr.insert(0, str);
   CHECK_EQUAL( eostr.size(), 4 )
   CHECK_EQUAL( eostr.capacity(), 8 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "dabc"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_at_bgn_mul_1)
   string eostr{"abc"};
   const string str{"def"};
   eostr.insert(0, str);
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( eostr.capacity(), 12 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "defabc"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_at_bgn_mul_2)
   string eostr{"iii"};
   const string str{"ooo"};
   eostr.insert(0, str);
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( eostr.capacity(), 12 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "oooiii") , 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_in_middle_1)
   string eostr{"iii"};
   const string str{"ooo"};
   eostr.insert(1, str);
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( eostr.capacity(), 12 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "ioooii") , 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_in_middle_2)
   string eostr{"iii"};
   const string str{"ooo"};
   eostr.insert(2, str);
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( eostr.capacity(), 12 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "iioooi") , 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_at_end)
   string eostr{"iii"};
   const string str{"ooo"};
   eostr.insert(3, str);
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( eostr.capacity(), 12 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "iiiooo") , 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_neg_index_1)
   string eostr{"abcdefg"};
   const string str{"ooo"};
   CHECK_ASSERT( "sysio::string::insert", [&]() {eostr.insert(-1, str);} )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_op_pl_1)
   string eostr{""};
   string str{""};
   str += "ooo";
   eostr.insert(0, str);
   CHECK_EQUAL( eostr.size(), 3 )
   CHECK_EQUAL( eostr.capacity(), 6 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "ooo"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_op_pl_2)
   string eostr{""};
   eostr += "abc";
   string str{""};
   str += "d";
   eostr.insert(0, str);
   CHECK_EQUAL( eostr.size(), 4 )
   CHECK_EQUAL( eostr.capacity(), 6 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "dabc"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_op_pl_3)
   string eostr{""};
   eostr += "abc";
   string str{""};
   str += "def";
   eostr.insert(0, str);
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( eostr.capacity(), 6 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "defabc"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_op_pl_4)
   string eostr{""};
   eostr += "iii";
   string str{""};
   str += "ooo";
   eostr.insert(0, str);
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( eostr.capacity(), 6 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "oooiii") , 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_op_pl_5)
   string eostr{""};
   eostr += "iii";
   string str{""};
   str += "ooo";
   eostr.insert(1, str);
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( eostr.capacity(), 6 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "ioooii") , 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_op_pl_6)
   string eostr{""};
   eostr += "iii";
   string str{""};
   str += "ooo";
   eostr.insert(2, str);
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( eostr.capacity(), 6 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "iioooi") , 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_op_pl_7)
   string eostr{""};
   eostr += "iii";
   string str{""};
   str += "ooo";
   eostr.insert(3, str);
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( eostr.capacity(), 6 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "iiiooo") , 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_neg_index_2)
   string eostr{"abcdefg"};
   string str{"ooo"};
   CHECK_ASSERT( "sysio::string::insert", [&]() {eostr.insert(-1, str);} )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_ins_capacity)
   string eostr = "hello";
   eostr.insert(0, "0", 1); /// `_capacity` is now 12; `_begin` now holds `std::unique_ptr<char[]>`
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( eostr.capacity(), 12 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "0hello") , 0 )

   eostr.insert(0, "h", 1);
   CHECK_EQUAL( eostr.size(), 7 )
   CHECK_EQUAL( eostr.capacity(), 12 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "h0hello") , 0 )
SYSIO_TEST_END

//// string& erase(size_t pos = 0, size_t len = npos)
SYSIO_TEST_BEGIN(string_test_erase)
   string eostr{"abcdefgh"};
   eostr.erase();
   CHECK_EQUAL( eostr.size(), 0 )
   CHECK_EQUAL( strcmp(eostr.c_str(), ""), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_at_zero)
   string eostr{"abcdefgh"};
   eostr.erase(0);
   CHECK_EQUAL( eostr.size(), 0 )
   CHECK_EQUAL( strcmp(eostr.c_str(), ""), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_to_npos)
   string eostr{"abcdefgh"};
   eostr.erase(0, string::npos);
   CHECK_EQUAL( eostr.size(), 0 )
   CHECK_EQUAL( strcmp(eostr.c_str(), ""), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_1)
   string eostr{"abcdefgh"};
   eostr.erase(1, string::npos);
   CHECK_EQUAL( eostr.size(), 1 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "a"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_2)
   string eostr{"abcdefgh"};
   eostr.erase(2, string::npos);
   CHECK_EQUAL( eostr.size(), 2 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "ab"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_3)
   string eostr{"abcdefgh"};
   eostr.erase(3, string::npos);
   CHECK_EQUAL( eostr.size(), 3 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abc"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_4)
   string eostr{"abcdefgh"};
   eostr.erase(4, string::npos);
   CHECK_EQUAL( eostr.size(), 4 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcd"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_5)
   string eostr{"abcdefgh"};
   eostr.erase(5, string::npos);
   CHECK_EQUAL( eostr.size(), 5 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcde"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_6)
   string eostr{"abcdefgh"};
   eostr.erase(6, string::npos);
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcdef"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_7)
   string eostr{"abcdefgh"};
   eostr.erase(7, string::npos);
   CHECK_EQUAL( eostr.size(), 7 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcdefg"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_8)
   string eostr{"abcdefgh"};
   eostr.erase(8, string::npos);
   CHECK_EQUAL( eostr.size(), 8 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcdefgh"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_8_len_0)
   string eostr{"abcdefgh"};
   eostr.erase(8, 0);
   CHECK_EQUAL( eostr.size(), 8 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcdefgh"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_neg_index_1)
   string eostr{"abcdefg"};
   CHECK_ASSERT( "sysio::string::erase", [&]() {eostr.erase(-1, 1);} )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_op_pl)
   string eostr{""};
   eostr += "abcdefgh";

   eostr.erase();
   CHECK_EQUAL( eostr.size(), 0 )
   CHECK_EQUAL( strcmp(eostr.c_str(), ""), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_at_0_op_pl)
   string eostr{""};
   eostr += "abcdefgh";

   eostr.erase(0);
   CHECK_EQUAL( eostr.size(), 0 )
   CHECK_EQUAL( strcmp(eostr.c_str(), ""), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_at_0_op_pl_npos)
   string eostr{""};
   eostr += "abcdefgh";

   eostr.erase(0, string::npos);
   CHECK_EQUAL( eostr.size(), 0 )
   CHECK_EQUAL( strcmp(eostr.c_str(), ""), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_1_op_pl)
   string eostr{""};
   eostr += "abcdefgh";

   eostr.erase(1, string::npos);
   CHECK_EQUAL( eostr.size(), 1 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "a"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_2_op_pl)
   string eostr{""};
   eostr += "abcdefgh";

   eostr.erase(2, string::npos);
   CHECK_EQUAL( eostr.size(), 2 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "ab"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_3_op_pl)
   string eostr{""};
   eostr += "abcdefgh";

   eostr.erase(3, string::npos);
   CHECK_EQUAL( eostr.size(), 3 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abc"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_4_op_pl)
   string eostr{""};
   eostr += "abcdefgh";

   eostr.erase(4, string::npos);
   CHECK_EQUAL( eostr.size(), 4 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcd"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_5_op_pl)
   string eostr{""};
   eostr += "abcdefgh";

   eostr.erase(5, string::npos);
   CHECK_EQUAL( eostr.size(), 5 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcde"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_6_op_pl)
   string eostr{""};
   eostr += "abcdefgh";

   eostr.erase(6, string::npos);
   CHECK_EQUAL( eostr.size(), 6 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcdef"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_7_op_pl)
   string eostr{""};
   eostr += "abcdefgh";

   eostr.erase(7, string::npos);
   CHECK_EQUAL( eostr.size(), 7 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcdefg"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_8_op_pl)
   string eostr{""};
   eostr += "abcdefgh";

   eostr.erase(8, string::npos);
   CHECK_EQUAL( eostr.size(), 8 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcdefgh"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_at_8_op_pl)
   string eostr{""};
   eostr += "abcdefgh";

   eostr.erase(8, 0);
   CHECK_EQUAL( eostr.size(), 8 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcdefgh"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_erase_neg_index_2)
   string eostr{"abcdefg"};
   CHECK_ASSERT( "sysio::string::erase", [&]() {eostr.erase(-1, 1);} )
SYSIO_TEST_END

//// string& append(const char* str)
SYSIO_TEST_BEGIN(string_test_append_to_blank_1)
   string eostr{};
   const char* str{"iii"};
   eostr.append(str);
   CHECK_EQUAL( eostr.size(), 3 )
   CHECK_EQUAL( eostr.capacity(), 6 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "iii"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_append_1)
   string eostr{"abcdefg"};
   const char* str{"iii"};
   eostr.append(str);
   CHECK_EQUAL( eostr.size(), 10 )
   CHECK_EQUAL( eostr.capacity(), 20 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcdefgiii"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_append_null)
   string eostr{"abcdefg"};
   const char* null_man{nullptr};
   CHECK_ASSERT( "sysio::string::append", [&]() {eostr.append(null_man);} )
SYSIO_TEST_END

//// string& append(const string& str)
SYSIO_TEST_BEGIN(string_test_append_to_blank_2)
   string eostr{};
   const string str{"iii"};
   eostr.append(str);
   CHECK_EQUAL( eostr.size(), 3 )
   CHECK_EQUAL( eostr.capacity(), 6 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "iii"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_append_2)
   string eostr{"abcdefg"};
   const string str{"iii"};
   eostr.append(str);
   CHECK_EQUAL( eostr.size(), 10 )
   CHECK_EQUAL( eostr.capacity(), 20 )
   CHECK_EQUAL( strcmp(eostr.c_str(), "abcdefgiii"), 0 )
SYSIO_TEST_END

//// string& operator+=(const char c)
SYSIO_TEST_BEGIN(string_test_append_3)
   string eostr0{};
   string eostr1{"a"};
   string eostr2{"abcdef"};

   eostr0 += 'c';
   CHECK_EQUAL( eostr0.size(), 1 )
   CHECK_EQUAL( eostr0.capacity(), 2 )
   CHECK_EQUAL( strcmp(eostr0.c_str(), "c"), 0 )

   eostr1 += 'c';
   eostr1 += 'c';
   CHECK_EQUAL( eostr1.size(), 3 )
   CHECK_EQUAL( eostr1.capacity(), 4 )
   CHECK_EQUAL( strcmp(eostr1.c_str(), "acc"), 0 )

   eostr2 += 'c';
   CHECK_EQUAL( eostr2.size(), 7 )
   CHECK_EQUAL( eostr2.capacity(), 14 )
   CHECK_EQUAL( strcmp(eostr2.c_str(), "abcdefc"), 0 )
SYSIO_TEST_END

//// string& operator+=(const char* rhs)
SYSIO_TEST_BEGIN(string_test_append_op_pl_1)
   string eostr0{};
   string eostr1{"a"};
   string eostr2{"abcdef"};
   string eostr3{"abcdef"};

   eostr0 += "c";
   CHECK_EQUAL( eostr0.size(), 1 )
   CHECK_EQUAL( eostr0.capacity(), 2 )
   CHECK_EQUAL( strcmp(eostr0.c_str(), "c"), 0 )

   eostr1 += "c";
   eostr1 += "c";
   CHECK_EQUAL( eostr1.size(), 3 )
   CHECK_EQUAL( eostr1.capacity(), 4 )
   CHECK_EQUAL( strcmp(eostr1.c_str(), "acc"), 0 )

   eostr2 += "c";
   CHECK_EQUAL( eostr2.size(), 7 )
   CHECK_EQUAL( eostr2.capacity(), 14 )
   CHECK_EQUAL( strcmp(eostr2.c_str(), "abcdefc"), 0 )

   eostr3 += "ghijklm";
   CHECK_EQUAL( eostr3.size(), 13 )
   CHECK_EQUAL( eostr3.capacity(), 26 )
   CHECK_EQUAL( strcmp(eostr3.c_str(), "abcdefghijklm"), 0 )
SYSIO_TEST_END

//// string& operator+=(const string& rhs)
SYSIO_TEST_BEGIN(string_test_append_op_pl_2)
   string eostr0{};
   string eostr1{"a"};
   string eostr2{"abcdef"};
   string eostr3{"abcdef"};

   eostr0 += string{"c"};
   CHECK_EQUAL( eostr0.size(), 1 )
   CHECK_EQUAL( eostr0.capacity(), 2 )
   CHECK_EQUAL( strcmp(eostr0.c_str(), "c"), 0 )

   eostr1 += string{"c"};
   eostr1 += string{"c"};
   CHECK_EQUAL( eostr1.size(), 3 )
   CHECK_EQUAL( eostr1.capacity(), 4 )
   CHECK_EQUAL( strcmp(eostr1.c_str(), "acc"), 0 )

   eostr2 += string{"c"};
   CHECK_EQUAL( eostr2.size(), 7 )
   CHECK_EQUAL( eostr2.capacity(), 14 )
   CHECK_EQUAL( strcmp(eostr2.c_str(), "abcdefc"), 0 )

   eostr3 += string{"ghijklm"};
   CHECK_EQUAL( eostr3.size(), 13 )
   CHECK_EQUAL( eostr3.capacity(), 26 )
   CHECK_EQUAL( strcmp(eostr3.c_str(), "abcdefghijklm"), 0 )
SYSIO_TEST_END

//// string& operator+=(const string& s)
SYSIO_TEST_BEGIN(string_test_append_op_pl_3)
   string eostr0{"a"};
   string eostr1{"b"};
   CHECK_EQUAL( eostr0.size(), 1 )
   eostr0 += eostr1;
   CHECK_EQUAL( eostr0.size(), 2 )
   CHECK_EQUAL( strcmp(eostr0.c_str(), "ab"), 0 )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_append_op_pl_4)
   string eostr0{"abc"};
   string eostr1{"def"};
   CHECK_EQUAL( eostr0.size(), 3 )
   eostr0 += eostr1;
   CHECK_EQUAL( eostr0.size(), 6 )
   CHECK_EQUAL( strcmp(eostr0.c_str(), "abcdef"), 0 )
SYSIO_TEST_END

//// inline void print(sysio::string str)
SYSIO_TEST_BEGIN(string_test_print)
   const string eostr0{""};
   const string eostr1{"abc"};
   const string eostr2{"abcdef"};

   CHECK_PRINT( "", [&](){ print(eostr0); } )
   CHECK_PRINT( "abc", [&](){ print(eostr1); } )
   CHECK_PRINT( "abcdef", [&](){ print(eostr2); } )
SYSIO_TEST_END

//// friend bool operator< (const string& lhs, const string& rhs)
SYSIO_TEST_BEGIN(string_test_less_than)
   const string eostr0{"abc"};
   const string eostr1{"def"};
   CHECK_EQUAL( (eostr0 < eostr0), false )
   CHECK_EQUAL( (eostr1 < eostr1), false )
   CHECK_EQUAL( (eostr0 < eostr1), true )
SYSIO_TEST_END

//// friend bool operator> (const string& lhs, const string& rhs)
SYSIO_TEST_BEGIN(string_test_gt)
   const string eostr0{"abc"};
   const string eostr1{"def"};
   CHECK_EQUAL( (eostr0 > eostr0), false )
   CHECK_EQUAL( (eostr1 > eostr1), false )
   CHECK_EQUAL( (eostr0 > eostr1), false )
SYSIO_TEST_END

//// friend bool operator<=(const string& lhs, const string& rhs)
SYSIO_TEST_BEGIN(string_test_lt_or_eq)
   const string eostr0{"abc"};
   const string eostr1{"def"};
   CHECK_EQUAL( (eostr0 <= eostr0), true )
   CHECK_EQUAL( (eostr1 <= eostr1), true )
   CHECK_EQUAL( (eostr0 <= eostr1), true )
SYSIO_TEST_END

//// friend bool operator>=(const string& lhs, const string& rhs)
SYSIO_TEST_BEGIN(string_test_gt_or_eq)
   const string eostr0{"abc"};
   const string eostr1{"def"};
   CHECK_EQUAL( (eostr0 >= eostr0), true )
   CHECK_EQUAL( (eostr1 >= eostr1), true )
   CHECK_EQUAL( (eostr0 >= eostr1), false )
SYSIO_TEST_END

//// friend bool operator==(const string& lhs, const string& rhs)
SYSIO_TEST_BEGIN(string_test_equal)
   const string eostr0{"abc"};
   const string eostr1{"def"};
   CHECK_EQUAL( (eostr0 == eostr0), true )
   CHECK_EQUAL( (eostr1 == eostr1), true )
   CHECK_EQUAL( (eostr0 == eostr1), false )
SYSIO_TEST_END

//// friend bool operator!=(const string& lhs, const string& rhs)
SYSIO_TEST_BEGIN(string_test_not_equal)
   const string eostr0{"abc"};
   const string eostr1{"def"};
   CHECK_EQUAL( (eostr0 != eostr0), false )
   CHECK_EQUAL( (eostr1 != eostr1), false )
   CHECK_EQUAL( (eostr0 != eostr1), true )
SYSIO_TEST_END

//// template<typename DataStream>
//// DataStream& operator<<(DataStream& ds, const string& str)
//// DataStream& operator>>(DataStream& ds, string& str)
SYSIO_TEST_BEGIN(string_test_stream_io_1)
   constexpr uint16_t buffer_size{256};
   char datastream_buffer[buffer_size]{}; // Buffer for the datastream to point to
   char buffer[buffer_size]; // Buffer to compare `datastream_buffer` with
   datastream<char*> ds{datastream_buffer, buffer_size};

   ds.seekp(0);
   fill(std::begin(datastream_buffer), std::end(datastream_buffer), 0);
   const string cstr {""};
   string str{};
   ds << cstr;
   ds.seekp(0);
   ds >> str;
   CHECK_EQUAL( cstr, str )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_stream_io_2)
   constexpr uint16_t buffer_size{256};
   char datastream_buffer[buffer_size]{};
   char buffer[buffer_size];
   datastream<char*> ds{datastream_buffer, buffer_size};

   ds.seekp(0);
   fill(std::begin(datastream_buffer), std::end(datastream_buffer), 0);
   const string cstr {"a"};
   string str{};
   ds << cstr;
   ds.seekp(0);
   ds >> str;
   CHECK_EQUAL( cstr, str )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(string_test_stream_io_3)
   constexpr uint16_t buffer_size{256};
   char datastream_buffer[buffer_size]{};
   char buffer[buffer_size];
   datastream<char*> ds{datastream_buffer, buffer_size};

   ds.seekp(0);
   fill(std::begin(datastream_buffer), std::end(datastream_buffer), 0);
   const string cstr {"abcdefghi"};
   string str{};
   ds << cstr;
   ds.seekp(0);
   ds >> str;
   CHECK_EQUAL( cstr, str )
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if( argc >= 2 && std::strcmp( argv[1], "-v" ) == 0 ) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(string_test_ins_null)
   SYSIO_TEST(string_test_ins_to_blank)
   SYSIO_TEST(string_test_ins_at_bgn_single)
   SYSIO_TEST(string_test_ins_at_bgn_mul_1)
   SYSIO_TEST(string_test_ins_at_bgn_mul_2)
   SYSIO_TEST(string_test_ins_in_middle_1)
   SYSIO_TEST(string_test_ins_in_middle_2)
   SYSIO_TEST(string_test_ins_at_end)
   SYSIO_TEST(string_test_ins_neg_index_1)
   SYSIO_TEST(string_test_ins_op_pl_1)
   SYSIO_TEST(string_test_ins_op_pl_2)
   SYSIO_TEST(string_test_ins_op_pl_3)
   SYSIO_TEST(string_test_ins_op_pl_4)
   SYSIO_TEST(string_test_ins_op_pl_5)
   SYSIO_TEST(string_test_ins_op_pl_6)
   SYSIO_TEST(string_test_ins_op_pl_7)
   SYSIO_TEST(string_test_ins_neg_index_2)
   SYSIO_TEST(string_test_ins_capacity)
   SYSIO_TEST(string_test_erase)
   SYSIO_TEST(string_test_erase_at_zero)
   SYSIO_TEST(string_test_erase_to_npos)
   SYSIO_TEST(string_test_erase_1)
   SYSIO_TEST(string_test_erase_2)
   SYSIO_TEST(string_test_erase_3)
   SYSIO_TEST(string_test_erase_4)
   SYSIO_TEST(string_test_erase_5)
   SYSIO_TEST(string_test_erase_6)
   SYSIO_TEST(string_test_erase_7)
   SYSIO_TEST(string_test_erase_8)
   SYSIO_TEST(string_test_erase_8_len_0)
   SYSIO_TEST(string_test_erase_neg_index_1)
   SYSIO_TEST(string_test_erase_op_pl)
   SYSIO_TEST(string_test_erase_at_0_op_pl)
   SYSIO_TEST(string_test_erase_at_0_op_pl_npos)
   SYSIO_TEST(string_test_erase_1_op_pl)
   SYSIO_TEST(string_test_erase_2_op_pl)
   SYSIO_TEST(string_test_erase_3_op_pl)
   SYSIO_TEST(string_test_erase_4_op_pl)
   SYSIO_TEST(string_test_erase_5_op_pl)
   SYSIO_TEST(string_test_erase_6_op_pl)
   SYSIO_TEST(string_test_erase_7_op_pl)
   SYSIO_TEST(string_test_erase_8_op_pl)
   SYSIO_TEST(string_test_erase_at_8_op_pl)
   SYSIO_TEST(string_test_erase_neg_index_2)
   SYSIO_TEST(string_test_append_to_blank_1)
   SYSIO_TEST(string_test_append_1)
   SYSIO_TEST(string_test_append_null)
   SYSIO_TEST(string_test_append_to_blank_2)
   SYSIO_TEST(string_test_append_2)
   SYSIO_TEST(string_test_append_3)
   SYSIO_TEST(string_test_append_op_pl_1)
   SYSIO_TEST(string_test_append_op_pl_2)
   SYSIO_TEST(string_test_append_op_pl_3)
   SYSIO_TEST(string_test_append_op_pl_4)
   SYSIO_TEST(string_test_print)
   SYSIO_TEST(string_test_less_than)
   SYSIO_TEST(string_test_gt)
   SYSIO_TEST(string_test_lt_or_eq)
   SYSIO_TEST(string_test_gt_or_eq)
   SYSIO_TEST(string_test_equal)
   SYSIO_TEST(string_test_not_equal)
   SYSIO_TEST(string_test_stream_io_1)
   SYSIO_TEST(string_test_stream_io_2)
   SYSIO_TEST(string_test_stream_io_3)

   return has_failed();
}
