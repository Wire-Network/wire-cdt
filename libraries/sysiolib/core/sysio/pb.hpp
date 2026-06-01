#pragma once

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <memory>
#include <tuple>
#include <utility>
#include <type_traits>
#include <span>
#include <zpp_bits.h>
#include <sysio/datastream.hpp>

namespace sysio {

   /// Protobuf int32 field storage with protobuf-compatible varint encoding.
   /// Negative int32 values are stored in 32 bits but encoded through a signed
   /// 64-bit varint, which matches protobuf's sign-extended int32 wire format.
   struct pb_int32 {
      int32_t value = 0;

      constexpr pb_int32() = default;
      constexpr pb_int32(int32_t v) : value(v) {}

      constexpr operator int32_t&() & { return value; }
      constexpr operator int32_t() const { return value; }

      constexpr pb_int32& operator=(int32_t v) {
         value = v;
         return *this;
      }

      friend bool operator==(const pb_int32&, const pb_int32&) = default;
      friend auto operator<=>(const pb_int32&, const pb_int32&) = default;
   };

   template <typename Archive>
   constexpr auto serialize(Archive& archive, pb_int32 self) requires(Archive::kind() == zpp::bits::kind::out) {
      return archive(zpp::bits::vint64_t{static_cast<int64_t>(self.value)});
   }

   template <typename Archive>
   constexpr auto serialize(Archive& archive, pb_int32& self) requires(Archive::kind() == zpp::bits::kind::in) {
      zpp::bits::vint64_t wire_value;
      if (auto result = archive(wire_value); result != std::errc{}) {
         return result;
      }
      self.value = static_cast<int32_t>(static_cast<int64_t>(wire_value));
      return zpp::bits::errc{};
   }

   template <typename T>
   inline constexpr bool is_pb_int32_v = std::same_as<std::remove_cvref_t<T>, pb_int32>;

   template <typename>
   inline constexpr bool dependent_false_v = false;

   /// zpp_bits' protobuf protocol does not accept class field types unless they
   /// are messages, so `pb_protocol` mirrors the upstream protocol at the field
   /// dispatch boundary and injects protobuf int32 handling for `sysio::pb_int32`.
   /// Non-int32 field categories delegate to `zpp::bits::pb` where that protocol
   /// can represent them directly.
   template <typename... Options>
   struct pb_protocol : zpp::bits::pb<Options...> {
      using base = zpp::bits::pb<Options...>;
      using pb_default = pb_protocol<>;

      constexpr pb_protocol(Options&&... options) : base(std::forward<Options>(options)...) {}

      template <typename Type>
      constexpr static auto check_type() {
         using type = std::remove_cvref_t<Type>;
         if constexpr (is_pb_int32_v<type>) {
            return true;
         } else if constexpr (base::template is_pb_field<type>()) {
            return check_type<typename type::pb_field_type>();
         } else if constexpr (!std::is_class_v<type> || zpp::bits::concepts::varint<type> ||
                              zpp::bits::concepts::empty<type>) {
            return true;
         } else if constexpr (zpp::bits::concepts::associative_container<type> &&
                              requires { typename type::mapped_type; }) {
            static_assert(requires { type{}.push_back(typename type::value_type{}); } ||
                          requires { type{}.insert(typename type::value_type{}); });
            static_assert(check_type<typename type::key_type>());
            static_assert(check_type<typename type::mapped_type>());
            return true;
         } else if constexpr (zpp::bits::concepts::container<type>) {
            static_assert(requires { type{}.push_back(typename type::value_type{}); } ||
                          requires { type{}.insert(typename type::value_type{}); });
            static_assert(check_type<typename type::value_type>());
            return true;
         } else if constexpr (zpp::bits::concepts::by_protocol<type>) {
            return true;
         } else {
            static_assert(dependent_false_v<Type>, "pb_protocol: unsupported protobuf field type");
         }
      }

      ZPP_BITS_INLINE constexpr auto operator()(auto& archive, auto& item) const
         requires(std::remove_cvref_t<decltype(archive)>::kind() == zpp::bits::kind::out)
      {
         using type = std::remove_cvref_t<decltype(item)>;
         static_assert(check_type<type>());

         return zpp::bits::visit_members(item, [&](auto&&... items) {
            static_assert((... && check_type<decltype(items)>()));
            return serialize_many(std::make_index_sequence<sizeof...(items)>{}, archive, items...);
         });
      }

      ZPP_BITS_INLINE constexpr auto operator()(auto& archive,
                                                auto& item,
                                                std::size_t size = std::numeric_limits<std::size_t>::max()) const
         requires(std::remove_cvref_t<decltype(archive)>::kind() == zpp::bits::kind::in)
      {
         auto data = archive.remaining_data();
         zpp::bits::in in{std::span{data.data(), std::min(size, data.size())},
                          zpp::bits::size_varint{},
                          zpp::bits::endian::little{},
                          zpp::bits::alloc_limit<std::remove_cvref_t<decltype(archive)>::allocation_limit>{}};
         auto result = deserialize_fields(in, item);
         archive.position() += in.position();
         return result;
      }

