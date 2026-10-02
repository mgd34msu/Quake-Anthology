#include "qa/network_unified_session.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/random.h>
#endif
#endif

bool qa_unified_token_random(qa_unified_token *out, qa_error *e)
{
    if (!out) { qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing production nonce output"); return false; }
    qa_unified_token token = {{0}};
#ifdef _WIN32
    NTSTATUS result = BCryptGenRandom(NULL, token.bytes, (ULONG)sizeof(token.bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (result != 0) { qa_error_set(e, QA_ERROR_IO, 0, "Native connection randomness failed"); return false; }
#else
    size_t used = 0;
#ifdef __linux__
    while (used < sizeof(token.bytes)) {
        ssize_t received = getrandom(token.bytes + used, sizeof(token.bytes) - used, 0);
        if (received > 0) used += (size_t)received;
        else if (received < 0 && errno == EINTR) continue;
        else { qa_error_set(e, QA_ERROR_IO, 0, "Native connection randomness failed"); return false; }
    }
#else
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int descriptor = open("/dev/urandom", flags);
    if (descriptor < 0) { qa_error_set(e, QA_ERROR_IO, 0, "Opening native connection randomness"); return false; }
    bool ok = true;
    while (used < sizeof(token.bytes)) {
        ssize_t received = read(descriptor, token.bytes + used, sizeof(token.bytes) - used);
        if (received > 0) used += (size_t)received;
        else if (received < 0 && errno == EINTR) continue;
        else { ok = false; break; }
    }
    if (close(descriptor) < 0) ok = false;
    if (!ok) { qa_error_set(e, QA_ERROR_IO, 0, "Reading native connection randomness"); return false; }
#endif
#endif
    *out = token;
    return true;
}
