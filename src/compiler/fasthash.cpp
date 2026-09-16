#include "fasthash.h"
#include <xxhash.h>
#include <memory>
#include <new>

QByteArray fastHash(const QByteArray &bytes, const QByteArray &context)
{
    XXH128_hash_t hash;
    if (context.isEmpty()) {
        hash = XXH3_128bits(bytes.constData(), size_t(bytes.size()));
    } else {
        std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state(XXH3_createState(), &XXH3_freeState);
        if (!state)
            throw std::bad_alloc();
        XXH3_128bits_reset(state.get());
        XXH3_128bits_update(state.get(), context.constData(), size_t(context.size()));
        XXH3_128bits_update(state.get(), "\0", 1);
        XXH3_128bits_update(state.get(), bytes.constData(), size_t(bytes.size()));
        hash = XXH3_128bits_digest(state.get());
    }
    XXH128_canonical_t canonical;
    XXH128_canonicalFromHash(&canonical, hash);
    return QByteArray(reinterpret_cast<const char *>(canonical.digest), sizeof(canonical.digest));
}
