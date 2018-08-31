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

#ifndef CONNECTIVITY_MANAGER_IMPL_H
#define CONNECTIVITY_MANAGER_IMPL_H

#include <Weave/Profiles/network-provisioning/NetworkProvisioning.h>
#include <Weave/Profiles/weave-tunneling/WeaveTunnelCommon.h>
#include <Weave/Profiles/weave-tunneling/WeaveTunnelConnectionMgr.h>
#include <Weave/Support/FlagUtils.hpp>
#include "esp_event.h"

namespace nl {
namespace Inet {
class IPAddress;
} // namespace Inet
} // namespace nl

namespace nl {
namespace Weave {
namespace DeviceLayer {

namespace Internal {

class NetworkProvisioningServerImpl;
template<class ImplClass> class GenericNetworkProvisioningServerImpl;

// TODO: move this to utilities file.
extern const char *CharacterizeIPv6Address(const ::nl::Inet::IPAddress & ipAddr);

} // namespace Internal

/**
 * Concrete implementation of the ConnectivityManager singleton object for the ESP32 platform.
 */
class ConnectivityManagerImpl : public ConnectivityManager
{
    using TunnelConnNotifyReasons = ::nl::Weave::Profiles::WeaveTunnel::WeaveTunnelConnectionMgr::TunnelConnNotifyReasons;

    // Allow the ConnectivityManager interface class to delegate method calls to
    // the implementation methods provided by this class.
    friend class ConnectivityManager;

public:

    // ===== Members that implement the ConnectivityManager public interface.

    // WiFi station methods
    WiFiStationMode _GetWiFiStationMode(void);
    WEAVE_ERROR _SetWiFiStationMode(WiFiStationMode val);
    bool _IsWiFiStationEnabled(void);
    bool _IsWiFiStationApplicationControlled(void);
    bool _IsWiFiStationConnected(void);
    uint32_t _GetWiFiStationReconnectIntervalMS(void);
    WEAVE_ERROR _SetWiFiStationReconnectIntervalMS(uint32_t val);
    bool _IsWiFiStationProvisioned(void);
    void _ClearWiFiStationProvision(void);

    // WiFi AP methods
    WiFiAPMode _GetWiFiAPMode(void);
    WEAVE_ERROR _SetWiFiAPMode(WiFiAPMode val);
    bool _IsWiFiAPActive(void);
    bool _IsWiFiAPApplicationControlled(void);
    void _DemandStartWiFiAP(void);
    void _StopOnDemandWiFiAP(void);
    void _MaintainOnDemandWiFiAP(void);
    uint32_t _GetWiFiAPIdleTimeoutMS(void);
    void _SetWiFiAPIdleTimeoutMS(uint32_t val);

    // Internet connectivity methods
    bool _HaveIPv4InternetConnectivity(void);
    bool _HaveIPv6InternetConnectivity(void);

    // Service tunnel methods
    ServiceTunnelMode _GetServiceTunnelMode(void);
    WEAVE_ERROR _SetServiceTunnelMode(ServiceTunnelMode val);
    bool _IsServiceTunnelConnected(void);
    bool _IsServiceTunnelRestricted(void);

    // Service connectivity methods
    bool _HaveServiceConnectivity(void);

    // WoBLE service methods
    WoBLEServiceMode _GetWoBLEServiceMode(void);
    WEAVE_ERROR _SetWoBLEServiceMode(WoBLEServiceMode val);
    bool _IsBLEAdvertisingEnabled(void);
    WEAVE_ERROR _SetBLEAdvertisingEnabled(bool val);
    bool _IsBLEFastAdvertisingEnabled(void);
    WEAVE_ERROR _SetBLEFastAdvertisingEnabled(bool val);
    WEAVE_ERROR _GetBLEDeviceName(char * buf, size_t bufSize);
    WEAVE_ERROR _SetBLEDeviceName(const char * deviceName);
    uint16_t _NumBLEConnections(void);

    // ===== Implementation-specific members that may be accessed directly by the application.

    static ConnectivityManagerImpl & Instance();

private:

    // ===== Members for internal use by the following friends.

    friend class ::nl::Weave::DeviceLayer::PlatformManager;
    friend class ::nl::Weave::DeviceLayer::Internal::NetworkProvisioningServerImpl;
    template<class ImplClass> friend class ::nl::Weave::DeviceLayer::Internal::GenericNetworkProvisioningServerImpl;
    friend ConnectivityManager & ConnectivityMgr(void);

    static ConnectivityManagerImpl sInstance;

    WEAVE_ERROR _Init(void);
    void _OnPlatformEvent(const WeaveDeviceEvent * event);
    bool _CanStartWiFiScan();
    void _OnWiFiScanDone();
    void _OnWiFiStationProvisionChange();

private:

    // ===== Private members reserved for use by this class only.

    enum WiFiStationState
    {
        kWiFiStationState_NotConnected,
        kWiFiStationState_Connecting,
        kWiFiStationState_Connecting_Succeeded,
        kWiFiStationState_Connecting_Failed,
        kWiFiStationState_Connected,
        kWiFiStationState_Disconnecting,
    };

