#include <sysio/action.h>
#include <sysio/chain.h>
#include <sysio/crypto.h>
#include <sysio/db.h>
#include <sysio/permission.h>
#include <sysio/print.h>
#include <sysio/privileged.h>
#include <sysio/system.h>
#include <sysio/transaction.h>
#include <sysio/types.h>
#include "native/sysio/intrinsics.hpp"
#include "native/sysio/crt.hpp"
#include <softfloat.hpp>
#include <float.h>

// Boilerplate
using namespace sysio::native;
extern "C" {
   void get_resource_limits( capi_name account, int64_t* ram_bytes, int64_t* net_weight, int64_t* cpu_weight ) {
      return intrinsics::get().call<intrinsics::get_resource_limits>(account, ram_bytes, net_weight, cpu_weight);
   }
   void set_resource_limits( capi_name account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight ) {
      return intrinsics::get().call<intrinsics::set_resource_limits>(account, ram_bytes, net_weight, cpu_weight);
   }
   int64_t get_ram_usage( capi_name account ) {
      return intrinsics::get().call<intrinsics::get_ram_usage>(account);
   }
   int64_t set_proposed_producers( char *producer_data, uint32_t producer_data_size ) {
      return intrinsics::get().call<intrinsics::set_proposed_producers>(producer_data, producer_data_size);
   }
   int64_t set_proposed_producers_ex( uint64_t producer_data_format, char *producer_data, uint32_t producer_data_size ) {
      return intrinsics::get().call<intrinsics::set_proposed_producers_ex>(producer_data_format, producer_data, producer_data_size);
   }
   uint32_t get_blockchain_parameters_packed( char* data, uint32_t datalen ) {
      return intrinsics::get().call<intrinsics::get_blockchain_parameters_packed>(data, datalen);
   }
   void set_blockchain_parameters_packed( char* data, uint32_t datalen ) {
      return intrinsics::get().call<intrinsics::set_blockchain_parameters_packed>(data, datalen);
   }
   bool is_privileged( capi_name account ) {
      return intrinsics::get().call<intrinsics::is_privileged>(account);
   }
   void set_privileged( capi_name account, bool is_priv ) {
      return intrinsics::get().call<intrinsics::set_privileged>(account, is_priv);
   }
   bool is_feature_activated( const capi_checksum256* feature_digest ) {
      return intrinsics::get().call<intrinsics::is_feature_activated>(feature_digest);
   }
   void preactivate_feature( const capi_checksum256* feature_digest ) {
      return intrinsics::get().call<intrinsics::preactivate_feature>(feature_digest);
   }
   uint32_t get_active_producers( capi_name* producers, uint32_t datalen ) {
      return intrinsics::get().call<intrinsics::get_active_producers>(producers, datalen);
   }
   void assert_recover_key( const capi_checksum256* digest, const char* sig, size_t siglen, const char* pub, size_t publen ) {
      return intrinsics::get().call<intrinsics::assert_recover_key>(digest, sig, siglen, pub, publen);
   }
   int recover_key( const capi_checksum256* digest, const char* sig, size_t siglen, char* pub, size_t publen ) {
      return intrinsics::get().call<intrinsics::recover_key>(digest, sig, siglen, pub, publen);
   }
   void assert_sha256( const char* data, uint32_t length, const capi_checksum256* hash ) {
      return intrinsics::get().call<intrinsics::assert_sha256>(data, length, hash);
   }
   void assert_sha1( const char* data, uint32_t length, const capi_checksum160* hash ) {
      return intrinsics::get().call<intrinsics::assert_sha1>(data, length, hash);
   }
   void assert_sha512( const char* data, uint32_t length, const capi_checksum512* hash ) {
      return intrinsics::get().call<intrinsics::assert_sha512>(data, length, hash);
   }
   void assert_ripemd160( const char* data, uint32_t length, const capi_checksum160* hash ) {
      return intrinsics::get().call<intrinsics::assert_ripemd160>(data, length, hash);
   }
   void sha256( const char* data, uint32_t length, capi_checksum256* hash ) {
      return intrinsics::get().call<intrinsics::sha256>(data, length, hash);
   }
   void sha1( const char* data, uint32_t length, capi_checksum160* hash ) {
      return intrinsics::get().call<intrinsics::sha1>(data, length, hash);
   }
   void sha512( const char* data, uint32_t length, capi_checksum512* hash ) {
      return intrinsics::get().call<intrinsics::sha512>(data, length, hash);
   }
   void ripemd160( const char* data, uint32_t length, capi_checksum160* hash ) {
      return intrinsics::get().call<intrinsics::ripemd160>(data, length, hash);
   }
   int32_t check_transaction_authorization( const char* trx_data,     uint32_t trx_size,
                                    const char* pubkeys_data, uint32_t pubkeys_size,
                                    const char* perms_data,   uint32_t perms_size
                                  ) {
      return intrinsics::get().call<intrinsics::check_transaction_authorization>(trx_data, trx_size, pubkeys_data, pubkeys_size, perms_data, perms_size);
   }
   int32_t check_permission_authorization( capi_name account, capi_name permission,
                                    const char* pubkeys_data, uint32_t pubkeys_size,
                                    const char* perms_data,   uint32_t perms_size, uint64_t delay_us
                                  ) {
      return intrinsics::get().call<intrinsics::check_permission_authorization>(account, permission, pubkeys_data, pubkeys_size, perms_data, perms_size, delay_us);
   }
   int32_t get_permission_lower_bound( capi_name account, capi_name permission, char* buffer, uint32_t buffer_size ) {
      return intrinsics::get().call<intrinsics::get_permission_lower_bound>(account, permission, buffer, buffer_size);
   }
   uint64_t  current_time() {
      return intrinsics::get().call<intrinsics::current_time>();
   }
   uint64_t  publication_time() {
      return intrinsics::get().call<intrinsics::publication_time>();
   }
   uint32_t read_action_data( void* msg, uint32_t len ) {
      return intrinsics::get().call<intrinsics::read_action_data>(msg, len);
   }
   uint32_t action_data_size() {
      return intrinsics::get().call<intrinsics::action_data_size>();
   }
   capi_name current_receiver() {
      return intrinsics::get().call<intrinsics::current_receiver>();
   }
   void set_action_return_value( void* rv, size_t len ) {
      intrinsics::get().call<intrinsics::set_action_return_value>(rv, len);
   }
   void require_recipient( capi_name name ) {
      return intrinsics::get().call<intrinsics::require_recipient>(name);
   }
   void require_auth( capi_name name ) {
      return intrinsics::get().call<intrinsics::require_auth>(name);
   }
   void require_auth2( capi_name name, capi_name permission ) {
      return intrinsics::get().call<intrinsics::require_auth2>(name, permission);
   }
   bool has_auth( capi_name name ) {
      return intrinsics::get().call<intrinsics::has_auth>(name);
   }
   bool is_account( capi_name name ) {
      return intrinsics::get().call<intrinsics::is_account>(name);
   }
   size_t read_transaction(char *buffer, size_t size) {
      return intrinsics::get().call<intrinsics::read_transaction>(buffer, size);
   }
   size_t transaction_size() {
      return intrinsics::get().call<intrinsics::transaction_size>();
   }
   uint32_t expiration() {
      return intrinsics::get().call<intrinsics::expiration>();
   }
   int tapos_block_prefix() {
      return intrinsics::get().call<intrinsics::tapos_block_prefix>();
   }
   int tapos_block_num() {
      return intrinsics::get().call<intrinsics::tapos_block_num>();
   }
   int get_action( uint32_t type, uint32_t index, char* buff, size_t size ) {
      return intrinsics::get().call<intrinsics::get_action>(type, index, buff, size);
   }
   void send_inline(char *serialized_action, size_t size) {
      return intrinsics::get().call<intrinsics::send_inline>(serialized_action, size);
   }
   void send_context_free_inline(char *serialized_action, size_t size) {
      return intrinsics::get().call<intrinsics::send_context_free_inline>(serialized_action, size);
   }
   int get_context_free_data( uint32_t index, char* buff, size_t size ) {
      return intrinsics::get().call<intrinsics::get_context_free_data>(index, buff, size);
   }
   capi_name get_sender() {
      return intrinsics::get().call<intrinsics::get_sender>();
   }

   // softfloat
   static constexpr uint32_t inv_float_eps = 0x4B000000;
   static constexpr uint64_t inv_double_eps = 0x4330000000000000;

   float _sysio_f32_add( float a, float b ) {
      softfloat32_t ret = f32_add( to_softfloat32(a), to_softfloat32(b) );
      return from_softfloat32(ret);
   }
   float _sysio_f32_sub( float a, float b ) {
      softfloat32_t ret = f32_sub( to_softfloat32(a), to_softfloat32(b) );
      return from_softfloat32(ret);
   }
   float _sysio_f32_div( float a, float b ) {
      softfloat32_t ret = f32_div( to_softfloat32(a), to_softfloat32(b) );
      return from_softfloat32(ret);
   }
   float _sysio_f32_mul( float a, float b ) {
      softfloat32_t ret = f32_mul( to_softfloat32(a), to_softfloat32(b) );
      return from_softfloat32(ret);
   }
   float _sysio_f32_min( float af, float bf ) {
      softfloat32_t a = to_softfloat32(af);
      softfloat32_t b = to_softfloat32(bf);
      if (f32_is_nan(a)) {
         return af;
      }
      if (f32_is_nan(b)) {
         return bf;
      }
      if ( f32_sign_bit(a) != f32_sign_bit(b) ) {
         return f32_sign_bit(a) ? af : bf;
      }
      return f32_lt(a,b) ? af : bf;
   }
   float _sysio_f32_max( float af, float bf ) {
      softfloat32_t a = to_softfloat32(af);
      softfloat32_t b = to_softfloat32(bf);
      if (f32_is_nan(a)) {
         return af;
      }
      if (f32_is_nan(b)) {
         return bf;
      }
      if ( f32_sign_bit(a) != f32_sign_bit(b) ) {
         return f32_sign_bit(a) ? bf : af;
      }
      return f32_lt( a, b ) ? bf : af;
   }
   float _sysio_f32_copysign( float af, float bf ) {
      softfloat32_t a = to_softfloat32(af);
      softfloat32_t b = to_softfloat32(bf);
      uint32_t sign_of_b = b.v >> 31;
      a.v &= ~(1 << 31);             // clear the sign bit
      a.v = a.v | (sign_of_b << 31); // add the sign of b
      return from_softfloat32(a);
   }
   // float unops
   float _sysio_f32_abs( float af ) {
      softfloat32_t a = to_softfloat32(af);
      a.v &= ~(1 << 31);
      return from_softfloat32(a);
   }
   float _sysio_f32_neg( float af ) {
      softfloat32_t a = to_softfloat32(af);
      uint32_t sign = a.v >> 31;
      a.v &= ~(1 << 31);
      a.v |= (!sign << 31);
      return from_softfloat32(a);
   }
   float _sysio_f32_sqrt( float a ) {
      softfloat32_t ret = f32_sqrt( to_softfloat32(a) );
      return from_softfloat32(ret);
   }
   // ceil, floor, trunc and nearest are lifted from libc
   float _sysio_f32_ceil( float af ) {
      softfloat32_t a = to_softfloat32(af);
      int e = (int)(a.v >> 23 & 0xFF) - 0X7F;
      uint32_t m;
      if (e >= 23)
         return af;
      if (e >= 0) {
         m = 0x007FFFFF >> e;
         if ((a.v & m) == 0)
            return af;
         if (a.v >> 31 == 0)
            a.v += m;
         a.v &= ~m;
      } else {
         if (a.v >> 31)
            a.v = 0x80000000; // return -0.0f
         else if (a.v << 1)
            a.v = 0x3F800000; // return 1.0f
      }

      return from_softfloat32(a);
   }
   float _sysio_f32_floor( float af ) {
      softfloat32_t a = to_softfloat32(af);
      int e = (int)(a.v >> 23 & 0xFF) - 0X7F;
      uint32_t m;
      if (e >= 23)
         return af;
      if (e >= 0) {
         m = 0x007FFFFF >> e;
         if ((a.v & m) == 0)
            return af;
         if (a.v >> 31)
            a.v += m;
         a.v &= ~m;
      } else {
         if (a.v >> 31 == 0)
            a.v = 0;
         else if (a.v << 1)
            a.v = 0xBF800000; // return -1.0f
      }
      return from_softfloat32(a);
   }
   float _sysio_f32_trunc( float af ) {
      softfloat32_t a = to_softfloat32(af);
      int e = (int)(a.v >> 23 & 0xff) - 0x7f + 9;
      uint32_t m;
      if (e >= 23 + 9)
         return af;
      if (e < 9)
         e = 1;
      m = -1U >> e;
      if ((a.v & m) == 0)
         return af;
      a.v &= ~m;
      return from_softfloat32(a);
   }
   float _sysio_f32_nearest( float af ) {
      softfloat32_t a = to_softfloat32(af);
      int e = a.v>>23 & 0xff;
      int s = a.v>>31;
      softfloat32_t y;
      if (e >= 0x7f+23)
         return af;
      if (s)
         y = f32_add( f32_sub( a, softfloat32_t{inv_float_eps} ), softfloat32_t{inv_float_eps} );
      else
         y = f32_sub( f32_add( a, softfloat32_t{inv_float_eps} ), softfloat32_t{inv_float_eps} );
      if (f32_eq( y, {0} ) )
         return s ? -0.0f : 0.0f;
      return from_softfloat32(y);
   }

   // float relops
   bool _sysio_f32_eq( float a, float b ) {  return f32_eq( to_softfloat32(a), to_softfloat32(b) ); }
   bool _sysio_f32_ne( float a, float b ) { return !f32_eq( to_softfloat32(a), to_softfloat32(b) ); }
   bool _sysio_f32_lt( float a, float b ) { return f32_lt( to_softfloat32(a), to_softfloat32(b) ); }
   bool _sysio_f32_le( float a, float b ) { return f32_le( to_softfloat32(a), to_softfloat32(b) ); }
   bool _sysio_f32_gt( float af, float bf ) {
      softfloat32_t a = to_softfloat32(af);
      softfloat32_t b = to_softfloat32(bf);
      if (f32_is_nan(a))
         return false;
      if (f32_is_nan(b))
         return false;
      return !f32_le( a, b );
   }
   bool _sysio_f32_ge( float af, float bf ) {
      softfloat32_t a = to_softfloat32(af);
      softfloat32_t b = to_softfloat32(bf);
      if (f32_is_nan(a))
         return false;
      if (f32_is_nan(b))
         return false;
      return !f32_lt( a, b );
   }

   // double binops
   double _sysio_f64_add( double a, double b ) {
      softfloat64_t ret = f64_add( to_softfloat64(a), to_softfloat64(b) );
      return from_softfloat64(ret);
   }
   double _sysio_f64_sub( double a, double b ) {
      softfloat64_t ret = f64_sub( to_softfloat64(a), to_softfloat64(b) );
      return from_softfloat64(ret);
   }
   double _sysio_f64_div( double a, double b ) {
      softfloat64_t ret = f64_div( to_softfloat64(a), to_softfloat64(b) );
      return from_softfloat64(ret);
   }
   double _sysio_f64_mul( double a, double b ) {
      softfloat64_t ret = f64_mul( to_softfloat64(a), to_softfloat64(b) );
      return from_softfloat64(ret);
   }
   double _sysio_f64_min( double af, double bf ) {
      softfloat64_t a = to_softfloat64(af);
      softfloat64_t b = to_softfloat64(bf);
      if (f64_is_nan(a))
         return af;
      if (f64_is_nan(b))
         return bf;
      if (f64_sign_bit(a) != f64_sign_bit(b))
         return f64_sign_bit(a) ? af : bf;
      return f64_lt( a, b ) ? af : bf;
   }
   double _sysio_f64_max( double af, double bf ) {
      softfloat64_t a = to_softfloat64(af);
      softfloat64_t b = to_softfloat64(bf);
      if (f64_is_nan(a))
         return af;
      if (f64_is_nan(b))
         return bf;
      if (f64_sign_bit(a) != f64_sign_bit(b))
         return f64_sign_bit(a) ? bf : af;
      return f64_lt( a, b ) ? bf : af;
   }
   double _sysio_f64_copysign( double af, double bf ) {
      softfloat64_t a = to_softfloat64(af);
      softfloat64_t b = to_softfloat64(bf);
      uint64_t sign_of_b = b.v >> 63;
      a.v &= ~(uint64_t(1) << 63);             // clear the sign bit
      a.v = a.v | (sign_of_b << 63); // add the sign of b
      return from_softfloat64(a);
   }

   // double unops
   double _sysio_f64_abs( double af ) {
      softfloat64_t a = to_softfloat64(af);
      a.v &= ~(uint64_t(1) << 63);
      return from_softfloat64(a);
   }
   double _sysio_f64_neg( double af ) {
      softfloat64_t a = to_softfloat64(af);
      uint64_t sign = a.v >> 63;
      a.v &= ~(uint64_t(1) << 63);
      a.v |= (uint64_t(!sign) << 63);
      return from_softfloat64(a);
   }
   double _sysio_f64_sqrt( double a ) {
      softfloat64_t ret = f64_sqrt( to_softfloat64(a) );
      return from_softfloat64(ret);
   }
   // ceil, floor, trunc and nearest are lifted from libc
   double _sysio_f64_ceil( double af ) {
      softfloat64_t a = to_softfloat64( af );
      softfloat64_t ret;
      int e = a.v >> 52 & 0x7ff;
      softfloat64_t y;
      if (e >= 0x3ff+52 || f64_eq( a, { 0 } ))
         return af;
      /* y = int(x) - x, where int(x) is an integer neighbor of x */
      if (a.v >> 63)
         y = f64_sub( f64_add( f64_sub( a, softfloat64_t{inv_double_eps} ), softfloat64_t{inv_double_eps} ), a );
      else
         y = f64_sub( f64_sub( f64_add( a, softfloat64_t{inv_double_eps} ), softfloat64_t{inv_double_eps} ), a );
      /* special case because of non-nearest rounding modes */
      if (e <= 0x3ff-1) {
         return a.v >> 63 ? -0.0 : 1.0; //softfloat64_t{0x8000000000000000} : softfloat64_t{0xBE99999A3F800000}; //either -0.0 or 1
      }
      if (f64_lt( y, to_softfloat64(0) )) {
         ret = f64_add( f64_add( a, y ), to_softfloat64(1) ); // 0xBE99999A3F800000 } ); // plus 1
         return from_softfloat64(ret);
      }
      ret = f64_add( a, y );
      return from_softfloat64(ret);
   }
   double _sysio_f64_floor( double af ) {
      softfloat64_t a = to_softfloat64( af );
      softfloat64_t ret;
      int e = a.v >> 52 & 0x7FF;
      softfloat64_t y;
      if ( a.v == 0x8000000000000000) {
         return af;
      }
      if (e >= 0x3FF+52 || a.v == 0) {
         return af;
      }
      if (a.v >> 63)
         y = f64_sub( f64_add( f64_sub( a, softfloat64_t{inv_double_eps} ), softfloat64_t{inv_double_eps} ), a );
      else
         y = f64_sub( f64_sub( f64_add( a, softfloat64_t{inv_double_eps} ), softfloat64_t{inv_double_eps} ), a );
      if (e <= 0x3FF-1) {
         return a.v>>63 ? -1.0 : 0.0; //softfloat64_t{0xBFF0000000000000} : softfloat64_t{0}; // -1 or 0
      }
      if ( !f64_le( y, softfloat64_t{0} ) ) {
         ret = f64_sub( f64_add(a,y), to_softfloat64(1.0));
         return from_softfloat64(ret);
      }
      ret = f64_add( a, y );
      return from_softfloat64(ret);
   }
   double _sysio_f64_trunc( double af ) {
      softfloat64_t a = to_softfloat64( af );
      int e = (int)(a.v >> 52 & 0x7ff) - 0x3ff + 12;
      uint64_t m;
      if (e >= 52 + 12)
         return af;
      if (e < 12)
         e = 1;
      m = -1ULL >> e;
      if ((a.v & m) == 0)
         return af;
      a.v &= ~m;
      return from_softfloat64(a);
   }

   double _sysio_f64_nearest( double af ) {
      softfloat64_t a = to_softfloat64( af );
      int e = (a.v >> 52 & 0x7FF);
      int s = a.v >> 63;
      softfloat64_t y;
      if ( e >= 0x3FF+52 )
         return af;
      if ( s )
         y = f64_add( f64_sub( a, softfloat64_t{inv_double_eps} ), softfloat64_t{inv_double_eps} );
      else
         y = f64_sub( f64_add( a, softfloat64_t{inv_double_eps} ), softfloat64_t{inv_double_eps} );
      if ( f64_eq( y, softfloat64_t{0} ) )
         return s ? -0.0 : 0.0;
      return from_softfloat64(y);
   }

   // double relops
   bool _sysio_f64_eq( double a, double b ) { return f64_eq( to_softfloat64(a), to_softfloat64(b) ); }
   bool _sysio_f64_ne( double a, double b ) { return !f64_eq( to_softfloat64(a), to_softfloat64(b) ); }
   bool _sysio_f64_lt( double a, double b ) { return f64_lt( to_softfloat64(a), to_softfloat64(b) ); }
   bool _sysio_f64_le( double a, double b ) { return f64_le( to_softfloat64(a), to_softfloat64(b) ); }
   bool _sysio_f64_gt( double af, double bf ) {
      softfloat64_t a = to_softfloat64(af);
      softfloat64_t b = to_softfloat64(bf);
      if (f64_is_nan(a))
         return false;
      if (f64_is_nan(b))
         return false;
      return !f64_le( a, b );
   }
   bool _sysio_f64_ge( double af, double bf ) {
      softfloat64_t a = to_softfloat64(af);
      softfloat64_t b = to_softfloat64(bf);
      if (f64_is_nan(a))
         return false;
      if (f64_is_nan(b))
         return false;
      return !f64_lt( a, b );
   }

   // float and double conversions
   double _sysio_f32_promote( float a ) {
      return from_softfloat64(f32_to_f64( to_softfloat32(a)) );
   }
   float _sysio_f64_demote( double a ) {
      return from_softfloat32(f64_to_f32( to_softfloat64(a)) );
   }
   int32_t _sysio_f32_trunc_i32s( float af ) {
      softfloat32_t a = to_softfloat32(af);
      if (_sysio_f32_ge(af, 2147483648.0f) || _sysio_f32_lt(af, -2147483648.0f))
         sysio_assert(false,  "Error, f32.convert_s/i32 overflow" );

      if (f32_is_nan(a))
         sysio_assert(false,  "Error, f32.convert_s/i32 unrepresentable");
      return f32_to_i32( to_softfloat32(_sysio_f32_trunc( af )), 0, false );
   }
   int32_t _sysio_f64_trunc_i32s( double af ) {
      softfloat64_t a = to_softfloat64(af);
      if (_sysio_f64_ge(af, 2147483648.0) || _sysio_f64_lt(af, -2147483648.0))
         sysio_assert(false,  "Error, f64.convert_s/i32 overflow");
      if (f64_is_nan(a))
         sysio_assert(false,  "Error, f64.convert_s/i32 unrepresentable");
      return f64_to_i32( to_softfloat64(_sysio_f64_trunc( af )), 0, false );
   }
   uint32_t _sysio_f32_trunc_i32u( float af ) {
      softfloat32_t a = to_softfloat32(af);
      if (_sysio_f32_ge(af, 4294967296.0f) || _sysio_f32_le(af, -1.0f))
         sysio_assert(false,  "Error, f32.convert_u/i32 overflow");
      if (f32_is_nan(a))
         sysio_assert(false,  "Error, f32.convert_u/i32 unrepresentable");
      return f32_to_ui32( to_softfloat32(_sysio_f32_trunc( af )), 0, false );
   }
   uint32_t _sysio_f64_trunc_i32u( double af ) {
      softfloat64_t a = to_softfloat64(af);
      if (_sysio_f64_ge(af, 4294967296.0) || _sysio_f64_le(af, -1.0))
         sysio_assert(false,  "Error, f64.convert_u/i32 overflow");
      if (f64_is_nan(a))
         sysio_assert(false,  "Error, f64.convert_u/i32 unrepresentable");
      return f64_to_ui32( to_softfloat64(_sysio_f64_trunc( af )), 0, false );
   }
   int64_t _sysio_f32_trunc_i64s( float af ) {
      softfloat32_t a = to_softfloat32(af);
      if (_sysio_f32_ge(af, 9223372036854775808.0f) || _sysio_f32_lt(af, -9223372036854775808.0f))
         sysio_assert(false,  "Error, f32.convert_s/i64 overflow");
      if (f32_is_nan(a))
         sysio_assert(false,  "Error, f32.convert_s/i64 unrepresentable");
      return f32_to_i64( to_softfloat32(_sysio_f32_trunc( af )), 0, false );
   }
   int64_t _sysio_f64_trunc_i64s( double af ) {
      softfloat64_t a = to_softfloat64(af);
      if (_sysio_f64_ge(af, 9223372036854775808.0) || _sysio_f64_lt(af, -9223372036854775808.0))
         sysio_assert(false,  "Error, f64.convert_s/i64 overflow");
      if (f64_is_nan(a))
         sysio_assert(false,  "Error, f64.convert_s/i64 unrepresentable");

      return f64_to_i64( to_softfloat64(_sysio_f64_trunc( af )), 0, false );
   }
   uint64_t _sysio_f32_trunc_i64u( float af ) {
      softfloat32_t a = to_softfloat32(af);
      if (_sysio_f32_ge(af, 18446744073709551616.0f) || _sysio_f32_le(af, -1.0f))
         sysio_assert(false,  "Error, f32.convert_u/i64 overflow");
      if (f32_is_nan(a))
         sysio_assert(false,  "Error, f32.convert_u/i64 unrepresentable");
      return f32_to_ui64( to_softfloat32(_sysio_f32_trunc( af )), 0, false );
   }
   uint64_t _sysio_f64_trunc_i64u( double af ) {
      softfloat64_t a = to_softfloat64(af);
      if (_sysio_f64_ge(af, 18446744073709551616.0) || _sysio_f64_le(af, -1.0))
         sysio_assert(false,  "Error, f64.convert_u/i64 overflow");
      if (f64_is_nan(a))
         sysio_assert(false,  "Error, f64.convert_u/i64 unrepresentable");
      return f64_to_ui64( to_softfloat64(_sysio_f64_trunc( af )), 0, false );
   }
   float _sysio_i32_to_f32( int32_t a )  {
      return from_softfloat32(i32_to_f32( a ));
   }
   float _sysio_i64_to_f32( int64_t a ) {
      return from_softfloat32(i64_to_f32( a ));
   }
   float _sysio_ui32_to_f32( uint32_t a ) {
      return from_softfloat32(ui32_to_f32( a ));
   }
   float _sysio_ui64_to_f32( uint64_t a ) {
      return from_softfloat32(ui64_to_f32( a ));
   }
   double _sysio_i32_to_f64( int32_t a ) {
      return from_softfloat64(i32_to_f64( a ));
   }
   double _sysio_i64_to_f64( int64_t a ) {
      return from_softfloat64(i64_to_f64( a ));
   }
   double _sysio_ui32_to_f64( uint32_t a ) {
      return from_softfloat64(ui32_to_f64( a ));
   }
   double _sysio_ui64_to_f64( uint64_t a ) {
      return from_softfloat64(ui64_to_f64( a ));
   }

   void prints_l(const char* cstr, uint32_t len) {
      return intrinsics::get().call<intrinsics::prints_l>(cstr, len);
   }

   void prints(const char* cstr) {
      return intrinsics::get().call<intrinsics::prints>(cstr);
   }

   void printi(int64_t value) {
      return intrinsics::get().call<intrinsics::printi>(value);
   }

   void printui(uint64_t value) {
      return intrinsics::get().call<intrinsics::printui>(value);
   }

   void printi128(const int128_t* value) {
      return intrinsics::get().call<intrinsics::printi128>(value);
   }

    void printui128(const uint128_t* value) {
      return intrinsics::get().call<intrinsics::printui128>(value);
   }

   void printsf(float value) {
      return intrinsics::get().call<intrinsics::printsf>(value);
   }

   void printdf(double value) {
      return intrinsics::get().call<intrinsics::printdf>(value);
   }

   void printqf(const long double* value) {
      return intrinsics::get().call<intrinsics::printqf>(value);
   }

   void printn(uint64_t nm) {
      return intrinsics::get().call<intrinsics::printn>(nm);
   }

   void printhex(const void* data, uint32_t len) {
      return intrinsics::get().call<intrinsics::printhex>(data, len);
   }

   void* memset ( void* ptr, int value, size_t num ) {
      unsigned char v = value;
      char* _ptr = (char*)ptr;
      for (int i=0; i < num; i++) {
         _ptr[i] = v;
      }
      return (void*)_ptr;
   }
   void* memcpy ( void* destination, const void* source, size_t num ) {
      char* dest = (char*)destination;
      char* src = (char*)source;
      for (int i=0; i < num; i++) {
         dest[i] = src[i];
      }
      return (void*)dest;
   }

   void* memmove ( void* destination, const void* source, size_t num ) {
      char tmp_buf[num];
      char* dest = (char*)destination;
      char* src = (char*)source;
      for (int i=0; i < num; i++)
         tmp_buf[i] = src[i];
      for (int i=0; i < num; i++)
         dest[i] = tmp_buf[i];
      return (void*)dest;
   }

   void sysio_assert(uint32_t test, const char* msg) {
      if (test == 0) {
         _prints(msg, sysio::cdt::output_stream_kind::std_err);
         _prints_l("\n", 1, sysio::cdt::output_stream_kind::none);
         longjmp(*___env_ptr, 1);
      }
   }

   void sysio_assert_message(uint32_t test, const char* msg, uint32_t len) {
      if (test == 0) {
         _prints_l(msg, len, sysio::cdt::output_stream_kind::std_err);
         _prints_l("\n", 1, sysio::cdt::output_stream_kind::none);
         longjmp(*___env_ptr, 1);
      }
   }

   void sysio_assert_code(uint32_t test, uint64_t code) {
      if (test == 0) {
         char buff[32];
         snprintf(buff, 32, "%llu", code);
         _prints(buff, sysio::cdt::output_stream_kind::std_err);
         _prints_l("\n", 1, sysio::cdt::output_stream_kind::none);
         longjmp(*___env_ptr, 1);
      }
   }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-noreturn"
   void abort() {
      sysio_assert(false, "abort");
   }
#pragma clang diagnostic pop

   int64_t add_security_group_participants(const char* data, uint32_t datalen) {
      return intrinsics::get().call<intrinsics::add_security_group_participants>(data, datalen);
   }

   int64_t remove_security_group_participants(const char* data, uint32_t datalen){
      return intrinsics::get().call<intrinsics::remove_security_group_participants>(data, datalen);
   }

   bool in_active_security_group(const char* data, uint32_t datalen){
      return intrinsics::get().call<intrinsics::in_active_security_group>(data, datalen);
   }

   uint32_t get_active_security_group(char* data, uint32_t datalen){
      return intrinsics::get().call<intrinsics::get_active_security_group>(data, datalen);
   }
  
   void set_finalizers(uint64_t packed_finalizer_format, const char* data, uint32_t len) {
      intrinsics::get().call<intrinsics::set_finalizers>(packed_finalizer_format, data, len);
   }
  
} // extern "C"

