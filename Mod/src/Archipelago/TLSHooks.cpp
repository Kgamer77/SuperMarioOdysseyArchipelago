#include "Archipelago/TLSHooks.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <nn/os.h>
#include <nn/socket.h>

// ===================== Platform glue for mbedTLS =====================
// Ported verbatim from BTT-Studio-Skybox-Build's https/HttpsClient.cpp - this
// target has no OS-level RNG or wall clock reachable from a mod, so
// mbedTLS's usual platform hooks (MBEDTLS_ENTROPY_HARDWARE_ALT /
// MBEDTLS_PLATFORM_TIME_ALT / MBEDTLS_PLATFORM_MS_TIME_ALT, all set in this
// project's mbedtls_config.h, itself copied from that same source) are
// wired to these instead. The function names below are load-bearing - they
// match exactly what mbedtls_config.h's MBEDTLS_PLATFORM_PRINTF_MACRO /
// FPRINTF_MACRO macros expand to, not just a naming convention.
//
// mbedtls_printf/fprintf are redirected to safe no-ops: this is a
// freestanding target with no real stdout, and a release build's LTO pass
// can synthesize printf/puts calls from format-string patterns even in code
// that looks inert at the source level, so the real libc printf family is
// never safe to let onto the link line here.

extern "C" int mbedtls_noop_printf(const char*, ...) {
    return 0;
}

extern "C" int mbedtls_noop_fprintf(void*, const char*, ...) {
    return 0;
}

extern "C" int mbedtls_hardware_poll(void*, unsigned char* output, size_t len, size_t* olen) {
    for (size_t i = 0; i < len; i++) {
        u64 tick = nn::os::GetSystemTick().value;
        u64 addr = reinterpret_cast<u64>(&tick) + i;
        output[i] = static_cast<unsigned char>((tick ^ addr) >> ((i % 8) * 8));
    }
    *olen = len;
    return 0;
}

static mbedtls_time_t nxFixedTime(mbedtls_time_t* out) {
    // Arbitrary fixed point in time - we skip certificate date-validity checks
    // (MBEDTLS_SSL_VERIFY_NONE below) so this only needs to be "plausible", not correct.
    mbedtls_time_t fixed = 0x5af2a056;
    if (out)
        *out = fixed;
    return fixed;
}

extern "C" mbedtls_ms_time_t mbedtls_ms_time(void) {
    // nn::os::ConvertToTimeSpan (used by BTT-Studio's original version of this
    // function) isn't actually linkable in this project's environment - same
    // class of gap as GetSystemTickFrequency not being available either (see
    // MoonRando.cpp's elapsedMillisSince). Horizon's system tick is the
    // 19.2 MHz ARM generic counter, a fixed hardware constant, so convert
    // directly from the raw tick value instead of going through either SDK
    // wrapper.
    static constexpr u64 kSystemTickFrequency = 19200000ull;
    const u64 ticks = nn::os::GetSystemTick().value;
    return static_cast<mbedtls_ms_time_t>(ticks / (kSystemTickFrequency / 1000));
}

void ensurePlatformInitialized() {
    if (sPlatformInitialized)
        return;
    sPlatformInitialized = true;
    mbedtls_platform_set_time(nxFixedTime);
}

// ===================== Socket bio callbacks =====================

// MBEDTLS_ERR_NET_* codes only exist when MBEDTLS_NET_C is enabled (disabled here - this is
// our own bio layer over nn::socket, not mbedTLS's bundled POSIX net module), so a plain
// mbedTLS-generic error code is used instead.
int nxSslSend(void* ctx, const unsigned char* buf, size_t len) {
    s32 fd = *static_cast<s32*>(ctx);
    s32 sent = nn::socket::Send(fd, buf, len, 0);
    if (sent < 0)
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    return sent;
}

int nxSslRecv(void* ctx, unsigned char* buf, size_t len) {
    s32 fd = *static_cast<s32*>(ctx);
    s32 received = nn::socket::Recv(fd, buf, len, 0);
    if (received < 0)
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    return received;
}