    enum WiFiAPState
    {
        kWiFiAPState_NotActive,
        kWiFiAPState_Activating,
        kWiFiAPState_Active,
        kWiFiAPState_Deactivating,
    };

    enum Flags
    {
        kFlag_HaveIPv4InternetConnectivity      = 0x0001,
        kFlag_HaveIPv6InternetConnectivity      = 0x0002,
        kFlag_ServiceTunnelStarted              = 0x0004,
        kFlag_ServiceTunnelUp                   = 0x0008,
        kFlag_AwaitingConnectivity              = 0x0010,
    };

    uint64_t mLastStationConnectFailTime;
    uint64_t mLastAPDemandTime;
    WiFiStationMode mWiFiStationMode;
    WiFiStationState mWiFiStationState;
    WiFiAPMode mWiFiAPMode;
    WiFiAPState mWiFiAPState;
    ServiceTunnelMode mServiceTunnelMode;
    uint32_t mWiFiStationReconnectIntervalMS;
    uint32_t mWiFiAPIdleTimeoutMS;
    uint16_t mFlags;

    void DriveStationState(void);
    void OnStationConnected(void);
    void OnStationDisconnected(void);
    void ChangeWiFiStationState(WiFiStationState newState);
    static void DriveStationState(::nl::Weave::System::Layer * aLayer, void * aAppState, ::nl::Weave::System::Error aError);

    void DriveAPState(void);
    WEAVE_ERROR ConfigureWiFiAP(void);
    void ChangeWiFiAPState(WiFiAPState newState);
    static void DriveAPState(::nl::Weave::System::Layer * aLayer, void * aAppState, ::nl::Weave::System::Error aError);

    void UpdateInternetConnectivityState(void);
    void OnStationIPv4AddressAvailable(const system_event_sta_got_ip_t & got_ip);
    void OnStationIPv4AddressLost(void);
    void OnIPv6AddressAvailable(const system_event_got_ip6_t & got_ip);

    void DriveServiceTunnelState(void);
    static void DriveServiceTunnelState(::nl::Weave::System::Layer * aLayer, void * aAppState, ::nl::Weave::System::Error aError);

    static const char * WiFiStationModeToStr(WiFiStationMode mode);
    static const char * WiFiStationStateToStr(WiFiStationState state);
    static const char * WiFiAPModeToStr(WiFiAPMode mode);
    static const char * WiFiAPStateToStr(WiFiAPState state);
    static void RefreshMessageLayer(void);
    static void HandleServiceTunnelNotification(TunnelConnNotifyReasons reason, WEAVE_ERROR err, void *appCtxt);
};

inline bool ConnectivityManagerImpl::_IsWiFiStationApplicationControlled(void)
{
    return mWiFiStationMode == kWiFiStationMode_ApplicationControlled;
}

inline bool ConnectivityManagerImpl::_IsWiFiStationConnected(void)
{
    return mWiFiStationState == kWiFiStationState_Connected;
}

inline bool ConnectivityManagerImpl::_IsWiFiAPApplicationControlled(void)
{
    return mWiFiAPMode == kWiFiAPMode_ApplicationControlled;
}

inline uint32_t ConnectivityManagerImpl::_GetWiFiStationReconnectIntervalMS(void)
{
    return mWiFiStationReconnectIntervalMS;
}

inline ConnectivityManager::WiFiAPMode ConnectivityManagerImpl::_GetWiFiAPMode(void)
{
    return mWiFiAPMode;
}

inline bool ConnectivityManagerImpl::_IsWiFiAPActive(void)
{
    return mWiFiAPState == kWiFiAPState_Active;
}

inline uint32_t ConnectivityManagerImpl::_GetWiFiAPIdleTimeoutMS(void)
{
    return mWiFiAPIdleTimeoutMS;
}

inline bool ConnectivityManagerImpl::_HaveIPv4InternetConnectivity(void)
{
    return ::nl::GetFlag(mFlags, kFlag_HaveIPv4InternetConnectivity);
}

inline bool ConnectivityManagerImpl::_HaveIPv6InternetConnectivity(void)
{
    return ::nl::GetFlag(mFlags, kFlag_HaveIPv6InternetConnectivity);
}

inline ConnectivityManager::ServiceTunnelMode ConnectivityManagerImpl::_GetServiceTunnelMode(void)
{
    return mServiceTunnelMode;
}

inline bool ConnectivityManagerImpl::_CanStartWiFiScan()
{
    return mWiFiStationState != kWiFiStationState_Connecting;
}

inline ConnectivityManagerImpl & ConnectivityManagerImpl::Instance(void)
{
    return sInstance;
}

inline ConnectivityManager & ConnectivityMgr(void)
{
    return ConnectivityManagerImpl::sInstance;
}

} // namespace DeviceLayer
} // namespace Weave
} // namespace nl

#endif // CONNECTIVITY_MANAGER_IMPL_H