int32_t blake2_f( uint32_t rounds, const char* state, uint32_t state_len, const char* msg, uint32_t msg_len, 
                  const char* t0_offset, uint32_t t0_len, const char* t1_offset, uint32_t t1_len, int32_t final, char* result, uint32_t result_len) {
   return intrinsics::get().call<intrinsics::blake2_f>(rounds, state, state_len, msg, msg_len, t0_offset, t0_len, t1_offset, t1_len, final, result, result_len);
}

int32_t k1_recover( const char* sig, uint32_t sig_len, const char* dig, uint32_t dig_len, char* pub, uint32_t pub_len) {
   return intrinsics::get().call<intrinsics::k1_recover>(sig, sig_len, dig, dig_len, pub, pub_len);
}

int32_t alt_bn128_add( const char* op1, uint32_t op1_len, const char* op2, uint32_t op2_len, char* result, uint32_t result_len) {
   return intrinsics::get().call<intrinsics::alt_bn128_add>(op1, op1_len, op2, op2_len, result, result_len);
}

int32_t alt_bn128_mul( const char* g1, uint32_t g1_len, const char* scalar, uint32_t scalar_len, char* result, uint32_t result_len) {
   return intrinsics::get().call<intrinsics::alt_bn128_mul>(g1, g1_len, scalar, scalar_len, result, result_len);
}

