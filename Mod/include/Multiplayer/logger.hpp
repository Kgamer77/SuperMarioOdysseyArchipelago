#pragma once

#include "Multiplayer/SocketBase.hpp"

class Logger : public SocketBase {
public:
    Logger(const char* ip, u16 port, const char* name)
        : SocketBase(name) { this->init(ip, port); };
    bool init(const char* ip, u16 port);

    static void createInstance();
    static void setLogName(const char* name) {
        if (sInstance)
            sInstance->setName(name);
    }
    static void log(const char* fmt, ...);

    static void enableName() {
        if (sInstance)
            sInstance->isDisableName = false;
    }
    static void disableName() {
        if (sInstance)
            sInstance->isDisableName = true;
    }

private:
    static Logger* sInstance;
    bool isDisableName;
};