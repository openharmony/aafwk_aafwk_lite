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

#include "ability_scheduler.h"

#include <log.h>
#include <utils.h>

#include "ability_errors.h"
#include "ability_kit_command.h"
#include "ability_service_interface.h"
#include "ipc_skeleton.h"
#include "want_utils.h"

namespace OHOS {
const int MAX_MODULE_SIZE = 16;
AbilityScheduler::AbilityScheduler(AbilityEventHandler &eventHandler, Scheduler &scheduler)
    : eventHandler_(eventHandler), scheduler_(scheduler)
{
}

int32_t AbilityScheduler::AmsCallback(uint32_t code, IpcIo *data, IpcIo *reply, MessageOption option)
{
    auto scheduler = static_cast<AbilityScheduler *>(option.args);
    if (scheduler == nullptr) {
        HILOG_ERROR(HILOG_MODULE_APP, "ams call back error, scheduler is null");
        // if you need to send reply to the client, please use SendReply method
        return PARAM_NULL_ERROR;
    }
    int result = 0;
    switch (code) {
        case SCHEDULER_APP_INIT: {
            AppInfo appInfo = {};
            char *bundleName = reinterpret_cast<char *>(ReadString(data, nullptr));
            char *srcPath = reinterpret_cast<char *>(ReadString(data, nullptr));
            char *dataPath = reinterpret_cast<char *>(ReadString(data, nullptr));
            if ((bundleName == nullptr) || (srcPath == nullptr) || (dataPath == nullptr)) {
                HILOG_ERROR(HILOG_MODULE_APP, "ams call back error, bundleName, srcPath or dataPath is null");
                return PARAM_NULL_ERROR;
            }
            appInfo.bundleName = bundleName;
            appInfo.srcPath = srcPath;
            appInfo.dataPath = dataPath;
            ReadBool(data, &(appInfo.isNativeApp));
            int moduleSize = 0;
            ReadInt32(data, &moduleSize);
            if (moduleSize > MAX_MODULE_SIZE) {
                HILOG_ERROR(HILOG_MODULE_APP, "moduleSize is too big");
                return COMMAND_ERROR;
            }
            for (int i = 0; i < moduleSize; i++) {
                char *moduleName = reinterpret_cast<char *>(ReadString(data, nullptr));
                if ((moduleName != nullptr) && (strlen(moduleName) > 0)) {
                    appInfo.moduleNames.emplace_front(moduleName);
                }
            }
            scheduler->PerformAppInit(appInfo);
            break;
        }
        case SCHEDULER_ABILITY_LIFECYCLE: {
            int state = 0;
            ReadInt32(data, &state);
            uint64_t token = 0;
            ReadUint64(data, &token);
            int abilityType = 0;
            ReadInt32(data, &abilityType);
            Want want = { nullptr, nullptr, nullptr, 0 };
            if (!DeserializeWant(&want, data)) {
                result = SERIALIZE_ERROR;
                break;
            }
            scheduler->PerformTransactAbilityState(want, state, token, abilityType);
            break;
        }
        case SCHEDULER_ABILITY_CONNECT: {
            uint64_t token = 0;
            ReadUint64(data, &token);
            Want want = { nullptr, nullptr, nullptr, 0 };
            if (!DeserializeWant(&want, data)) {
                result = SERIALIZE_ERROR;
                break;
            }
            scheduler->PerformConnectAbility(want, token);
            break;
        }
        case SCHEDULER_ABILITY_DISCONNECT: {
            uint64_t token = 0;
            ReadUint64(data, &token);
            Want want = { nullptr, nullptr, nullptr, 0 };
            if (!DeserializeWant(&want, data)) {
                result = SERIALIZE_ERROR;
                break;
            }
            scheduler->PerformDisconnectAbility(want, token);
            break;
        }
        case SCHEDULER_APP_EXIT: {
            scheduler->PerformAppExit();
            break;
        }
        case SCHEDULER_DUMP_ABILITY: {
            Want want = {};
            if (!DeserializeWant(&want, data)) {
                result = SERIALIZE_ERROR;
                break;
            }

            uint64_t token = 0;
            ReadUint64(data, &token);
            scheduler->PerformDumpAbility(want, token);
            break;
        }
        default: {
            result = COMMAND_ERROR;
            break;
        }
    }
    return result;
}

void AbilityScheduler::PerformAppInit(const AppInfo &appInfo)
{
    auto task = [this, appInfo] {
        scheduler_.PerformAppInit(appInfo);
    };
    eventHandler_.PostTask(task);
}

void AbilityScheduler::PerformTransactAbilityState(const Want &want, int state, uint64_t token, int abilityType)
{
    auto task = [this, want, state, token, abilityType] {
        scheduler_.PerformTransactAbilityState(want, state, token, abilityType);
        ClearWant(const_cast<Want *>(&want));
    };
    eventHandler_.PostTask(task);
}

void AbilityScheduler::PerformConnectAbility(const Want &want, uint64_t token)
{
    auto task = [this, want, token] {
        scheduler_.PerformConnectAbility(want, token);
        ClearWant(const_cast<Want *>(&want));
    };
    eventHandler_.PostTask(task);
}

void AbilityScheduler::PerformDisconnectAbility(const Want &want, uint64_t token)
{
    auto task = [this, want, token] {
        scheduler_.PerformDisconnectAbility(want, token);
        ClearWant(const_cast<Want *>(&want));
    };
    eventHandler_.PostTask(task);
}

void AbilityScheduler::PerformAppExit()
{
    auto task = [this] {
        scheduler_.PerformAppExit();
    };
    eventHandler_.PostTask(task);
}

void AbilityScheduler::PerformDumpAbility(const Want &want, uint64_t token)
{
    if (want.sid == nullptr) {
        HILOG_ERROR(HILOG_MODULE_APP, "svcId is invalid when dump ability");
        return;
    }
    auto task = [this, want, token] {
        scheduler_.PerformDumpAbility(want, token);
        ClearWant(const_cast<Want *>(&want));
    };
    eventHandler_.PostTask(task);
}
} // namespace OHOS