int32_t alt_bn128_pair( const char* pairs, uint32_t pairs_len) {
   return intrinsics::get().call<intrinsics::alt_bn128_pair>(pairs, pairs_len);
}

int32_t mod_exp( const char* base, uint32_t base_len, const char* exp, uint32_t exp_len, const char* mod, uint32_t mod_len, char* result, uint32_t result_len) {
   return intrinsics::get().call<intrinsics::mod_exp>(base, base_len, exp, exp_len, mod, mod_len, result, result_len);
}

void sha3( const char* data, uint32_t data_len, char* hash, uint32_t hash_len, int32_t keccak ) {
   intrinsics::get().call<intrinsics::sha3>(data, data_len, hash, hash_len, keccak);
}

int32_t bls_g1_add(const char* op1, uint32_t op1_len, const char* op2, uint32_t op2_len, char* res, uint32_t res_len)
{
    return intrinsics::get().call<intrinsics::bls_g1_add>(op1, op1_len, op2, op2_len, res, res_len);
}

int32_t bls_g2_add(const char* op1, uint32_t op1_len, const char* op2, uint32_t op2_len, char* res, uint32_t res_len)
{
    return intrinsics::get().call<intrinsics::bls_g2_add>(op1, op1_len, op2, op2_len, res, res_len);
}

