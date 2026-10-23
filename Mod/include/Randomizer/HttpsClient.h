#pragma once

#include <cstddef>
#include <nn/types.h>

namespace https {

    // General-purpose HTTPS request over a real TLS 1.2 connection (mbedTLS, custom entropy/time
    // hooks - no OS RNG/clock available on this target, and no cert-chain verification, since this
    // is meant for talking to specific known hosts rather than general browsing). Ported from
    // BTT-Studio-Skybox-Build's https/HttpsClient (see that project for the original, more heavily
    // exercised version - Firestore/Firebase Auth calls there, Firestore calls here). Blocks the
    // calling thread for the handshake + request/response.
    //
    // host is used for the HTTP Host header and the TLS SNI - it's a real hostname, not an IP,
    // wherever the target host has one. Since nn::socket::InetAton (used internally) does no DNS
    // resolution, an IP to actually connect() to must be supplied via connectIp; Google's frontend
    // serving infra (and presumably most others fronted by a CDN/load balancer) routes by Host/SNI
    // rather than destination IP, so this lets a resolved-once IP stand in for real DNS. Pass
    // nullptr (the default) when host is already a literal IP with nothing to distinguish - the
    // connection then targets host directly, as before.
    //
    // extraHeaders is a caller-formatted block of header lines (each ending "\r\n") appended after
    // Host - this must include a Content-Type of the caller's choosing (there's no default), plus
    // anything else needed like "Authorization: Bearer xyz\r\n". body may be "" for a bodyless
    // request (GET) - a zero-length Content-Length is still sent, which every server here tolerates.
    //
    // outResponse always receives the raw response body (everything after the blank line),
    // regardless of status code, truncated to outResponseSize - 1. outStatusCode (if non-null)
    // receives the parsed HTTP status code, or 0 if no valid status line was found (e.g. the
    // connection/handshake itself failed - outResponse gets a diagnostic message in that case
    // instead of a body). Returns true only for a 2xx status.
    bool httpsRequest(
        const char* method, const char* host, u16 port, const char* path, const char* extraHeaders,
        const char* jsonBody, char* outResponse, size_t outResponseSize, const char* connectIp = nullptr,
        s32* outStatusCode = nullptr);

} // namespace https
