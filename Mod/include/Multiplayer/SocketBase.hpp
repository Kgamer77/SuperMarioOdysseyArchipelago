#pragma once

#include "sead/prim/seadSafeString.h"

#include <cstring>

enum class SockState
{
    UNINITIALIZED = 0,
    CONNECTED = 1,
    UNAVAILABLE = 2,
    DISCONNECTED = 3,
    NONET = 4,
    INVALIP = 5,
    CONNFAIL = 6
};

class SocketBase {
public:
    SocketBase(const char* name);

    const char* getStateChar();
    SockState getLogState();

    void set_sock_flags(int flags);

    const char* getIP() { return this->mSockIp.cstr(); }
    u16 getPort() { return this->mPort; }
    void setName(const char* name) { strcpy(mSockName, name); };
    u32 mSockErrno;

protected:
    s32 socket_log(const char* str);

    char mSockName[0x10] = {};
    sead::FixedSafeString<64> mSockIp;

    u16 mPort;
    SockState mSockState = SockState::UNINITIALIZED;
    // -1 means "no socket open". Was uninitialised, which mattered once
    // closeSocket() started trusting it: an unconnected SocketClient would
    // otherwise try to shut down and close whatever integer happened to be in
    // this field.
    s32 mSockFd = -1;

    int mSockFlags;
};