int32_t bls_g1_weighted_sum(const char* points, uint32_t points_len, const char* scalars, uint32_t scalars_len, uint32_t n, char* res, uint32_t res_len)
{
    return  intrinsics::get().call<intrinsics::bls_g1_weighted_sum>(points, points_len, scalars, scalars_len, n, res, res_len);
}

int32_t bls_g2_weighted_sum(const char* points, uint32_t points_len, const char* scalars, uint32_t scalars_len, uint32_t n, char* res, uint32_t res_len)
{
    return  intrinsics::get().call<intrinsics::bls_g2_weighted_sum>(points, points_len, scalars, scalars_len, n, res, res_len);
}

int32_t bls_pairing(const char* g1_points, uint32_t g1_points_len, const char* g2_points, uint32_t g2_points_len, uint32_t n, char* res, uint32_t res_len)
{
    return intrinsics::get().call<intrinsics::bls_pairing>(g1_points, g1_points_len, g1_points, g1_points_len, n, res, res_len);
}

int32_t bls_g1_map(const char* e, uint32_t e_len, char* res, uint32_t res_len)
{
    return intrinsics::get().call<intrinsics::bls_g1_map>(e, e_len, res, res_len);
}

int32_t bls_g2_map(const char* e, uint32_t e_len, char* res, uint32_t res_len)
{
    return intrinsics::get().call<intrinsics::bls_g2_map>(e, e_len, res, res_len);
}

