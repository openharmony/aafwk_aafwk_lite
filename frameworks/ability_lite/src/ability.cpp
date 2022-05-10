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

#include "ability.h"

#include <ability_kit_command.h>
#include <ability_state.h>
#include <log.h>
#include <unistd.h>

#include "ability_info.h"
#include "ability_loader.h"
#ifdef ABILITY_WINDOW_SUPPORT
#include "ability_slice_manager.h"
#include "ability_window.h"
#endif
#include "adapter.h"
#include "ipc_skeleton.h"
#include "rpc_errno.h"

namespace OHOS {
void Ability::OnStart(const Want &want)
{
    HILOG_INFO(HILOG_MODULE_APP, "Ability OnStart");
    if (abilityState_ != STATE_INITIAL) {
        HILOG_ERROR(HILOG_MODULE_APP, "Start ability error, state: %{public}d", abilityState_);
        exit(-1);
    }

#ifdef ABILITY_WINDOW_SUPPORT
    DeliverAbilityLifecycle(START, &want);
    if (abilityWindow_ != nullptr) {
        abilityWindow_->OnPostAbilityStart();
    }
#endif
    abilityState_ = STATE_INACTIVE;
}

void Ability::OnInactive()
{
    HILOG_INFO(HILOG_MODULE_APP, "Ability OnInactive");
    if (abilityState_ != STATE_ACTIVE) {
        HILOG_ERROR(HILOG_MODULE_APP, "Inactive ability error, state: %{public}d", abilityState_);
        exit(-1);
    }

#ifdef ABILITY_WINDOW_SUPPORT
    DeliverAbilityLifecycle(INACTIVE);
#endif
    abilityState_ = STATE_INACTIVE;
}

void Ability::OnActive(const Want &want)
{
    HILOG_INFO(HILOG_MODULE_APP, "Ability OnActive");
    if ((abilityState_ != STATE_INACTIVE) && (abilityState_ != STATE_BACKGROUND)) {
        HILOG_ERROR(HILOG_MODULE_APP, "Active ability error, state: %{public}d", abilityState_);
        exit(-1);
    }

#ifdef ABILITY_WINDOW_SUPPORT
    DeliverAbilityLifecycle(ACTIVE, &want);
    if ((abilityWindow_ != nullptr) && (abilityState_ == STATE_BACKGROUND)) {
        abilityWindow_->OnPostAbilityActive();
    }
#endif
    abilityState_ = STATE_ACTIVE;
}

void Ability::OnBackground()
{
    HILOG_INFO(HILOG_MODULE_APP, "Ability OnBackground");
    if (abilityState_ != STATE_INACTIVE) {
        HILOG_ERROR(HILOG_MODULE_APP, "Background ability error, state: %{public}d", abilityState_);
        exit(-1);
    }

#ifdef ABILITY_WINDOW_SUPPORT
    DeliverAbilityLifecycle(BACKGROUND);
    if (abilityWindow_ != nullptr) {
        abilityWindow_->OnPostAbilityBackground();
    }
#endif
    abilityState_ = STATE_BACKGROUND;
}

void Ability::OnStop()
{
    HILOG_INFO(HILOG_MODULE_APP, "Ability OnStop");
    if (abilityState_ != STATE_BACKGROUND) {
        HILOG_ERROR(HILOG_MODULE_APP, "Stop ability error, state: %{public}d", abilityState_);
        exit(-1);
    }

#ifdef ABILITY_WINDOW_SUPPORT
    DeliverAbilityLifecycle(STOP);
    if (abilityWindow_ != nullptr) {
        abilityWindow_->OnPostAbilityStop();
        delete abilityWindow_;
    }
    delete abilitySliceManager_;
    abilitySliceManager_ = nullptr;
    abilityWindow_ = nullptr;
#endif
    abilityState_ = STATE_INITIAL;
}

const SvcIdentity *Ability::OnConnect(const Want &want)
{
    HILOG_INFO(HILOG_MODULE_APP, "Ability Connect");
    sid_ = static_cast<SvcIdentity *>(AdapterMalloc(sizeof(SvcIdentity)));
    if (sid_ == nullptr) {
        HILOG_ERROR(HILOG_MODULE_APP, "malloc memory error, sid_ is null");
        return nullptr;
    }
    objectStub_.func = Ability::MsgHandleInner;
    objectStub_.args = (void*)this;
    objectStub_.isRemote = false;
    sid_->handle = IPC_INVALID_HANDLE;
    sid_->token = SERVICE_TYPE_ANONYMOUS;
    sid_->cookie = reinterpret_cast<uintptr_t>(&objectStub_);
    return sid_;
}

void Ability::OnDisconnect(const Want &want)
{
    HILOG_INFO(HILOG_MODULE_APP, "Ability OnDisconnect");

    // clear
    AdapterFree(sid_);
    sid_ = nullptr;
}

#ifdef ABILITY_WINDOW_SUPPORT
void Ability::SetMainRoute(const std::string &entry)
{
    if (abilitySliceManager_ == nullptr) {
        HILOG_ERROR(HILOG_MODULE_APP, "AbilitySliceManager is null, fail to set main route");
        exit(-1);
    }

    abilitySliceManager_->SetMainRoute(entry);
}

void Ability::SetUIContent(RootView *rootView)
{
    if (abilityWindow_ == nullptr) {
        HILOG_ERROR(HILOG_MODULE_APP, "AbilityWindow is null, fail to SetUIContent");
        exit(-1);
    }

    abilityWindow_->SetRootView(rootView);
}
#endif

void Ability::MsgHandle(uint32_t funcId, IpcIo *request, IpcIo *reply)
{
}

void Ability::Dump(const std::string &extra)
{
}

void Ability::Init(uint64_t token, int abilityType, bool isNativeApp)
{
    HILOG_INFO(HILOG_MODULE_APP, "Ability Init");
#ifdef ABILITY_WINDOW_SUPPORT
    if (abilityType == PAGE) {
        if (isNativeApp) {
            abilitySliceManager_ = new AbilitySliceManager(*this);
        }
        abilityWindow_ = new AbilityWindow();
    }
#endif

    abilityType_ = abilityType;
    token_ = token;
    AbilityContext::token_ = token;
    abilityState_ = STATE_INITIAL;
}

int Ability::GetState() const
{
    return abilityState_;
}

std::string Ability::GetDumpInfo() const
{
    std::string dumpInfo;
    dumpInfo += "Ability Type:   [" + std::to_string(abilityType_) + "]\n";
    dumpInfo += "Ability State:  [" + std::to_string(abilityState_) + "]\n";
#ifdef ABILITY_WINDOW_SUPPORT
    dumpInfo += "AbilitySlice stack:\n";
    if ((abilityType_ == PAGE) && (abilitySliceManager_ != nullptr)) {
        // Add slice stack information
        dumpInfo += abilitySliceManager_->GetSliceStackInfo();
    } else {
        dumpInfo += "    none";
    }
#endif

    return dumpInfo;
}

#ifdef ABILITY_WINDOW_SUPPORT
void Ability::DeliverAbilityLifecycle(Action action, const Want *want)
{
    if (abilitySliceManager_ == nullptr) {
        HILOG_WARN(HILOG_MODULE_APP, "AbilitySliceManager is null, no need to dispatch lifecycle state change");
        return;
    }

    if ((action == START || action == ACTIVE) && want == nullptr) {
        HILOG_ERROR(HILOG_MODULE_APP, "want is null");
        return;
    }

    switch (action) {
        case START:
            abilitySliceManager_->OnAbilityStart(*want);
            break;
        case INACTIVE:
            abilitySliceManager_->OnAbilityInactive();
            break;
        case ACTIVE:
            abilitySliceManager_->OnAbilityActive(*want);
            break;
        case BACKGROUND:
            abilitySliceManager_->OnAbilityBackground();
            break;
        case STOP:
            abilitySliceManager_->OnAbilityStop();
            break;
        default:
            HILOG_ERROR(HILOG_MODULE_APP, "action is error");
            break;
    }
}
#endif

int32_t Ability::MsgHandleInner(uint32_t code, IpcIo *data, IpcIo *reply, MessageOption option)
{
    auto ability = static_cast<Ability *>(option.args);
    if (ability == nullptr) {
        HILOG_INFO(HILOG_MODULE_APP, "handle message error, ability is null");
        return ERR_INVALID_PARAM;
    }
    // call user method
    ability->MsgHandle(code, data, reply);
    return ERR_NONE;
}
} // namespace OHOS
