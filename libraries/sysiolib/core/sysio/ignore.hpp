#pragma once

#include "datastream.hpp"

#include <type_traits>

namespace sysio {
   /**
    * @defgroup ignore
    * @ingroup core
    * @brief Enables telling the datastream to ignore this type, but allowing abi generator to add the correct type.
    */

   /**
    * Tells the datastream to ignore this type, but allows the abi generator to add the correct type.
    *
    * @ingroup ignore
    * @details Currently non-ignore types can not succeed an ignore type in a method definition, i.e. void foo(float, ignore<int>) is allowed and void foo(float, ignore<int>, int) is not allowed.
    * @note This restriction will be relaxed in a later release. Currently non-ignore types can not succeed an ignore type in a method definition, i.e. void foo(float, ignore<int>) is allowed and void foo(float, ignore<int>, int) is not allowed.
    */
   template <typename T>
   struct [[sysio::ignore]] ignore {};

   /**
    * True when `T` is `ignore<U>` for some `U`.
    *
    * @ingroup ignore
    * @details A dispatcher leaves the bytes of an `ignore<U>` parameter in the action's datastream for the action
    * itself to read, so it cannot check that the action's data was read to the end.
    */
   template <typename T>
   struct is_ignore : std::false_type {};

   /// @copydoc is_ignore
   template <typename T>
   struct is_ignore<ignore<T>> : std::true_type {};

   /// @copydoc is_ignore
   template <typename T>
   inline constexpr bool is_ignore_v = is_ignore<T>::value;

    /**
    * Wrapper class to allow sending inline actions with the correct payload
    */
   template <typename T>
   struct ignore_wrapper {
      constexpr ignore_wrapper() {}
      constexpr ignore_wrapper(T val) : value(val) {}
      constexpr ignore_wrapper(ignore<T> val) {}
      constexpr inline T get() { return value; }
      constexpr operator T() { return value; }
      constexpr operator ignore<T>() { return {}; }
      T value;
   };

   /**
    *  Serialize an ignored_wrapper type into a stream
    *
    *  @brief Serialize ignored_wrapper<T>'s T value
    *  @param ds - The stream to write
    *  @param val - The value to serialize
    *  @tparam DataStream - Type of datastream buffer
    *  @return DataStream& - Reference to the datastream
    */
   template<typename DataStream, typename T>
   inline DataStream& operator<<(DataStream& ds, const ::sysio::ignore_wrapper<T>& val) {
     ds << val.value;
     return ds;
   }

   /**
    *  Serialize an ignored type into a stream
    *
    *  @brief Serialize an ignored type
    *  @param ds - The stream to write
    *  @param ignore - The value to serialize
    *  @tparam DataStream - Type of datastream buffer
    *  @return DataStream& - Reference to the datastream
    */
   template<typename DataStream, typename T>
   inline DataStream& operator<<(DataStream& ds, const ::sysio::ignore<T>& val) {
     return ds;
   }

   /**
    *  Deserialize an ignored type from a stream
    *
    *  @brief Deserialize an ignored type
    *  @param ds - The stream to read
    *  @param ignored - The destination for deserialized value
    *  @tparam DataStream - Type of datastream buffer
    *  @return DataStream& - Reference to the datastream
    */
   template<typename DataStream, typename T>
   inline DataStream& operator>>(DataStream& ds, ::sysio::ignore<T>&) {
     return ds;
   }
}
