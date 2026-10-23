#include "Randomizer/HttpsClient.h"

#include "Randomizer/Randomizer.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Archipelago/TLSHooks.h"
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

// extern "C" int mbedtls_noop_printf(const char*, ...) {
//     return 0;
// }

// extern "C" int mbedtls_noop_fprintf(void*, const char*, ...) {
//     return 0;
// }

// extern "C" int mbedtls_hardware_poll(void*, unsigned char* output, size_t len, size_t* olen) {
//     for (size_t i = 0; i < len; i++) {
//         u64 tick = nn::os::GetSystemTick().value;
//         u64 addr = reinterpret_cast<u64>(&tick) + i;
//         output[i] = static_cast<unsigned char>((tick ^ addr) >> ((i % 8) * 8));
//     }
//     *olen = len;
//     return 0;
// }

// static mbedtls_time_t nxFixedTime(mbedtls_time_t* out) {
//     // Arbitrary fixed point in time - we skip certificate date-validity checks
//     // (MBEDTLS_SSL_VERIFY_NONE below) so this only needs to be "plausible", not correct.
//     mbedtls_time_t fixed = 0x5af2a056;
//     if (out)
//         *out = fixed;
//     return fixed;
// }

// extern "C" mbedtls_ms_time_t mbedtls_ms_time(void) {
//     // nn::os::ConvertToTimeSpan (used by BTT-Studio's original version of this
//     // function) isn't actually linkable in this project's environment - same
//     // class of gap as GetSystemTickFrequency not being available either (see
//     // MoonRando.cpp's elapsedMillisSince). Horizon's system tick is the
//     // 19.2 MHz ARM generic counter, a fixed hardware constant, so convert
//     // directly from the raw tick value instead of going through either SDK
//     // wrapper.
//     static constexpr u64 kSystemTickFrequency = 19200000ull;
//     const u64 ticks = nn::os::GetSystemTick().value;
//     return static_cast<mbedtls_ms_time_t>(ticks / (kSystemTickFrequency / 1000));
// }

// static bool sPlatformInitialized = false;

// static void ensurePlatformInitialized() {
//     if (sPlatformInitialized)
//         return;
//     sPlatformInitialized = true;
//     mbedtls_platform_set_time(nxFixedTime);
// }

// ===================== Socket bio callbacks =====================

// MBEDTLS_ERR_NET_* codes only exist when MBEDTLS_NET_C is enabled (disabled here - this is
// our own bio layer over nn::socket, not mbedTLS's bundled POSIX net module), so a plain
// mbedTLS-generic error code is used instead.
// static int nxSslSend(void* ctx, const unsigned char* buf, size_t len) {
//     s32 fd = *static_cast<s32*>(ctx);
//     s32 sent = nn::socket::Send(fd, buf, len, 0);
//     if (sent < 0)
//         return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
//     return sent;
// }

// static int nxSslRecv(void* ctx, unsigned char* buf, size_t len) {
//     s32 fd = *static_cast<s32*>(ctx);
//     s32 received = nn::socket::Recv(fd, buf, len, 0);
//     if (received < 0)
//         return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
//     return received;
// }

// ===================== Shared connect+handshake ========================
// Plain functions rather than an RAII wrapper deliberately: httpsRequest's
// original goto-based cleanup (ported as-is from BTT-Studio) can't safely
// jump over a non-trivial destructor's construction.

namespace {

    struct TlsConnection {
        s32 sockFd = -1;
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config conf;
        mbedtls_ctr_drbg_context ctrDrbg;
        mbedtls_entropy_context entropy;
    };

