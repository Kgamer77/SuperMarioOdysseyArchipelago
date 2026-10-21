/**
 * @file account.h
 * @brief Account service implementation.
 */

#pragma once

#include "hk/diag/diag.h"

#include <nn/os.h>
#include <nn/types.h>

namespace nn {
    namespace account {
        struct Nickname {
            char m_Buffer[0x21];
        };
        struct NetworkServiceAccountId {
            u64 m_Id;
        };

        class AsyncContext;

        class Uid {
        public:
            bool IsValid() const { return m_Storage[0] != 0 || m_Storage[1] != 0; }

            u64 m_Storage[2];

            // custom funcs
            bool operator==(const Uid& rhs) const { return m_Storage[0] == rhs.m_Storage[0] && m_Storage[1] == rhs.m_Storage[1]; }

            Uid& operator=(const Uid& other) {
                m_Storage[0] = other.m_Storage[0];
                m_Storage[1] = other.m_Storage[1];
                return *this;
            }

            inline bool isEmpty() const { return !IsValid(); }

            inline void print() const { hk::diag::logLine("%lu%lu", m_Storage[0], m_Storage[1]); }

            inline void print(const char* prefix) { hk::diag::logLine("%s: %lu%lu", prefix, m_Storage[0], m_Storage[1]); }
        };

        class UserHandle {
        public:
            Uid m_Uid;
            void* m_Handle;
        };

        void Initialize();
        Result ListAllUsers(s32*, Uid*, s32 numUsers);
        Result OpenUser(UserHandle*, const Uid&);
        Result IsNetworkServiceAccountAvailable(bool* out, const UserHandle&);
        void CloseUser(const UserHandle&);

        Result EnsureNetworkServiceAccountAvailable(const UserHandle& userHandle);
        Result EnsureNetworkServiceAccountIdTokenCacheAsync(AsyncContext*, const UserHandle&);
        Result LoadNetworkServiceAccountIdTokenCache(u64*, char*, u64, const UserHandle&);

        Result GetLastOpenedUser(Uid*);
        Result GetNickname(Nickname* nickname, const Uid& userID);
        Result GetNetworkServiceAccountId(NetworkServiceAccountId*, const UserHandle&);

        Result GetUserId(Uid* uid, const UserHandle& handle);
        Result OpenPreselectedUser(UserHandle* handle);

        class AsyncContext {
        public:
            AsyncContext();
            ~AsyncContext();

            Result HasDone(bool*);
            Result GetResult();
            Result Cancel();
            Result GetSystemEvent(nn::os::SystemEvent*);
        };
    } // namespace account
} // namespace nn