int32_t bls_fp_mod(const char* s, uint32_t s_len, char* res, uint32_t res_len)
{
    return intrinsics::get().call<intrinsics::bls_fp_mod>(s, s_len, res, res_len);
}

int32_t bls_fp_mul(const char* op1, uint32_t op1_len, const char* op2, uint32_t op2_len, char* res, uint32_t res_len)
{
    return  intrinsics::get().call<intrinsics::bls_fp_mul>(op1, op1_len, op2, op2_len, res, res_len);
}

int32_t bls_fp_exp(const char* base, uint32_t base_len, const char* exp, uint32_t exp_len, char* res, uint32_t res_len)
{
    return  intrinsics::get().call<intrinsics::bls_fp_exp>(base, base_len, exp, exp_len, res, res_len);
}

// --- KV Database intrinsics ---

int64_t kv_set(uint32_t key_format, uint64_t payer, const void* key, uint32_t key_size, const void* value, uint32_t value_size) {
   return intrinsics::get().call<intrinsics::kv_set>(key_format, payer, key, key_size, value, value_size);
}

int32_t kv_get(uint32_t key_format, capi_name code, const void* key, uint32_t key_size, void* value, uint32_t value_size) {
   return intrinsics::get().call<intrinsics::kv_get>(key_format, code, key, key_size, value, value_size);
}