    bool openTlsConnection(
        TlsConnection* conn, const char* host, u16 port, const char* connectIp, char* outError, size_t outErrorSize) {
        ensurePlatformInitialized();
        const char* targetIp = connectIp ? connectIp : host;

        conn->sockFd = nn::socket::Socket(AF_INET, SOCK_STREAM, 0);
        if (conn->sockFd < 0) {
            snprintf(outError, outErrorSize, "Failed to create socket");
            return false;
        }

        in_addr hostAddress = {};
        nn::socket::InetAton(targetIp, &hostAddress);
        sockaddr_in serverAddr = {};
        serverAddr.sin_addr = hostAddress;
        serverAddr.sin_family = nn::socket::InetHtons(AF_INET);
        serverAddr.sin_port = nn::socket::InetHtons(port);

        if (nn::socket::Connect(conn->sockFd, (sockaddr*)&serverAddr, sizeof(serverAddr)).IsFailure()) {
            nn::socket::Close(conn->sockFd);
            conn->sockFd = -1;
            snprintf(outError, outErrorSize, "TCP connect to %s:%d failed", host, port);
            return false;
        }

        mbedtls_ssl_init(&conn->ssl);
        mbedtls_ssl_config_init(&conn->conf);
        mbedtls_ctr_drbg_init(&conn->ctrDrbg);
        mbedtls_entropy_init(&conn->entropy);

        s32 result = mbedtls_ctr_drbg_seed(
            &conn->ctrDrbg, mbedtls_entropy_func, &conn->entropy, (const unsigned char*)"smo-randomizer",
            strlen("smo-randomizer"));
        if (result == 0) {
            result = mbedtls_ssl_config_defaults(
                &conn->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        }
        if (result != 0) {
            snprintf(outError, outErrorSize, "SSL setup failed: %d", result);
            mbedtls_ssl_free(&conn->ssl);
            mbedtls_ssl_config_free(&conn->conf);
            mbedtls_ctr_drbg_free(&conn->ctrDrbg);
            mbedtls_entropy_free(&conn->entropy);
            nn::socket::Close(conn->sockFd);
            conn->sockFd = -1;
            return false;
        }

        // No cert-chain verification - we're talking to one specific known host, not browsing.
        mbedtls_ssl_conf_authmode(&conn->conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&conn->conf, mbedtls_ctr_drbg_random, &conn->ctrDrbg);

        result = mbedtls_ssl_setup(&conn->ssl, &conn->conf);
        if (result == 0) {
            mbedtls_ssl_set_hostname(&conn->ssl, host);
            mbedtls_ssl_set_bio(&conn->ssl, &conn->sockFd, nxSslSend, nxSslRecv, nullptr);
            result = mbedtls_ssl_handshake(&conn->ssl);
        }
        if (result != 0) {
            char errBuf[128];
            mbedtls_strerror(result, errBuf, sizeof(errBuf));
            snprintf(outError, outErrorSize, "TLS handshake failed: %s", errBuf);
            mbedtls_ssl_free(&conn->ssl);
            mbedtls_ssl_config_free(&conn->conf);
            mbedtls_ctr_drbg_free(&conn->ctrDrbg);
            mbedtls_entropy_free(&conn->entropy);
            nn::socket::Close(conn->sockFd);
            conn->sockFd = -1;
            return false;
        }

        return true;
    }

    void closeTlsConnection(TlsConnection* conn) {
        mbedtls_ssl_close_notify(&conn->ssl);
        nn::socket::Close(conn->sockFd);
        mbedtls_ssl_free(&conn->ssl);
        mbedtls_ssl_config_free(&conn->conf);
        mbedtls_ctr_drbg_free(&conn->ctrDrbg);
        mbedtls_entropy_free(&conn->entropy);
    }

    bool tlsWriteAll(mbedtls_ssl_context* ssl, const char* data, s32 len) {
        s32 written = 0;
        while (written < len) {
            s32 n = mbedtls_ssl_write(ssl, (const unsigned char*)data + written, len - written);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE)
                continue;
            if (n <= 0)
                return false;
            written += n;
        }
        return true;
    }

} // namespace

namespace https {

    bool httpsRequest(
        const char* method, const char* host, u16 port, const char* path, const char* extraHeaders,
        const char* jsonBody, char* outResponse, size_t outResponseSize, const char* connectIp, s32* outStatusCode) {
        outResponse[0] = '\0';
        if (outStatusCode)
            *outStatusCode = 0;

        TlsConnection conn;
        if (!openTlsConnection(&conn, host, port, connectIp, outResponse, outResponseSize))
            return false;

        bool ok = false;

        // Sized generously for a Firestore request: a JSON document body plus headers can run
        // well past a few hundred bytes.
        char request[0x2000];
        s32 requestLen = snprintf(
            request, sizeof(request),
            "%s %s HTTP/1.1\r\nHost: %s\r\n%sContent-Length: %d\r\nConnection: close\r\n\r\n%s", method, path,
            host, extraHeaders, (s32)strlen(jsonBody), jsonBody);

        if (!tlsWriteAll(&conn.ssl, request, requestLen)) {
            snprintf(outResponse, outResponseSize, "TLS write failed");
            closeTlsConnection(&conn);
            return false;
        }

        char response[0x2000] = {};
        s32 totalRead = 0;
        s32 readLen = 0;
        do {
            readLen = mbedtls_ssl_read(&conn.ssl, (unsigned char*)response + totalRead, sizeof(response) - 1 - totalRead);
            if (readLen == MBEDTLS_ERR_SSL_WANT_READ || readLen == MBEDTLS_ERR_SSL_WANT_WRITE)
                continue;
            if (readLen <= 0)
                break;
            totalRead += readLen;
        } while (totalRead < (s32)sizeof(response) - 1);

        if (totalRead <= 0) {
            snprintf(outResponse, outResponseSize, "No response after TLS handshake");
        } else {
            response[totalRead] = '\0';

            s32 statusCode = 0;
            const char* statusStart = strstr(response, "HTTP/1.1 ");
            if (statusStart)
                statusCode = atoi(statusStart + strlen("HTTP/1.1 "));
            if (outStatusCode)
                *outStatusCode = statusCode;
            ok = statusCode >= 200 && statusCode < 300;

            const char* bodyStart = strstr(response, "\r\n\r\n");
            snprintf(outResponse, outResponseSize, "%s", bodyStart ? bodyStart + 4 : response);
        }

        closeTlsConnection(&conn);
        if (!ok)
            Randomizer::logPush("https: request to %s%s failed: %s", host, path, outResponse);
        return ok;
    }

} // namespace https
