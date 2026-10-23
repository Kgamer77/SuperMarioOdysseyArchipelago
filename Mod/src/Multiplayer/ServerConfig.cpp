#include "Multiplayer/ServerConfig.h"

#include "nn/fs.h"

#include "SDRedirect/SDPaths.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Same guard-with-fallback shape Client.cpp used to define locally for
// SMOR_MP_SERVER_IP/SMOR_MP_SERVER_PORT (CMakeLists.txt's cache variables,
// defaulting to the real public address) - this is now the one place that
// default lives; Client.cpp reads through ServerConfig::getIp()/getPort()
// instead of defining its own copy.
#ifndef SMOR_MP_SERVER_IP
#define SMOR_MP_SERVER_IP "97.81.8.12"
#endif
#ifndef SMOR_MP_SERVER_PORT
#define SMOR_MP_SERVER_PORT 1027
#endif

namespace ServerConfig {
    namespace {
        constexpr const char* cConfigPath = "sd:/SMO-Randomizer/server.ini";
        constexpr size_t cIpBufferSize = 64;
        // Generous for two "key=value" lines plus a couple of comment lines -
        // real content is always well under this.
        constexpr long cMaxFileSize = 0x400;

        char sIp[cIpBufferSize] = SMOR_MP_SERVER_IP;
        int sPort = SMOR_MP_SERVER_PORT;

        // Session override storage, deliberately separate from sIp/sPort above
        // so load() (called on every connect) can rewrite the file-backed
        // values without touching these. See the header for why.
        char sSessionIp[cIpBufferSize] = {};
        bool sHasSessionIp = false;
        int sSessionPort = 0;

        // See the header: off unless a card opts in.
        bool sNonBlockingRecv = false;

        void copyIp(const char* ip) {
            if (!ip || !ip[0])
                return;
            std::strncpy(sIp, ip, cIpBufferSize - 1);
            sIp[cIpBufferSize - 1] = '\0';
        }

        // Trims trailing \r/\n/spaces so a file edited on Windows (CRLF) or
        // with trailing whitespace still parses cleanly.
        void trimTrailing(char* str) {
            size_t len = std::strlen(str);
            while (len > 0
                && (str[len - 1] == '\r' || str[len - 1] == '\n' || str[len - 1] == ' '))
                str[--len] = '\0';
        }

        // Minimal key=value line scanner - no sections, no quoting, matches
        // the "plain enough to hand-edit" ask this file exists for.
        void parseLine(char* line) {
            trimTrailing(line);
            if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
                return;

            char* eq = std::strchr(line, '=');
            if (!eq)
                return;
            *eq = '\0';
            const char* key = line;
            const char* value = eq + 1;

            if (std::strcmp(key, "ip") == 0)
                copyIp(value);
            else if (std::strcmp(key, "port") == 0 && value[0])
                sPort = std::atoi(value);
            else if (std::strcmp(key, "nonblocking_recv") == 0 && value[0])
                sNonBlockingRecv = std::atoi(value) != 0;
        }

        bool writeDefaultFile() {
            SDPaths::ensureRandomizerRoot();
            nn::fs::DeleteFile(cConfigPath);

            char buf[512];
            const int len = std::snprintf(buf, sizeof(buf),
                "# SMO-Randomizer multiplayer server config.\n"
                "# Edit these to point at a different relay server - no\n"
                "# recompile needed. An in-game override (ModConfig's\n"
                "# Multiplayer page) rewrites this file too.\n"
                "ip=%s\n"
                "port=%d\n"
                "\n"
                "# Emulator workaround. Leave at 0 on real hardware.\n"
                "# On Ryujinx a blocking receive also blocks sends on the same\n"
                "# socket, so the client cannot send anything once connected.\n"
                "# Set to 1 there. Hardware does not have this problem and is\n"
                "# left on the code path that already works.\n"
                "nonblocking_recv=%d\n",
                sIp, sPort, sNonBlockingRecv ? 1 : 0);
            if (len <= 0)
                return false;

            if (nn::fs::CreateFile(cConfigPath, len).IsFailure())
                return false;

            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, cConfigPath, nn::fs::OpenMode_Write).IsFailure())
                return false;

            const bool ok = nn::fs::WriteFile(handle, 0, buf, len,
                nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush))
                                .IsSuccess();
            nn::fs::CloseFile(handle);
            return ok;
        }

        bool readFile() {
            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, cConfigPath, nn::fs::OpenMode_Read).IsFailure())
                return false;

            long fileSize = 0;
            const bool sizeOk = nn::fs::GetFileSize(&fileSize, handle).IsSuccess()
                && fileSize > 0 && fileSize <= cMaxFileSize;

            char buf[cMaxFileSize + 1] = {};
            const bool readOk = sizeOk
                && nn::fs::ReadFile(handle, 0, buf, static_cast<size_t>(fileSize)).IsSuccess();
            nn::fs::CloseFile(handle);
            if (!readOk)
                return false;

            buf[fileSize] = '\0';

            char* line = buf;
            while (line && *line) {
                char* next = std::strchr(line, '\n');
                if (next)
                    *next = '\0';
                parseLine(line);
                line = next ? next + 1 : nullptr;
            }
            return true;
        }
    } // namespace

    void load() {
        if (!readFile())
            writeDefaultFile();
    }

    // A session override wins over whatever load() last read from the file.
    const char* getIp() { return sHasSessionIp ? sSessionIp : sIp; }
    int getPort() { return sSessionPort > 0 ? sSessionPort : sPort; }

    const char* getFileIp() { return sIp; }
    int getFilePort() { return sPort; }

    bool useNonBlockingRecv() { return sNonBlockingRecv; }

    bool hasSessionOverride() { return sHasSessionIp || sSessionPort > 0; }

    bool commitSessionOverride() {
        if (sHasSessionIp)
            copyIp(sSessionIp);
        if (sSessionPort > 0)
            sPort = sSessionPort;

        if (!writeDefaultFile())
            return false;

        // Only drop the session layer once the write actually succeeded,
        // otherwise a failed save would silently revert the player's typed
        // address back to the stale file value.
        sSessionIp[0] = 0;
        sHasSessionIp = false;
        sSessionPort = 0;
        return true;
    }

    void setSessionOverride(const char* ip, int port) {
        if (ip && ip[0]) {
            std::strncpy(sSessionIp, ip, cIpBufferSize - 1);
            sSessionIp[cIpBufferSize - 1] = 0;
            sHasSessionIp = true;
        }
        if (port > 0)
            sSessionPort = port;
    }

    void setOverride(const char* ip, int port) {
        if (ip && ip[0])
            copyIp(ip);
        if (port > 0)
            sPort = port;
        writeDefaultFile();
    }

} // namespace ServerConfig