int64_t kv_erase(uint32_t key_format, const void* key, uint32_t key_size) {
   return intrinsics::get().call<intrinsics::kv_erase>(key_format, key, key_size);
}

int32_t kv_contains(uint32_t key_format, capi_name code, const void* key, uint32_t key_size) {
   return intrinsics::get().call<intrinsics::kv_contains>(key_format, code, key, key_size);
}

uint32_t kv_it_create(uint32_t key_format, capi_name code, const void* prefix, uint32_t prefix_size) {
   return intrinsics::get().call<intrinsics::kv_it_create>(key_format, code, prefix, prefix_size);
}

void kv_it_destroy(uint32_t handle) {
   intrinsics::get().call<intrinsics::kv_it_destroy>(handle);
}

int32_t kv_it_status(uint32_t handle) {
   return intrinsics::get().call<intrinsics::kv_it_status>(handle);
}

int32_t kv_it_next(uint32_t handle) {
   return intrinsics::get().call<intrinsics::kv_it_next>(handle);
}

int32_t kv_it_prev(uint32_t handle) {
   return intrinsics::get().call<intrinsics::kv_it_prev>(handle);
}

int32_t kv_it_lower_bound(uint32_t handle, const void* key, uint32_t key_size) {
   return intrinsics::get().call<intrinsics::kv_it_lower_bound>(handle, key, key_size);
}

