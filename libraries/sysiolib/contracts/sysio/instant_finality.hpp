#pragma once

#include "../../capi/sysio/types.h"
#include "../../core/sysio/crypto_bls_ext.hpp"

#include <string>
#include <vector>

/**
 * @defgroup instant_finality Instant_finality
 * @ingroup instant_finality
 * @ingroup contracts
 * @brief Defines C++ Instant Finality API
 */

namespace sysio {
    namespace internal_use_do_not_use {
        extern "C" {
            __attribute__((sysio_wasm_import))
            void set_finalizers( uint64_t packed_finalizer_format, const char* data, uint32_t len );
        } // extern "C"
    } //internal_use_do_not_use

    struct finalizer_authority {
        std::string           description;
        uint64_t              weight = 0;    // weight that this finalizer's vote has for meeting threshold
        std::vector<char>     public_key;    // Affine little endian non-montgomery g1

        SYSLIB_SERIALIZE(finalizer_authority, (description)(weight)(public_key));
    };
    struct finalizer_policy {
        uint64_t                          threshold = 0;
        std::vector<finalizer_authority>  finalizers;

        SYSLIB_SERIALIZE(finalizer_policy, (threshold)(finalizers));
    };

/**
 * Submits a finalizer policy change to Instant Finality
 *
 * @param finalizer_policy - finalizer policy to be set
 */
    inline void set_finalizers( const finalizer_policy& finalizer_policy ) {
        for (const auto& finalizer : finalizer_policy.finalizers)
            sysio::check(finalizer.public_key.size() == sizeof(bls_g1), "public key has a wrong size" );
        auto packed = sysio::pack(finalizer_policy);
        // 0 is packed format, currently only 0 is supported
        internal_use_do_not_use::set_finalizers(0, packed.data(), packed.size());
    }

} //sysio
