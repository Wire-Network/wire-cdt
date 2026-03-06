#pragma once

#include <utility>
#include <type_traits>
#include <span>
#include <zpp_bits.h>
#include <sysio/datastream.hpp>

namespace sysio {

   /// Wrapper for protobuf message types used as action parameters.
   /// When the ABI generator sees `sysio::pb<T>`, it encodes the type as
   /// `protobuf::fully.qualified.Name` instead of a normal struct, and the
   /// action data is serialized/deserialized using protobuf wire format
   /// (via zpp_bits) rather than the standard datastream packing.
   template <typename T>
   struct pb : T {
      using pb_message_type = T;
      // Inherit the protobuf serialize protocol from T
      using serialize = typename T::serialize;
      serialize use();

      pb() = default;
      pb(const T& v) : T(v) {}
      pb(T&& v) : T(std::move(v)) {}
      pb(const pb<T>&) = default;
      pb(pb<T>&&) = default;

      pb<T>& operator=(const pb<T>&) = default;
      pb<T>& operator=(pb<T>&&) = default;

      template <typename... Args>
      pb(Args&&... args) : T(std::forward<Args>(args)...) {}

      bool operator==(const pb<T>&) const = default;
   };

   template <typename T>
   const pb<T>& to_pb(const T& v) {
      return static_cast<const pb<T>&>(v);
   }

   template <typename T>
   T& from_pb(pb<T>& v) {
      return v;
   }

   template <typename T>
   const T& from_pb(const pb<T>& v) {
      return v;
   }

   template <typename T>
   T& from_pb(std::tuple<pb<T>>& v) {
      return std::get<0>(v);
   }

   template <typename T>
   const T& from_pb(const std::tuple<pb<T>>& v) {
      return std::get<0>(v);
   }

   // --- Protobuf serialization via zpp_bits ---

   /// Deserialize a pb<T> from a datastream using protobuf wire format.
   /// The stream must contain a varuint32 length prefix followed by the
   /// protobuf-encoded message bytes.
   template<typename Stream, typename T>
   datastream<Stream>& operator>>(datastream<Stream>& ds, pb<T>& v) {
      unsigned_int len;
      ds >> len;
      sysio::check(ds.remaining() >= len.value, "pb: not enough data for protobuf message");
      zpp::bits::in in(std::span(ds.pos(), len.value), zpp::bits::size_varint{});
      auto result = in(from_pb(v));
      sysio::check(result == std::errc{}, "protobuf deserialization failure");
      ds.skip(len.value);
      return ds;
   }

   /// Serialize a pb<T> to a datastream using protobuf wire format.
   /// Writes a varuint32 length prefix followed by the protobuf-encoded bytes.
   template<typename Stream, typename T>
   datastream<Stream>& operator<<(datastream<Stream>& ds, const pb<T>& v) {
      std::vector<char> result;
      zpp::bits::out out(result, zpp::bits::size_varint{});
      auto status = out(from_pb(v));
      sysio::check(status == std::errc{}, "protobuf serialization failure");
      ds << unsigned_int(result.size());
      ds.write(result.data(), result.size());
      return ds;
   }

   // --- zpp::bits varint JSON/binary serialization helpers ---

   template<typename T, zpp::bits::varint_encoding Encoding>
   void from_json(zpp::bits::varint<T, Encoding>& obj, auto& stream) {
      T val;
      from_json(val, stream);
      obj = zpp::bits::varint<T, Encoding>(val);
   }

   template<typename T, zpp::bits::varint_encoding Encoding>
   void to_json(zpp::bits::varint<T, Encoding> obj, auto& stream) {
      to_json(static_cast<T>(obj), stream);
   }

} // namespace sysio