int32_t kv_it_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size) {
   return intrinsics::get().call<intrinsics::kv_it_key>(handle, offset, dest, dest_size, actual_size);
}

int32_t kv_it_value(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size) {
   return intrinsics::get().call<intrinsics::kv_it_value>(handle, offset, dest, dest_size, actual_size);
}

void kv_idx_store(uint64_t payer, uint32_t table_id,
                  const void* pri_key, uint32_t pri_key_size,
                  const void* sec_key, uint32_t sec_key_size) {
   intrinsics::get().call<intrinsics::kv_idx_store>(payer, table_id, pri_key, pri_key_size, sec_key, sec_key_size);
}

void kv_idx_remove(uint32_t table_id,
                   const void* pri_key, uint32_t pri_key_size,
                   const void* sec_key, uint32_t sec_key_size) {
   intrinsics::get().call<intrinsics::kv_idx_remove>(table_id, pri_key, pri_key_size, sec_key, sec_key_size);
}

void kv_idx_update(uint64_t payer, uint32_t table_id,
                   const void* pri_key, uint32_t pri_key_size,
                   const void* old_sec_key, uint32_t old_sec_key_size,
                   const void* new_sec_key, uint32_t new_sec_key_size) {
   intrinsics::get().call<intrinsics::kv_idx_update>(payer, table_id, pri_key, pri_key_size, old_sec_key, old_sec_key_size, new_sec_key, new_sec_key_size);
}

