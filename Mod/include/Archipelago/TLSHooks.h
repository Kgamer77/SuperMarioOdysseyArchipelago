#pragma once

#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>

extern "C" int mbedtls_noop_printf(const char*, ...);

extern "C" int mbedtls_noop_fprintf(void*, const char*, ...);

extern "C" int mbedtls_hardware_poll(void*, unsigned char* output, size_t len, size_t* olen);

static mbedtls_time_t nxFixedTime(mbedtls_time_t* out);

extern "C" mbedtls_ms_time_t mbedtls_ms_time(void);

inline bool sPlatformInitialized = false;

void ensurePlatformInitialized();

int nxSslSend(void* ctx, const unsigned char* buf, size_t len);

int nxSslRecv(void* ctx, unsigned char* buf, size_t len);