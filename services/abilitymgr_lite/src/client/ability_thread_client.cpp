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

#include "client/ability_thread_client.h"

#define __STDC_FORMAT_MACROS
#include <cinttypes>

#include "ability_kit_command.h"
#include "adapter.h"
#include "app_manager.h"
#include "app_record.h"
#include "client/bundlems_client.h"
#include "element_name_utils.h"
#include "ipc_skeleton.h"
#include "rpc_errno.h"
#include "securec.h"
#include "util/abilityms_helper.h"
#include "utils.h"
#include "want_utils.h"

namespace OHOS {
const int MAX_MODULE_SIZE = 16;
AbilityThreadClient::AbilityThreadClient(uint64_t token, pid_t pid, const SvcIdentity &svcIdentity,
    OnRemoteDead handler) : token_(token), pid_(pid), svcIdentity_(svcIdentity), deathHandler_(handler)
{
}

AbilityThreadClient::AbilityThreadClient(const AbilityThreadClient &client)
    : token_(client.token_), pid_(client.pid_), svcIdentity_(client.svcIdentity_), deathHandler_(client.deathHandler_)
{
}

AbilityThreadClient::~AbilityThreadClient()
{
}

AbilityMsStatus AbilityThreadClient::Initialize(const char *bundleName)
{
    if (bundleName == nullptr) {
        return AbilityMsStatus::AppTransanctStatus("bundleName is null");
    }
    AppInfo *appInfo = new AppInfo();
    appInfo->bundleName = Utils::Strdup(bundleName);
    if (appInfo->bundleName == nullptr) {
        delete appInfo;
        return AbilityMsStatus::AppTransanctStatus("memory alloc fail");
    }
    appInfo->svcIdentity = svcIdentity_;
    if (AddDeathRecipient(svcIdentity_, deathHandler_, appInfo, &cbid_) != 0) {
        AdapterFree(appInfo->bundleName);
        delete appInfo;
        return AbilityMsStatus::AppTransanctStatus("register death callback ipc error");
    }
    PRINTD("AbilityThreadClient", "token(%{private}" PRIu64 ") bundleName(%{public}s) success",
        token_, appInfo->bundleName);
    return AbilityMsStatus::Ok();
}

uint64_t AbilityThreadClient::GetToken() const
{
    return token_;
}

pid_t AbilityThreadClient::GetPid() const
{
    return pid_;
}

const SvcIdentity& AbilityThreadClient::GetSvcIdentity() const
{
    return svcIdentity_;
}

AbilityMsStatus AbilityThreadClient::AbilityTransaction(const TransactionState &state,
    const Want &want, AbilityType abilityType) const
{
    PRINTD("AbilityThreadClient", "start");
    IpcIo req;
    char data[MAX_IO_SIZE];
    IpcIoInit(&req, data, MAX_IO_SIZE, MAX_OBJECTS);
    WriteInt32(&req, state.state);
    WriteUint64(&req, state.token);
    WriteInt32(&req, abilityType);
    if (!SerializeWant(&req, &want)) {
        return AbilityMsStatus::AppTransanctStatus("SerializeWant failed");
    }
    MessageOption option;
    MessageOptionInit(&option);
    option.flags = TF_OP_ASYNC;
    int32_t ret = SendRequest(svcIdentity_, SCHEDULER_ABILITY_LIFECYCLE, &req,
        nullptr, option, nullptr);
    if (ret != ERR_NONE) {
        return AbilityMsStatus::AppTransanctStatus("lifecycle ipc error");
    }
    return AbilityMsStatus::Ok();
}

AbilityMsStatus AbilityThreadClient::AppInitTransaction(const BundleInfo &bundleInfo)
{
    PRINTD("AbilityThreadClient", "start");
    if (bundleInfo.bundleName == nullptr || bundleInfo.codePath == nullptr ||
        bundleInfo.numOfModule > MAX_MODULE_SIZE) {
        return AbilityMsStatus::AppTransanctStatus("app init invalid argument");
    }
    IpcIo req;
    char data[MAX_IO_SIZE];
    IpcIoInit(&req, data, MAX_IO_SIZE, 0);
    WriteString(&req, bundleInfo.bundleName);
    WriteString(&req, bundleInfo.codePath);
    WriteString(&req, bundleInfo.dataPath);
    WriteBool(&req, bundleInfo.isNativeApp);
    // transact moduleName
    WriteInt32(&req, bundleInfo.numOfModule);
    for (int i = 0; i < bundleInfo.numOfModule; i++) {
        if (bundleInfo.moduleInfos[i].moduleName != nullptr) {
            WriteString(&req, bundleInfo.moduleInfos[i].moduleName);
        }
    }
    IpcIo reply;
    uintptr_t ptr;
    MessageOption option;
    MessageOptionInit(&option);
    if (SendRequest(svcIdentity_, SCHEDULER_APP_INIT, &req,
        &reply, option, &ptr) != ERR_NONE) {
        return  AbilityMsStatus::AppTransanctStatus("app init ipc error");
    }
    FreeBuffer((void *)ptr);
    return AbilityMsStatus::Ok();
}

AbilityMsStatus AbilityThreadClient::AppExitTransaction()
{
    PRINTD("AbilityThreadClient", "start");
    MessageOption option;
    MessageOptionInit(&option);
    option.flags = TF_OP_ASYNC;
    if (SendRequest(svcIdentity_, SCHEDULER_APP_EXIT, nullptr,
        nullptr, option, nullptr) != ERR_NONE) {
        return AbilityMsStatus::AppTransanctStatus("app exit ipc error");
    }
    return AbilityMsStatus::Ok();
}

AbilityMsStatus AbilityThreadClient::ConnectAbility(const Want &want, uint64_t token) const
{
    PRINTD("AbilityThreadClient", "connect");
    IpcIo req;
    char data[MAX_IO_SIZE];
    IpcIoInit(&req, data, MAX_IO_SIZE, MAX_OBJECTS);
    WriteUint64(&req, token);
    if (!SerializeWant(&req, &want)) {
        return AbilityMsStatus::TaskStatus("connectAbility", "SerializeWant failed");
    }
    MessageOption option;
    MessageOptionInit(&option);
    option.flags = TF_OP_ASYNC;
    if (SendRequest(svcIdentity_, SCHEDULER_ABILITY_CONNECT, &req, nullptr,
        option, nullptr) != ERR_NONE) {
        return AbilityMsStatus::TaskStatus("connectAbility", "connectAbility exit ipc error");
    }
    return AbilityMsStatus::Ok();
}

AbilityMsStatus AbilityThreadClient::DisconnectAbility(const Want &want, uint64_t token) const
{
    PRINTD("AbilityThreadClient", "disconnect");
    IpcIo req;
    char data[MAX_IO_SIZE];
    IpcIoInit(&req, data, MAX_IO_SIZE, MAX_OBJECTS);
    WriteUint64(&req, token);
    if (!SerializeWant(&req, &want)) {
        return AbilityMsStatus::TaskStatus("disconnectAbility", "SerializeWant failed");
    }
    MessageOption option;
    MessageOptionInit(&option);
    option.flags = TF_OP_ASYNC;
    if (SendRequest(svcIdentity_, SCHEDULER_ABILITY_DISCONNECT, &req, nullptr,
        option, nullptr) != ERR_NONE) {
        return AbilityMsStatus::TaskStatus("disconnectAbility", "disconnectAbility exit ipc error");
    }
    return AbilityMsStatus::Ok();
}

AbilityMsStatus AbilityThreadClient::ConnectAbilityDone(const Want &want, const SvcIdentity &serviceSid,
    const SvcIdentity &connectSid) const
{
    PRINTD("AbilityThreadClient", "connectDone");
    IpcIo req;
    char data[MAX_IO_SIZE];
    IpcIoInit(&req, data, MAX_IO_SIZE, 1);
    bool ret = WriteRemoteObject(&req, &serviceSid);
    if (!ret) {
        return AbilityMsStatus::TaskStatus("connectAbilityDone", "WriteRemoteObject failed");
    }
    if (!SerializeElement(&req, want.element)) {
        return AbilityMsStatus::TaskStatus("connectAbilityDone", "SerializeElement failed");
    }

    MessageOption option;
    MessageOptionInit(&option);
    option.flags = TF_OP_ASYNC;
    if (SendRequest(connectSid, SCHEDULER_ABILITY_CONNECT, &req, nullptr,
        option, nullptr) != ERR_NONE) {
        return AbilityMsStatus::TaskStatus("connectAbilityDone", "connectAbilityDone ipc error");
    }
    return AbilityMsStatus::Ok();
}

AbilityMsStatus AbilityThreadClient::DisconnectAbilityDone(const Want &want, const SvcIdentity &connectSid) const
{
    PRINTD("AbilityThreadClient", "disconnectDone");
    IpcIo req;
    char data[MAX_IO_SIZE];
    IpcIoInit(&req, data, MAX_IO_SIZE, 0);
    if (!SerializeElement(&req, want.element)) {
        return AbilityMsStatus::TaskStatus("DisconnectAbilityDone", "SerializeElement failed");
    }

    MessageOption option;
    MessageOptionInit(&option);
    option.flags = TF_OP_ASYNC;
    if (SendRequest(connectSid, SCHEDULER_ABILITY_DISCONNECT, &req,
        nullptr, option, nullptr) != ERR_NONE) {
        ReleaseSvc(connectSid);
        return AbilityMsStatus::TaskStatus("disconnectAbilityDone", "disconnectAbilityDone ipc error");
    }
    ReleaseSvc(connectSid);
    return AbilityMsStatus::Ok();
}

AbilityMsStatus AbilityThreadClient::DumpAbilityTransaction(const Want &want, uint64_t token)
{
    PRINTD("AbilityThreadClient", "start");
    IpcIo req;
    char data[MAX_IO_SIZE];
    IpcIoInit(&req, data, MAX_IO_SIZE, MAX_OBJECTS);
    if (!SerializeWant(&req, &want)) {
        return AbilityMsStatus::TaskStatus("dumpAbility", "SerializeWant failed");
    }
    MessageOption option;
    MessageOptionInit(&option);
    option.flags = TF_OP_ASYNC;
    if (SendRequest(svcIdentity_, SCHEDULER_DUMP_ABILITY, &req,
        nullptr, option, nullptr) != ERR_NONE) {
        return AbilityMsStatus::AppTransanctStatus("dump ability ipc error");
    }
    return AbilityMsStatus::Ok();
}
}
