/*
 * Copyright (c) 2020 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ability_service_manager.h"

#include <log.h>

#include "ability_kit_command.h"
#include "ability_service_interface.h"
#include "abilityms_client.h"
#include "ohos_errno.h"
#include "rpc_errno.h"
#include "securec.h"
#include "utils.h"
#include "want.h"

namespace OHOS {
AbilityServiceManager::~AbilityServiceManager()
{
    for (const auto &storeArgs : storeList_) {
        if (storeArgs == nullptr) {
            continue;
        }
        if (storeArgs->sid != nullptr) {
            delete storeArgs->sid;
        }
        delete storeArgs;
    }
    storeList_.clear();
}

int AbilityServiceManager::ConnectAbility(const Want &want, const IAbilityConnection &conn,
                                          uint64_t token, void *storeArg)
{
    if (conn.OnAbilityDisconnectDone == nullptr || conn.OnAbilityConnectDone == nullptr) {
        HILOG_INFO(HILOG_MODULE_APP, "IAbilityConnection callback func is null");
        return ERR_INVALID_PARAM;
    }

    StoreArgs *storeArgs = AddStoreArgs(conn, storeArg);
    if (storeArgs == nullptr) {
        return ERR_INVALID_PARAM;
    }
    AbilityMsClient::GetInstance().Initialize();

    objectStub_.func = ConnectAbilityCallBack;
    objectStub_.args = (void*)storeArgs;
    objectStub_.isRemote = false;
    storeArgs->sid->handle = IPC_INVALID_HANDLE;
    storeArgs->sid->token = SERVICE_TYPE_ANONYMOUS;
    storeArgs->sid->cookie = reinterpret_cast<uintptr_t>(&objectStub_);

    int32_t result = AbilityMsClient::GetInstance().ScheduleAms(&want, token, storeArgs->sid, CONNECT_ABILITY);
    if (result != EC_SUCCESS) {
        RemoveStoreArgs(nullptr, storeArgs);
        delete storeArgs->sid;
        delete storeArgs;
    }
    return result;
}

int AbilityServiceManager::DisconnectAbility(const IAbilityConnection &conn, uint64_t token)
{
    StoreArgs *storeArgs = RemoveStoreArgs(&conn, nullptr);
    if (storeArgs == nullptr) {
        HILOG_INFO(HILOG_MODULE_APP, "no need to disconnect");
        return ERR_INVALID_PARAM;
    }
    int result = AbilityMsClient::GetInstance().ScheduleAms(nullptr, token, storeArgs->sid, DISCONNECT_ABILITY);

    ClearStore(storeArgs);
    return result;
}

StoreArgs *AbilityServiceManager::AddStoreArgs(const IAbilityConnection &conn, void *storeArg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (GetStoreArgs(conn) != nullptr) {
        HILOG_ERROR(HILOG_MODULE_APP, "func has already used");
        return nullptr;
    }
    StoreArgs *storeArgs = new StoreArgs();
    storeArgs->conn = &conn;
    storeArgs->storeArg = storeArg;
    storeArgs->sid = new SvcIdentity();
    storeList_.emplace_back(storeArgs);

    return storeArgs;
}

StoreArgs *AbilityServiceManager::GetStoreArgs(const IAbilityConnection &conn) const
{
    for (const auto storeArgs : storeList_) {
        if (storeArgs->conn == &conn) {
            return storeArgs;
        }
    }
    return nullptr;
}

StoreArgs *AbilityServiceManager::RemoveStoreArgs(const IAbilityConnection *conn, StoreArgs *storeArgs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (storeArgs == nullptr) {
        storeArgs = GetStoreArgs(*conn);
    }
    storeList_.remove(storeArgs);

    return storeArgs;
}

int32_t AbilityServiceManager::ConnectAbilityCallBack(uint32_t code, IpcIo *data, IpcIo *reply, MessageOption option)
{
    // param check
    StoreArgs *storeArgs = static_cast<StoreArgs *>(option.args); 
    if (storeArgs == nullptr || storeArgs->conn == nullptr ||
        storeArgs->conn->OnAbilityConnectDone == nullptr || storeArgs->conn->OnAbilityDisconnectDone == nullptr) {
        HILOG_ERROR(HILOG_MODULE_APP, "storeArgs or callback func or ipcMsg is null");
        AbilityServiceManager::GetInstance().RemoveStoreArgs(nullptr, storeArgs);
        ClearStore(storeArgs);
        return ERR_INVALID_PARAM;
    }

    int resultCode = (code != SCHEDULER_ABILITY_CONNECT_FAIL) ? 0 : -1;

    SvcIdentity *serviceSid = nullptr;
    SvcIdentity svc;
    if (code == SCHEDULER_ABILITY_CONNECT) {
        // parse service sid
        bool ret = ReadRemoteObject(data, &svc);
        if (ret) {
            serviceSid = &svc;
        }
    }

    if ((code == SCHEDULER_ABILITY_CONNECT) && (serviceSid == nullptr)) {
        resultCode = -1;
    }
    // parse element
    ElementName elementName = { nullptr };
    if (!DeserializeElement(&elementName, data)) {
        resultCode = -1;
    }
    if (code == SCHEDULER_ABILITY_DISCONNECT) {
        storeArgs->conn->OnAbilityDisconnectDone(&elementName, resultCode, storeArgs->storeArg);
    } else {
        storeArgs->conn->OnAbilityConnectDone(&elementName, serviceSid, resultCode,  storeArgs->storeArg);
    }
    if (code != SCHEDULER_ABILITY_CONNECT || resultCode == -1) {
        AbilityServiceManager::GetInstance().RemoveStoreArgs(nullptr, storeArgs);
        ClearStore(storeArgs);
    }

    ClearElement(&elementName);
    return ERR_NONE;
}

void AbilityServiceManager::ClearStore(StoreArgs *storeArgs)
{
    if ((storeArgs == nullptr) || (storeArgs->sid == nullptr)) {
        HILOG_INFO(HILOG_MODULE_APP, "no need to clear storeArgs");
        return;
    }
    delete storeArgs->sid;
    delete storeArgs;
}
} // namespace OHOS