int32_t kv_idx_find_secondary(capi_name code, uint32_t table_id,
                              const void* sec_key, uint32_t sec_key_size) {
   return intrinsics::get().call<intrinsics::kv_idx_find_secondary>(code, table_id, sec_key, sec_key_size);
}

int32_t kv_idx_lower_bound(capi_name code, uint32_t table_id,
                           const void* sec_key, uint32_t sec_key_size) {
   return intrinsics::get().call<intrinsics::kv_idx_lower_bound>(code, table_id, sec_key, sec_key_size);
}

int32_t kv_idx_next(uint32_t handle) {
   return intrinsics::get().call<intrinsics::kv_idx_next>(handle);
}

int32_t kv_idx_prev(uint32_t handle) {
   return intrinsics::get().call<intrinsics::kv_idx_prev>(handle);
}

int32_t kv_idx_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size) {
   return intrinsics::get().call<intrinsics::kv_idx_key>(handle, offset, dest, dest_size, actual_size);
}

int32_t kv_idx_primary_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size) {
   return intrinsics::get().call<intrinsics::kv_idx_primary_key>(handle, offset, dest, dest_size, actual_size);
}

void kv_idx_destroy(uint32_t handle) {
   intrinsics::get().call<intrinsics::kv_idx_destroy>(handle);
}
