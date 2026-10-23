#include "Multiplayer/SocketBase.hpp"

#include "nn/socket.h"

#include <cstring>

SocketBase::SocketBase(const char* name) {
    strcpy(mSockName, name);
    mSockFlags = 0;
}

const char* SocketBase::getStateChar() {
    switch (mSockState) {
    case SockState::CONNECTED:
        return "Socket Connected";
    case SockState::UNAVAILABLE:
        return "Socket Unavailable";
    case SockState::UNINITIALIZED:
        return "Socket Uninitialized";
    case SockState::DISCONNECTED:
        return "Socket Disconnected";
    case SockState::CONNFAIL:
        return "Connection Failed";
    case SockState::INVALIP:
        return "Invalid IP or hostname";
    case SockState::NONET:
        return "Network not available";
    default:
        return "Unknown State";
    }
}

SockState SocketBase::getLogState() {
    return mSockState;
}

void SocketBase::set_sock_flags(int flags) {
    mSockFlags = flags;
}

s32 SocketBase::socket_log(const char* str) {
    if (mSockState != SockState::CONNECTED)
        return -1;

    return nn::socket::Send(mSockFd, str, strlen(str), 0);
}
