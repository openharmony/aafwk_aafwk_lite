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

#include "ability_tool.h"

#include <cstdio>
#include <cstring>
#include <ohos_errno.h>
#include <ctime>
#include <samgr_lite.h>
#include <securec.h>
#include <semaphore.h>

#include "ability_errors.h"
#include "ability_kit_command.h"
#include "ability_manager.h"
#include "ability_service_interface.h"
#include "adapter.h"
#include "ipc_skeleton.h"
#include "want_utils.h"

namespace OHOS {
namespace {
constexpr int WAIT_TIMEOUT = 30; // 5 second
constexpr int MAX_BUFFER_SIZE_PER = 1024; // 1024 byte
constexpr char CMD_START_ABILITY[] = "start";
constexpr char CMD_STOP_ABILITY[] = "stopability";
constexpr char CMD_TERMINATE_APP[] = "terminate";
constexpr char CMD_DUMP_ABILITY[] = "dump";
} // namespace

static sem_t g_sem;

AbilityTool::~AbilityTool()
{
    ClearElement(&elementName_);
}

bool AbilityTool::SetBundleName(const char *bundleName)
{
    if (bundleName == nullptr || strlen(bundleName) == 0) {
        return false;
    }
    SetElementBundleName(&elementName_, bundleName);
    return true;
}

bool AbilityTool::SetAbilityName(const char *abilityName)
{
    if (abilityName == nullptr || strlen(abilityName) == 0) {
        return false;
    }
    SetElementAbilityName(&elementName_, abilityName);
    return true;
}

void AbilityTool::SetExtra(const char *extra)
{
    extra_ = const_cast<char *>(extra);
}

bool AbilityTool::SetCommand(const char *command)
{
    if (command == nullptr) {
        return false;
    }
    if (strcmp(command, CMD_START_ABILITY) != 0 &&
        strcmp(command, CMD_STOP_ABILITY) != 0 &&
        strcmp(command, CMD_TERMINATE_APP) != 0 &&
        strcmp(command, CMD_DUMP_ABILITY) != 0) {
        return false;
    }
    printf("receive command: %s\n", command);
    command_ = const_cast<char *>(command);
    return true;
}

void AbilityTool::SetDumpAll()
{
    dumpAll_ = true;
}

bool AbilityTool::RunCommand()
{
    if (command_ == nullptr) {
        printf("unknown command\n");
        return false;
    }
    IUnknown *iUnknown = SAMGR_GetInstance()->GetFeatureApi(AMS_SERVICE, AMS_INNER_FEATURE);
    if (iUnknown == nullptr) {
        printf("ams inner unknown is null\n");
        return false;
    }
    IClientProxy *innerProxy = nullptr;
    (void)iUnknown->QueryInterface(iUnknown, CLIENT_PROXY_VER, (void **)&innerProxy);
    if (innerProxy == nullptr) {
        printf("ams inner feature is null\n");
        return false;
    }

    objectStub_.func = AbilityTool::AaCallback;
    objectStub_.args = (void*)this;
    objectStub_.isRemote = false;
    identity_.handle = IPC_INVALID_HANDLE;
    identity_.token = SERVICE_TYPE_ANONYMOUS;
    identity_.cookie = reinterpret_cast<uintptr_t>(&objectStub_);

    bool retVal = false;
    if (strcmp(command_, CMD_START_ABILITY) == 0) {
        retVal = InnerStartAbility();
    } else if (strcmp(command_, CMD_STOP_ABILITY) == 0) {
        retVal = InnerStopAbility();
    } else if (strcmp(command_, CMD_TERMINATE_APP) == 0) {
        retVal = TerminateApp(innerProxy);
    } else if (strcmp(command_, CMD_DUMP_ABILITY) == 0) {
        retVal = Dump(innerProxy);
    } else {
        printf("unknown command: %s\n", command_);
    }
    return retVal;
}

Want* AbilityTool::BuildWant()
{
    Want *want = new Want();
    if (memset_s(want, sizeof(Want), 0, sizeof(Want)) != EOK) {
        delete want;
        return nullptr;
    }
    if (!dumpAll_) {
        if (elementName_.abilityName == nullptr || strlen(elementName_.abilityName) == 0 ||
            elementName_.bundleName == nullptr || strlen(elementName_.bundleName) == 0) {
            printf("ability name or bundle name is not entered\n");
            delete want;
            return nullptr;
        }
        SetWantElement(want, elementName_);
    }
    if (strcmp(command_, CMD_DUMP_ABILITY) == 0) {
        SetWantSvcIdentity(want, identity_);
    }
    if (extra_ != nullptr) {
        SetWantData(want, extra_, strlen(extra_) + 1);
    }
    return want;
}

bool AbilityTool::InnerStartAbility()
{
    Want *want = BuildWant();
    if (want == nullptr) {
        return false;
    }
    int ret = StartAbility(want);
    ClearWant(want);
    delete want;
    return ret == ERR_OK;
}

bool AbilityTool::InnerStopAbility()
{
    Want *want = BuildWant();
    if (want == nullptr) {
        return false;
    }
    int ret = StopAbility(want);
    ClearWant(want);
    delete want;
    return ret == ERR_OK;
}

bool AbilityTool::TerminateApp(IClientProxy *proxy) const
{
    if (proxy == nullptr) {
        return false;
    }
    if (elementName_.bundleName == nullptr || strlen(elementName_.bundleName) == 0) {
        printf("invalid argument\n");
        return false;
    }
    IpcIo req;
    char data[MAX_IO_SIZE];
    IpcIoInit(&req, data, MAX_IO_SIZE, 0);
    WriteString(&req, elementName_.bundleName);
    return proxy->Invoke(proxy, TERMINATE_APP, &req, nullptr, nullptr) == EC_SUCCESS;
}

bool AbilityTool::Dump(IClientProxy *proxy)
{
    if (proxy == nullptr) {
        return false;
    }
    Want *want = BuildWant();
    if (want == nullptr) {
        return false;
    }

    IpcIo req;
    char data[MAX_IO_SIZE];
    IpcIoInit(&req, data, MAX_IO_SIZE, MAX_OBJECTS);
    if (!SerializeWant(&req, want)) {
        printf("SerializeWant failed\n");
        ClearWant(want);
        delete want;
        return false;
    }
    ClearWant(want);
    delete want;
    if (proxy->Invoke(proxy, DUMP_ABILITY, &req, nullptr, nullptr) != EC_SUCCESS) {
        printf("dumpAbility failed\n");
        return false;
    }
    if (sem_init(&g_sem, 0, 0)) {
        printf("sem_init failed\n");
        return false;
    }
    printf("wait for callback\n");
    struct timespec ts = { 0, 0 };
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += WAIT_TIMEOUT;
    sem_timedwait(&g_sem, &ts);
    printf("sem exit\n");
    return true;
}

int32_t AbilityTool::AaCallback(uint32_t code, IpcIo *data, IpcIo *reply, MessageOption option)
{
    printf("get ability info\n");
    auto abilityTool = static_cast<AbilityTool *>(option.args);
    if (abilityTool == nullptr) {
        printf("ams call back error, abilityTool is null\n");
        return -1;
    }
    switch (code) {
        case SCHEDULER_APP_INIT: {
            ElementName element = {};
            DeserializeElement(&element, data);
            int32_t ret = 0;
            ReadInt32(data, &ret);
            printf("ams call back, start %s.%s ret = %d\n", element.bundleName, element.abilityName, ret);
            ClearElement(&element);
            break;
        }
        case SCHEDULER_DUMP_ABILITY: {
            size_t len = 0;
            char *result = reinterpret_cast<char *>(ReadString(data, &len));
            printf("dump ability info:\n");
            if (!abilityTool->dumpAll_) {
                printf("[%s][%s]\n", abilityTool->elementName_.bundleName, abilityTool->elementName_.abilityName);
            }
            printf("{\n");
            for (int i = 0; i <= (len - 1) / MAX_BUFFER_SIZE_PER; i++) {
                int start = MAX_BUFFER_SIZE_PER * i;
                int size = (len - start) < MAX_BUFFER_SIZE_PER ? (len - start) : MAX_BUFFER_SIZE_PER;
                printf("%-.*s", size, result + start);
            }
            printf("}\n");
            break;
        }
        default: {
            printf("ams call back error, funcId: %u\n", code);
            break;
        }
    }
    sem_post(&g_sem);
    return 0;
}
} // namespace OHOS
