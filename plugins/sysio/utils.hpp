#pragma once

#include <stdlib.h>

#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>
#include <regex>
#include <string_view>

namespace sysio { namespace cdt {

uint64_t char_to_symbol( char c ) {
   if( c >= 'a' && c <= 'z' )
      return (c - 'a') + 6;
   if( c >= '1' && c <= '5' )
      return (c - '1') + 1;
   return 0;
}

uint64_t string_to_name( const char* str )
{
   uint64_t name = 0;
   int i = 0;
   for ( ; str[i] && i < 12; ++i) {
        name |= (char_to_symbol(str[i]) & 0x1f) << (64 - 5 * (i + 1));
    }

   if (i == 12)
       name |= char_to_symbol(str[12]) & 0x0F;
   return name;
}

template <typename Lambda>
void validate_name( const std::string& str, Lambda&& error_handler ) {
   const auto len = str.length();
   if ( len > 13 ) {
      return error_handler(std::string("Name {")+str+"} is more than 13 characters long");
   }
   uint64_t value = string_to_name( str.c_str() );

   static const char* charmap = ".12345abcdefghijklmnopqrstuvwxyz";
   std::string str2(13,'.');

   uint64_t tmp = value;
   for( uint32_t i = 0; i <= 12; ++i ) {
      char c = charmap[tmp & (i == 0 ? 0x0f : 0x1f)];
      str2[12-i] = c;
      tmp >>= (i == 0 ? 4 : 5);
   }

   auto trim = [](std::string& s) {
      int i;
      for (i = s.length()-1; i >= 0; i--)
         if (s[i] != '.')
            break;
      s = s.substr(0,i+1);
   };
   trim(str2);

   if ( str2 != str ) {
      return error_handler("name not properly normalized");
   }
}

std::string name_to_string( uint64_t nm ) {
   static const char* charmap = ".12345abcdefghijklmnopqrstuvwxyz";
   std::string str(13,'.');

   uint64_t tmp = nm;
   for( uint32_t i = 0; i <= 12; ++i ) {
      char c = charmap[tmp & (i == 0 ? 0x0f : 0x1f)];
      str[12-i] = c;
      tmp >>= (i == 0 ? 4 : 5);
   }

   auto trim_right_dots = [](std::string& str) {
      const auto last = str.find_last_not_of('.');
      if (last != std::string::npos)
         str = str.substr(0, last+1);
   };
   trim_right_dots( str );
   return str;
}

bool starts_with(const std::string& str, std::string_view prefix) {
   return strncmp(str.c_str(), prefix.data(), prefix.size()) == 0;
}

}} // ns sysio::cdt
