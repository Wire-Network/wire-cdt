/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */
#include "core/sysio/crypto.hpp"
#include "core/sysio/datastream.hpp"
#include "core/sysio/check.hpp"

#include <cstring>
#include <optional>

extern "C" {
   struct __attribute__((aligned (16))) capi_checksum160 { uint8_t hash[20]; };
   struct __attribute__((aligned (16))) capi_checksum256 { uint8_t hash[32]; };
   struct __attribute__((aligned (16))) capi_checksum512 { uint8_t hash[64]; };
   __attribute__((sysio_wasm_import))
   void assert_sha256( const char* data, uint32_t length, const capi_checksum256* hash );

   __attribute__((sysio_wasm_import))
   void assert_sha1( const char* data, uint32_t length, const capi_checksum160* hash );

   __attribute__((sysio_wasm_import))
   void assert_sha512( const char* data, uint32_t length, const capi_checksum512* hash );

   __attribute__((sysio_wasm_import))
   void assert_ripemd160( const char* data, uint32_t length, const capi_checksum160* hash );

   __attribute__((sysio_wasm_import))
   void sha256( const char* data, uint32_t length, capi_checksum256* hash );

   __attribute__((sysio_wasm_import))
   void sha1( const char* data, uint32_t length, capi_checksum160* hash );

   __attribute__((sysio_wasm_import))
   void sha512( const char* data, uint32_t length, capi_checksum512* hash );

   __attribute__((sysio_wasm_import))
   void ripemd160( const char* data, uint32_t length, capi_checksum160* hash );

   __attribute__((sysio_wasm_import))
   int recover_key( const capi_checksum256* digest, const char* sig,
                    size_t siglen, char* pub, size_t publen );

   __attribute__((sysio_wasm_import))
   void assert_recover_key( const capi_checksum256* digest, const char* sig,
                            size_t siglen, const char* pub, size_t publen );
}

namespace sysio {

   void assert_sha256( const char* data, uint32_t length, const sysio::checksum256& hash ) {
      auto hash_data = hash.extract_as_byte_array();
      ::assert_sha256( data, length, reinterpret_cast<const ::capi_checksum256*>(hash_data.data()) );
   }

   void assert_sha1( const char* data, uint32_t length, const sysio::checksum160& hash ) {
      auto hash_data = hash.extract_as_byte_array();
      ::assert_sha1( data, length, reinterpret_cast<const ::capi_checksum160*>(hash_data.data()) );
   }

   void assert_sha512( const char* data, uint32_t length, const sysio::checksum512& hash ) {
      auto hash_data = hash.extract_as_byte_array();
      ::assert_sha512( data, length, reinterpret_cast<const ::capi_checksum512*>(hash_data.data()) );
   }

   void assert_ripemd160( const char* data, uint32_t length, const sysio::checksum160& hash ) {
      auto hash_data = hash.extract_as_byte_array();
      ::assert_ripemd160( data, length, reinterpret_cast<const ::capi_checksum160*>(hash_data.data()) );
   }

   sysio::checksum256 sha256( const char* data, uint32_t length ) {
      ::capi_checksum256 hash;
      ::sha256( data, length, &hash );
      return {hash.hash};
   }

   sysio::checksum160 sha1( const char* data, uint32_t length ) {
      ::capi_checksum160 hash;
      ::sha1( data, length, &hash );
      return {hash.hash};
   }

   sysio::checksum512 sha512( const char* data, uint32_t length ) {
      ::capi_checksum512 hash;
      ::sha512( data, length, &hash );
      return {hash.hash};
   }

   sysio::checksum160 ripemd160( const char* data, uint32_t length ) {
      ::capi_checksum160 hash;
      ::ripemd160( data, length, &hash );
      return {hash.hash};
   }

