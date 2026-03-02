#pragma once
#include <algorithm>
#include <string>
#include <regex>
#include <vector>

namespace sysio_plugin {
   std::string remove_quotes(std::string str) {
      str.erase(std::remove(str.begin(), str.end(), '\"'), str.end());
      return str;
   }

   std::vector<std::string> tokenize(std::string str, std::string delim = R"([\s,]+)") {
      auto re = std::regex(delim);
      return std::vector<std::string>{
         std::sregex_token_iterator(str.begin(), str.end(), re, -1),
         std::sregex_token_iterator()
      };
   }
}
