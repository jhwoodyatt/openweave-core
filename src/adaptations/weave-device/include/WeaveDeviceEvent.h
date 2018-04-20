/*
 *
 *    Copyright (c) 2018 Nest Labs, Inc.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#ifndef WEAVE_DEVICE_EVENT_H
#define WEAVE_DEVICE_EVENT_H

#include <esp_event.h>

namespace nl {
namespace Weave {
namespace Device {

enum ConnectivityChange
{
    kConnectivity_Established = 0,
    kConnectivity_Lost,
    kConnectivity_NoChange
};

typedef void (*AsyncWorkFunct)(intptr_t arg);

struct WeaveDeviceEvent
{
    enum
    {
        kEventType_NoOp                                         = 0,
        kEventType_ESPSystemEvent,
        kEventType_WeaveSystemLayerEvent,
        kEventType_CallWorkFunct,
        kEventType_WiFiConnectivityChange,
        kEventType_InternetConnectivityChange,
        kEventType_ServiceConnectivityChange,
        kEventType_FabricMembershipChange,
        kEventType_ServiceProvisioningChange,
        kEventType_AccountPairingChange,
        kEventType_TimeSyncChange,
        kEventType_SessionEstablished,
    };

    uint16_t Type;

    union
    {
        system_event_t ESPSystemEvent;
        struct
        {
            ::nl::Weave::System::EventType Type;
            ::nl::Weave::System::Object * Target;
            uintptr_t Argument;
        } WeaveSystemLayerEvent;
        struct
        {
            AsyncWorkFunct WorkFunct;
            intptr_t Arg;
        } CallWorkFunct;
        struct
        {
            ConnectivityChange Result;
        } WiFiConnectivityChange;
        struct
        {
            ConnectivityChange IPv4;
            ConnectivityChange IPv6;
        } InternetConnectivityChange;
        struct
        {
            ConnectivityChange Result;
        } ServiceConnectivityChange;
        struct
        {
            bool IsMemberOfFabric;
        } FabricMembershipChange;
        struct
        {
            bool IsServiceProvisioned;
            bool ServiceConfigUpdated;
        } ServiceProvisioningChange;
        struct
        {
            bool IsPairedToAccount;
        } AccountPairingChange;
        struct
        {
            bool IsTimeSynchronized;
        } TimeSyncChange;
        struct
        {
            uint64_t PeerNodeId;
            uint16_t SessionKeyId;
            uint8_t EncType;
            ::nl::Weave::WeaveAuthMode AuthMode;
            bool IsCommissioner;
        } SessionEstablished;
    };
};

} // namespace Device
} // namespace Weave
} // namespace nl

#endif // WEAVE_DEVICE_EVENT_H