      template <std::size_t FirstIndex, std::size_t... Indices>
      ZPP_BITS_INLINE constexpr static zpp::bits::errc serialize_many(std::index_sequence<FirstIndex, Indices...>,
                                                                      auto& archive,
                                                                      auto& first_item,
                                                                      auto&... items) {
         if (auto result = serialize_one<FirstIndex>(archive, first_item); zpp::bits::failure(result)) {
            return result;
         }

         return serialize_many(std::index_sequence<Indices...>{}, archive, items...);
      }

      ZPP_BITS_INLINE constexpr static zpp::bits::errc serialize_many(std::index_sequence<>, auto&) {
         return {};
      }

      template <std::size_t Index, typename TagType = void>
      ZPP_BITS_INLINE constexpr static zpp::bits::errc serialize_one(auto& archive, auto& item) {
         using type = std::remove_cvref_t<decltype(item)>;
         using tag_type = std::conditional_t<std::is_void_v<TagType>, type, TagType>;

         if constexpr (zpp::bits::concepts::empty<type>) {
            return {};
         } else if constexpr (is_pb_int32_v<type>) {
            constexpr auto tag = base::template make_tag<zpp::bits::vint64_t, Index>();
            return archive(tag, zpp::bits::vint64_t{static_cast<int64_t>(item)});
         } else if constexpr (base::template is_pb_field<type>()) {
            return serialize_one<Index, tag_type>(archive, static_cast<const typename type::pb_field_type&>(item));
         } else if constexpr (std::is_enum_v<type> && !std::same_as<type, std::byte>) {
            constexpr auto tag = base::template make_tag<tag_type, Index>();
            return archive(tag, zpp::bits::varint{std::underlying_type_t<type>(item)});
         } else if constexpr (!zpp::bits::concepts::container<type>) {
            constexpr auto tag = base::template make_tag<tag_type, Index>();
            return archive(tag, item);
         } else if constexpr (zpp::bits::concepts::associative_container<type> &&
                              requires { typename type::mapped_type; }) {
            constexpr auto tag = base::template make_tag<tag_type, Index>();

            using key_type = std::conditional_t<
               std::is_enum_v<typename type::key_type> && !std::same_as<typename type::key_type, std::byte>,
               zpp::bits::varint<typename type::key_type>,
               typename type::key_type>;

            using mapped_type = std::conditional_t<
               std::is_enum_v<typename type::mapped_type> && !std::same_as<typename type::mapped_type, std::byte>,
               zpp::bits::varint<typename type::mapped_type>,
               typename type::mapped_type>;

            struct value_type {
               const key_type& key;
               const mapped_type& value;

               using serialize = zpp::bits::protocol<pb_protocol{}>;
               serialize use();
            };

            for (auto& [key, value] : item) {
               if (auto result = archive(tag, value_type{.key = key, .value = value}); zpp::bits::failure(result)) {
                  return result;
               }
            }

            return {};
         } else if constexpr (requires { requires is_pb_int32_v<typename type::value_type>; }) {
            constexpr auto tag =
               base::template make_tag<base::wire_type::length_delimited, zpp::bits::vint64_t, Index>();
            std::size_t size = {};
            for (auto& element : item) {
               size += zpp::bits::varint_size(static_cast<int64_t>(element));
            }
            if (!size) {
               return {};
            }
            // Keep this in lockstep with serialize(Archive&, pb_int32): the
            // packed length is precomputed from the same vint64_t bytes that
            // unsized(item) writes for each pb_int32 element.
            return archive(tag, zpp::bits::varint{size}, zpp::bits::unsized(item));
         } else {
            return base::template serialize_one<Index, TagType>(archive, item);
         }
      }

      ZPP_BITS_INLINE constexpr static zpp::bits::errc deserialize_fields(auto& archive, auto& item) {
         using type = std::remove_cvref_t<decltype(item)>;
         static_assert(check_type<type>());

         auto size = archive.data().size();
         zpp::bits::visit_members(item, [](auto&&... members) {
            (([](auto&& member) {
                using member_type = std::remove_cvref_t<decltype(member)>;
                if constexpr (zpp::bits::concepts::container<member_type> &&
                              !std::is_fundamental_v<member_type> &&
                              !std::same_as<member_type, std::byte> &&
                              requires { member.clear(); }) {
                   member.clear();
                }
             }(members)),
             ...);
         });

         while (archive.position() < size) {
            zpp::bits::vuint32_t tag;
            if (auto result = archive(tag); zpp::bits::failure(result)) {
               return result;
            }

            if (auto result = deserialize_field(archive, item, base::tag_number(tag), base::tag_type(tag));
                zpp::bits::failure(result)) {
               return result;
            }
         }

         return {};
      }

