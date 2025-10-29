#pragma once

#include <bluegrass/meta/preprocessor.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/seq/seq.hpp>
#include <boost/preprocessor/stringize.hpp>

#define SYSLIB_REFLECT_MEMBER_OP( OP, elem ) \
  OP t.elem

#define SYSLIB_REFLECT_MEMBER_COUNT( OP, elem ) \
  OP 1

#define SYSLIB_REFLECT_SEQ_NIL(x) (x)

//#define SYSLIB_REFLECT_ENUM(...) BLUEGRASS_META_SEQ_ENUM(__VA_ARGS__)
/**
 *  @defgroup serialize Serialize
 *  @ingroup core
 *  @brief Defines C++ API to serialize and deserialize object
 */

/**
 *  Defines serialization and deserialization for a class
 *
 *  @ingroup serialize
 *  @param TYPE - the class to have its serialization and deserialization defined
 *  @param MEMBERS - a sequence of member names.  (field1)(field2)(field3)
 */
#define SYSLIB_SERIALIZE( TYPE,  MEMBERS ) \
 template<typename DataStream> \
 friend DataStream& operator << ( DataStream& ds, const TYPE& t ){ \
    uint32_t member_count = 0 BLUEGRASS_META_FOREACH_SEQ( SYSLIB_REFLECT_MEMBER_COUNT, +, MEMBERS ) ; \
    return member_count == 0 ? ds : (ds BLUEGRASS_META_FOREACH_SEQ( SYSLIB_REFLECT_MEMBER_OP, <<, MEMBERS ));\
 }\
 template<typename DataStream> \
 friend DataStream& operator >> ( DataStream& ds, TYPE& t ){ \
    uint32_t member_count = 0 BLUEGRASS_META_FOREACH_SEQ( SYSLIB_REFLECT_MEMBER_COUNT, +, MEMBERS ) ; \
    return member_count == 0 ? ds : (ds BLUEGRASS_META_FOREACH_SEQ( SYSLIB_REFLECT_MEMBER_OP, >>, MEMBERS ));\
 }

/**
 *  Defines serialization and deserialization for a class which inherits from other classes that
 *  have their serialization and deserialization defined
 *
 *  @ingroup serialize
 *  @param TYPE - the class to have its serialization and deserialization defined
 *  @param BASE - a sequence of base class names (basea)(baseb)(basec)
 *  @param MEMBERS - a sequence of member names.  (field1)(field2)(field3)
 */
#define SYSLIB_SERIALIZE_DERIVED( TYPE, BASE, MEMBERS ) \
 template<typename DataStream> \
 friend DataStream& operator << ( DataStream& ds, const TYPE& t ){ \
    ds << static_cast<const BASE&>(t); \
    uint32_t member_count = 0 BLUEGRASS_META_FOREACH_SEQ( SYSLIB_REFLECT_MEMBER_COUNT, +, MEMBERS ); \
    return member_count == 0 ? ds : (ds BLUEGRASS_META_FOREACH_SEQ( SYSLIB_REFLECT_MEMBER_OP, <<, MEMBERS ));\
 }\
 template<typename DataStream> \
 friend DataStream& operator >> ( DataStream& ds, TYPE& t ){ \
    ds >> static_cast<BASE&>(t); \
    uint32_t member_count = 0 BLUEGRASS_META_FOREACH_SEQ( SYSLIB_REFLECT_MEMBER_COUNT, +, MEMBERS ); \
    return member_count == 0 ? ds : (ds BLUEGRASS_META_FOREACH_SEQ( SYSLIB_REFLECT_MEMBER_OP, >>, MEMBERS ));\
 }

#define SYSLIB_SERIALIZE_DERIVED_EMPTY( TYPE, BASE ) \
template<typename DataStream> \
friend DataStream& operator << ( DataStream& ds, const TYPE& t ){ \
ds << static_cast<const BASE&>(t); \
return ds;\
}\
template<typename DataStream> \
friend DataStream& operator >> ( DataStream& ds, TYPE& t ){ \
ds >> static_cast<BASE&>(t); \
return ds;\
}