   namespace {
      /// Shared raw signature-recovery path. Returns the recovered public key,
      /// or std::nullopt when the host signals a contract-observable failure
      /// via a negative return code (bad / empty / truncated signature,
      /// unactivated or unknown signature variant, or recovery-math failure).
      /// Handles the host's query-then-fill contract for public keys larger
      /// than the optimistic stack buffer. One implementation shared by
      /// recover_key (which aborts on failure) and try_recover_key (which
      /// surfaces it to the caller).
      std::optional<sysio::public_key>
      recover_key_impl( const sysio::checksum256& digest, const sysio::signature& sig ) {
         auto digest_data = digest.extract_as_byte_array();

         auto sig_data = sysio::pack(sig);

         // capi_checksum256 is declared alignas(16); digest_data is byte-array
         // storage (alignment 1). reinterpret_cast'ing its data to
         // capi_checksum256* is a misaligned-pointer access -- UB that can
         // trap or be miscompiled on strict / native targets (this path is
         // also exercised natively by the unit tests). Copy into a properly
         // aligned local and pass that instead.
         capi_checksum256 capi_digest{};
         std::memcpy( capi_digest.hash, digest_data.data(), sizeof(capi_digest.hash) );

         char optimistic_pubkey_data[256];
         // `::recover_key` returns int: the required public-key size on
         // success, or a negative code on a contract-observable failure.
         // Capturing it as signed first is essential -- assigning the -1
         // sentinel straight into a size_t yields SIZE_MAX and walks the
         // large-buffer path with a bogus length.
         int rc = ::recover_key( &capi_digest,
                                 sig_data.data(), sig_data.size(),
                                 optimistic_pubkey_data, sizeof(optimistic_pubkey_data) );
         if ( rc < 0 )
            return std::nullopt;

         size_t pubkey_size = static_cast<size_t>(rc);
         sysio::public_key pubkey;
         if ( pubkey_size <= sizeof(optimistic_pubkey_data) ) {
            sysio::datastream<const char*> pubkey_ds( optimistic_pubkey_data, pubkey_size );
            pubkey_ds >> pubkey;
         } else {
            constexpr static size_t max_stack_buffer_size = 512;
            void* pubkey_data = (max_stack_buffer_size < pubkey_size) ? malloc(pubkey_size) : alloca(pubkey_size);
            // malloc can fail; alloca cannot return null. A failed allocation
            // is an environment failure (not a bad-signature condition), so it
            // aborts rather than masquerading as an unrecoverable signature.
            sysio::check( pubkey_data != nullptr, "recover_key: public-key buffer allocation failed" );

            int refill_rc = ::recover_key( &capi_digest,
                                           sig_data.data(), sig_data.size(),
                                           reinterpret_cast<char*>(pubkey_data), pubkey_size );
            // The first call already succeeded, so the refill must report the
            // same size; anything else is an inconsistent host rather than a
            // contract-observable failure.
            sysio::check( refill_rc >= 0 && static_cast<size_t>(refill_rc) == pubkey_size,
                          "recover_key: inconsistent public-key size on refill" );
            sysio::datastream<const char*> pubkey_ds( reinterpret_cast<const char*>(pubkey_data), pubkey_size );
            pubkey_ds >> pubkey;

            if( max_stack_buffer_size < pubkey_size ) {
               free(pubkey_data);
            }
         }
         return pubkey;
      }
   } // namespace

   sysio::public_key recover_key( const sysio::checksum256& digest, const sysio::signature& sig ) {
      auto recovered = recover_key_impl( digest, sig );
      sysio::check( recovered.has_value(), "unable to recover public key from signature" );
      return *recovered;
   }

   std::optional<sysio::public_key> try_recover_key( const sysio::checksum256& digest, const sysio::signature& sig ) {
      return recover_key_impl( digest, sig );
   }

   void assert_recover_key( const sysio::checksum256& digest, const sysio::signature& sig, const sysio::public_key& pubkey ) {
      auto digest_data = digest.extract_as_byte_array();

      auto sig_data = sysio::pack(sig);
      auto pubkey_data = sysio::pack(pubkey);

      // capi_checksum256 is alignas(16); digest_data is byte-array storage
      // (alignment 1). Copy into a properly aligned local rather than
      // reinterpret_cast'ing through a misaligned pointer (UB; can trap on
      // strict / native targets).
      capi_checksum256 capi_digest{};
      std::memcpy( capi_digest.hash, digest_data.data(), sizeof(capi_digest.hash) );

      ::assert_recover_key( &capi_digest,
                            sig_data.data(), sig_data.size(),
                            pubkey_data.data(), pubkey_data.size() );
   }
}