      template <std::size_t Index = 0>
      ZPP_BITS_INLINE constexpr static auto deserialize_field(auto& archive,
                                                             auto&& item,
                                                             auto field_num,
                                                             typename base::wire_type field_type) {
         using type = std::remove_reference_t<decltype(item)>;
         if constexpr (Index >= zpp::bits::number_of_members<type>()) {
            if (!field_num) {
               return zpp::bits::errc{std::errc::protocol_error};
            }
            return zpp::bits::errc{};
         } else if (base::template field_number_from_struct<type, Index>() != field_num) {
            return deserialize_field<Index + 1>(archive, item, field_num, field_type);
         } else {
            return zpp::bits::visit_members(item, [&](auto&&... items) {
               std::tuple<decltype(items)&...> refs = {items...};
               auto& field = std::get<Index>(refs);
               using field_type_t = std::remove_reference_t<decltype(field)>;
               static_assert(check_type<field_type_t>());

               return deserialize_field(archive, field_type, field);
            });
         }
      }

      ZPP_BITS_INLINE constexpr static auto deserialize_field(auto& archive,
                                                             typename base::wire_type field_type,
                                                             auto& item) {
         using type = std::remove_reference_t<decltype(item)>;
         static_assert(check_type<type>());

         if constexpr (is_pb_int32_v<type>) {
            zpp::bits::vint64_t value;
            if (auto result = archive(value); zpp::bits::failure(result)) {
               return result;
            }
            item = static_cast<int32_t>(static_cast<int64_t>(value));
            return zpp::bits::errc{};
         } else if constexpr (std::is_enum_v<type>) {
            zpp::bits::varint<type> value;
            if (auto result = archive(value); zpp::bits::failure(result)) {
               return result;
            }
            item = value;
            return zpp::bits::errc{};
         } else if constexpr (base::template is_pb_field<type>()) {
            return deserialize_field(archive, field_type, static_cast<typename type::pb_field_type&>(item));
         } else if constexpr (!zpp::bits::concepts::container<type>) {
            return archive(item);
         } else if constexpr (zpp::bits::concepts::associative_container<type> &&
                              requires { typename type::mapped_type; }) {
            using key_type = std::conditional_t<
               std::is_enum_v<typename type::key_type> && !std::same_as<typename type::key_type, std::byte>,
               zpp::bits::varint<typename type::key_type>,
               typename type::key_type>;

            using mapped_type = std::conditional_t<
               std::is_enum_v<typename type::mapped_type> && !std::same_as<typename type::mapped_type, std::byte>,
               zpp::bits::varint<typename type::mapped_type>,
               typename type::mapped_type>;

            struct value_type {
               key_type key;
               mapped_type value;

               using serialize = zpp::bits::protocol<pb_protocol{}>;
               serialize use();
            };

            alignas(value_type) std::byte storage[sizeof(value_type)];

            auto object = zpp::bits::access::placement_new<value_type>(std::addressof(storage));
            zpp::bits::destructor_guard guard{*object};
            if (auto result = archive(*object); zpp::bits::failure(result)) {
               return result;
            }

            item.emplace(std::move(object->key), std::move(object->value));
            return zpp::bits::errc{};
         } else if constexpr (requires { requires is_pb_int32_v<typename type::value_type>; }) {
            auto fetch = [&]() {
               pb_int32 value;
               if (auto result = archive(value); zpp::bits::failure(result)) {
                  return result;
               }

               if constexpr (requires { item.push_back(value); }) {
                  item.push_back(value);
               } else {
                  item.insert(value);
               }

               return zpp::bits::errc{};
            };

            if (field_type != base::wire_type::length_delimited) {
               return fetch();
            }

            zpp::bits::vsize_t length;
            if (auto result = archive(length); zpp::bits::failure(result)) {
               return result;
            }

            auto end_position = length + archive.position();
            while (archive.position() < end_position) {
               if (auto result = fetch(); zpp::bits::failure(result)) {
                  return result;
               }
            }
            return zpp::bits::errc{};
         } else {
            return base::deserialize_field(archive, field_type, item);
         }
      }
   };

   template <std::size_t Members>
   using pb_members = zpp::bits::protocol<pb_protocol{}, Members>;

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

   inline void from_json(pb_int32& obj, auto& stream) {
      int32_t val;
      from_json(val, stream);
      obj = val;
   }

   inline void to_json(pb_int32 obj, auto& stream) {
      to_json(static_cast<int32_t>(obj), stream);
   }

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
