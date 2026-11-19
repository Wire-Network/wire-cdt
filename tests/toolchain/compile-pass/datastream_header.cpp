/// WIRE-277: merely including datastream must not instantiate unsupported
/// std::char_traits<uint8_t> in a host standard library such as libc++ 23.
#include <sysio/datastream.hpp>

/// Instantiate ordinary string serialization in both WASM and native builds.
void datastream_header_roundtrip(char* buffer, size_t size, std::string& value) {
   sysio::datastream<char*> stream(buffer, size);
   stream << value;
   stream.seekp(0);
   stream >> value;
}
