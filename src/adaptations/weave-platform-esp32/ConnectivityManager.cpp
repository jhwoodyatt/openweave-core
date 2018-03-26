#include <internal/WeavePlatformInternal.h>
#include <ConnectivityManager.h>
#include "esp_wifi.h"

using namespace ::nl;
using namespace ::nl::Weave;
using namespace ::WeavePlatform::Internal;

namespace WeavePlatform {

namespace {

extern const char *ESPWiFiModeToStr(wifi_mode_t wifiMode);
extern WEAVE_ERROR ChangeESPWiFiMode(esp_interface_t intf, bool enabled);

} // namespace Internal

WEAVE_ERROR ConnectivityManager::Init()
{
    WEAVE_ERROR err;

    mLastStationConnectTime = 0;
    mLastAPDemandTime = 0;
    mWiFiStationState = kWiFiStationState_Disabled;
    mWiFiAPMode = kWiFiAPMode_Disabled;
    mWiFiAPState = kWiFiAPState_Stopped;
    mWiFiStationReconnectIntervalMS = 5000; // TODO: make configurable
    mWiFiAPTimeoutMS = 30000; // TODO: make configurable

    // If the code has been compiled with a default WiFi station provision and no provision is currently configured...
    if (CONFIG_DEFAULT_WIFI_SSID[0] != 0 && !IsWiFiStationProvisioned())
    {
        ESP_LOGI(TAG, "Setting default WiFi station configuration (SSID %s)", CONFIG_DEFAULT_WIFI_SSID);

        // Switch to station mode temporarily so that the configuration can be changed.
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_wifi_set_mode() failed: %s", nl::ErrorStr(err));
        }

        // Set a default station configuration.
        wifi_config_t wifiConfig;
        memset(&wifiConfig, 0, sizeof(wifiConfig));
        memcpy(wifiConfig.sta.ssid, CONFIG_DEFAULT_WIFI_SSID, strlen(CONFIG_DEFAULT_WIFI_SSID) + 1);
        memcpy(wifiConfig.sta.password, CONFIG_DEFAULT_WIFI_PASSWORD, strlen(CONFIG_DEFAULT_WIFI_PASSWORD) + 1);
        wifiConfig.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        wifiConfig.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        err = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifiConfig);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_wifi_set_config() failed: %s", nl::ErrorStr(err));
        }
        err = WEAVE_NO_ERROR;

        // Enable WiFi station mode.
        err = esp_wifi_set_auto_connect(true);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_wifi_set_auto_connect() failed: %s", nl::ErrorStr(err));
        }
    }

    // Disable both AP and STA mode.  The AP and station state machines will re-enable these as needed.
    err = esp_wifi_set_mode(WIFI_MODE_NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_mode() failed: %s", nl::ErrorStr(err));
    }
    SuccessOrExit(err);

    // Queue work items to bootstrap the AP and station state machines once the Weave event loop is running.
    err = SystemLayer.ScheduleWork(DriveStationState, NULL);
    SuccessOrExit(err);
    err = SystemLayer.ScheduleWork(DriveAPState, NULL);
    SuccessOrExit(err);

exit:
    return err;
}

void ConnectivityManager::OnPlatformEvent(const struct WeavePlatformEvent * event)
{
    WEAVE_ERROR err;

    if (event->Type == WeavePlatformEvent::kType_ESPSystemEvent)
    {
        switch(event->ESPSystemEvent.event_id) {
        case SYSTEM_EVENT_STA_START:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
            DriveStationState();
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_CONNECTED");
            DriveStationState();
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
            DriveStationState();
            break;
        case SYSTEM_EVENT_STA_STOP:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_STOP");
            DriveStationState();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
            err = MessageLayer.RefreshEndpoints();
            if (err != WEAVE_NO_ERROR)
            {
                ESP_LOGE(TAG, "Error returned by MessageLayer.RefreshEndpoints(): %s", nl::ErrorStr(err));
            }
            break;
        case SYSTEM_EVENT_STA_LOST_IP:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_LOST_IP");
            err = MessageLayer.RefreshEndpoints();
            if (err != WEAVE_NO_ERROR)
            {
                ESP_LOGE(TAG, "Error returned by MessageLayer.RefreshEndpoints(): %s", nl::ErrorStr(err));
            }
            break;
        case SYSTEM_EVENT_GOT_IP6:
            ESP_LOGI(TAG, "SYSTEM_EVENT_GOT_IP6");
            err = MessageLayer.RefreshEndpoints();
            if (err != WEAVE_NO_ERROR)
            {
                ESP_LOGE(TAG, "Error returned by MessageLayer.RefreshEndpoints(): %s", nl::ErrorStr(err));
            }
            break;
        case SYSTEM_EVENT_AP_START:
            ESP_LOGI(TAG, "SYSTEM_EVENT_AP_START");
            if (mWiFiAPState == kWiFiAPState_Starting)
            {
                mWiFiAPState = kWiFiAPState_Started;
            }
            DriveAPState();
            break;
        case SYSTEM_EVENT_AP_STOP:
            ESP_LOGI(TAG, "SYSTEM_EVENT_AP_STOP");
            if (mWiFiAPState == kWiFiAPState_Stopping)
            {
                mWiFiAPState = kWiFiAPState_Stopped;
            }
            DriveAPState();
            break;
        case SYSTEM_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "SYSTEM_EVENT_AP_STACONNECTED");
            MaintainOnDemandWiFiAP();
            break;
        default:
            break;
        }
    }
}

void ConnectivityManager::DriveStationState()
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    WiFiStationState curState;

    // Determine the current state of the WiFi station interface.
    {
        wifi_mode_t wifiMode;
        if (esp_wifi_get_mode(&wifiMode) == ESP_OK && (wifiMode == WIFI_MODE_STA || wifiMode == WIFI_MODE_APSTA))
        {
            wifi_ap_record_t apInfo;
            // Determine if the station is currently connected to an AP.
            if (esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK)
            {
                curState = kWiFiStationState_Connected;
            }
            else
            {
                curState = kWiFiStationState_NotConnected;
            }
        }
        else
        {
            curState = kWiFiStationState_Disabled;
        }
    }

    // If the station state has changed...
    if (curState != mWiFiStationState)
    {
        // Handle a transition to the Connected state.
        if (curState == kWiFiStationState_Connected)
        {
            ESP_LOGI(TAG, "WiFi station interface connected");
            OnStationConnected();
        }

        // Handle a transition FROM the Connected state to Disconnected or Disabled.
        else if (mWiFiStationState == kWiFiStationState_Connected)
        {
            ESP_LOGI(TAG, "WiFi station interface disconnected");
            mLastStationConnectTime = 0;
            OnStationDisconnected();
        }

        mWiFiStationState = curState;
    }

    // If ESP station mode is currently disabled, enable it.
    if (mWiFiStationState == kWiFiStationState_Disabled)
    {
        err = ChangeESPWiFiMode(ESP_IF_WIFI_STA, true);
        SuccessOrExit(err);
    }

    // Otherwise ESP station mode is currently enables, so...
    // if station mode is enabled at the Weave level and a station provision exists...
    else if (IsWiFiStationEnabled() && IsWiFiStationProvisioned())
    {
        // If the station is not presently connected to an AP...
        if (mWiFiStationState == kWiFiStationState_NotConnected)
        {
            uint64_t now = SystemLayer.GetSystemTimeMS();

            // Initiate a connection to the AP if we haven't done so before, or if enough
            // time has passed since the last attempt.
            if (mLastStationConnectTime == 0 || now >= mLastStationConnectTime + mWiFiStationReconnectIntervalMS)
            {
                mLastStationConnectTime = now;

                ESP_LOGI(TAG, "Attempting to connect WiFi station interface");
                err = esp_wifi_connect();
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "esp_wifi_connect() failed: %s", nl::ErrorStr(err));
                }
                SuccessOrExit(err);
            }

            // Otherwise arrange another connection attempt at a suitable point in the future.
            else
            {
                uint32_t timeToNextConnect = (uint32_t)((mLastStationConnectTime + mWiFiStationReconnectIntervalMS) - now);

                ESP_LOGI(TAG, "Next WiFi station reconnect in %" PRIu32 " ms", timeToNextConnect);

                err = SystemLayer.StartTimer(timeToNextConnect, DriveStationState, NULL);
                SuccessOrExit(err);
            }
        }
    }

    // Otherwise station mode is DISABLED at the Weave level or no station provision exists, so...
    else
    {
        // If the station is currently connected to an AP, disconnect now.
        if (mWiFiStationState == kWiFiStationState_Connected)
        {
            ESP_LOGI(TAG, "Disconnecting WiFi station interface");
            err = esp_wifi_disconnect();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "esp_wifi_disconnect() failed: %s", nl::ErrorStr(err));
            }
            SuccessOrExit(err);
        }
    }

exit:
    if (err != WEAVE_NO_ERROR)
    {
        SetWiFiStationMode(kWiFiStationMode_Disabled);
    }
}

void ConnectivityManager::DriveAPState()
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    WiFiAPState targetState;
    uint32_t apTimeout = 0;

    if (mWiFiAPMode == kWiFiAPMode_Disabled)
    {
        targetState = kWiFiAPState_Stopped;
    }

    else if (mWiFiAPMode == kWiFiAPMode_Enabled)
    {
        targetState = kWiFiAPState_Started;
    }

    else if (mWiFiAPMode == kWiFiAPMode_OnDemand_NoStationProvision && !IsWiFiStationProvisioned())
    {
        targetState = kWiFiAPState_Started;
    }

    else if (mWiFiAPMode == kWiFiAPMode_OnDemand ||
             mWiFiAPMode == kWiFiAPMode_OnDemand_NoStationProvision)
    {
        uint64_t now = SystemLayer.GetSystemTimeMS();

        if (mLastAPDemandTime != 0 && now < (mLastAPDemandTime + mWiFiAPTimeoutMS))
        {
            targetState = kWiFiAPState_Started;
            apTimeout = (uint32_t)((mLastAPDemandTime + mWiFiAPTimeoutMS) - now);
        }
        else
        {
            targetState = kWiFiAPState_Stopped;
        }
    }
    else
    {
        targetState = kWiFiAPState_Stopped;
    }

    if (mWiFiAPState != targetState)
    {
        if (targetState == kWiFiAPState_Started)
        {
            wifi_config_t wifiConfig;

            err = ChangeESPWiFiMode(ESP_IF_WIFI_AP, true);
            SuccessOrExit(err);

            memset(&wifiConfig, 0, sizeof(wifiConfig));
            strcpy((char *)wifiConfig.ap.ssid, "ESP-TEST");
            wifiConfig.ap.channel = 1;
            wifiConfig.ap.authmode = WIFI_AUTH_OPEN;
            wifiConfig.ap.max_connection = 4;
            wifiConfig.ap.beacon_interval = 100;
            err = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifiConfig);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "esp_wifi_set_config(ESP_IF_WIFI_AP) failed: %s", nl::ErrorStr(err));
            }
            SuccessOrExit(err);

            if (mWiFiAPState == kWiFiAPState_Stopped)
            {
                mWiFiAPState = kWiFiAPState_Starting;
            }
        }
        else
        {
            err = ChangeESPWiFiMode(ESP_IF_WIFI_AP, false);
            SuccessOrExit(err);

            if (mWiFiAPState == kWiFiAPState_Started)
            {
                mWiFiAPState = kWiFiAPState_Stopping;
            }
        }
    }

    if (apTimeout != 0)
    {
        ESP_LOGI(TAG, "Next WiFi AP timeout in %" PRIu32 " ms", apTimeout);

        err = SystemLayer.StartTimer(apTimeout, DriveAPState, NULL);
        SuccessOrExit(err);
    }

exit:
    if (err != WEAVE_NO_ERROR)
    {
        SetWiFiAPMode(kWiFiAPMode_Disabled);
    }
}

ConnectivityManager::WiFiStationMode ConnectivityManager::GetWiFiStationMode(void) const
{
    bool autoConnect;
    return (esp_wifi_get_auto_connect(&autoConnect) == ESP_OK && autoConnect)
            ? kWiFiStationMode_Enabled : kWiFiStationMode_Disabled;
}

bool ConnectivityManager::IsWiFiStationEnabled(void) const
{
    return GetWiFiStationMode() == kWiFiStationMode_Enabled;
}

WEAVE_ERROR ConnectivityManager::SetWiFiStationMode(WiFiStationMode val)
{
    esp_err_t err;
    bool autoConnect;

    VerifyOrExit(val == kWiFiStationMode_Enabled || val == kWiFiStationMode_Disabled, err = WEAVE_ERROR_INVALID_ARGUMENT);

    err = esp_wifi_get_auto_connect(&autoConnect);
    SuccessOrExit(err);

    if (autoConnect != (val == kWiFiStationMode_Enabled))
    {
        autoConnect = (val == kWiFiStationMode_Enabled);
        err = esp_wifi_set_auto_connect(val);
        SuccessOrExit(err);

        ESP_LOGI(TAG, "WiFi station interface %s", (autoConnect) ? "enabled" : "disabled");

        SystemLayer.ScheduleWork(DriveStationState, NULL);
    }

exit:
    return err;
}

bool ConnectivityManager::IsWiFiStationProvisioned(void) const
{
    wifi_config_t stationConfig;
    return (esp_wifi_get_config(ESP_IF_WIFI_STA, &stationConfig) == ERR_OK && stationConfig.sta.ssid[0] != 0);
}

void ConnectivityManager::ClearWiFiStationProvision(void)
{
    wifi_config_t stationConfig;

    memset(&stationConfig, 0, sizeof(stationConfig));
    esp_wifi_set_config(ESP_IF_WIFI_STA, &stationConfig);

    SystemLayer.ScheduleWork(DriveStationState, NULL);
}

WEAVE_ERROR ConnectivityManager::SetWiFiAPMode(WiFiAPMode val)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;

    VerifyOrExit(val == kWiFiAPMode_Disabled ||
                 val == kWiFiAPMode_Enabled ||
                 val == kWiFiAPMode_OnDemand ||
                 val == kWiFiAPMode_OnDemand_NoStationProvision,
                 err = WEAVE_ERROR_INVALID_ARGUMENT);

    mWiFiAPMode = val;

    SystemLayer.ScheduleWork(DriveAPState, NULL);

exit:
    return err;
}

void ConnectivityManager::DemandStartWiFiAP(void)
{
    if (mWiFiAPMode == kWiFiAPMode_OnDemand ||
        mWiFiAPMode == kWiFiAPMode_OnDemand_NoStationProvision)
    {
        mLastAPDemandTime = SystemLayer.GetSystemTimeMS();
        SystemLayer.ScheduleWork(DriveAPState, NULL);
    }
}

void ConnectivityManager::StopOnDemandWiFiAP(void)
{
    if (mWiFiAPMode == kWiFiAPMode_OnDemand ||
        mWiFiAPMode == kWiFiAPMode_OnDemand_NoStationProvision)
    {
        mLastAPDemandTime = 0;
        SystemLayer.ScheduleWork(DriveAPState, NULL);
    }
}

void ConnectivityManager::MaintainOnDemandWiFiAP(void)
{
    if (mWiFiAPMode == kWiFiAPMode_OnDemand ||
        mWiFiAPMode == kWiFiAPMode_OnDemand_NoStationProvision)
    {
        if (mWiFiAPState == kWiFiAPState_Started || mWiFiAPState == kWiFiAPState_Starting)
        {
            mLastAPDemandTime = SystemLayer.GetSystemTimeMS();
        }
    }
}

void ConnectivityManager::SetWiFiAPTimeoutMS(uint32_t val)
{
    mWiFiAPTimeoutMS = val;
    SystemLayer.ScheduleWork(DriveAPState, NULL);
}

void ConnectivityManager::OnStationConnected()
{
    // TODO: alert other subsystems of connected state

    // Assign an IPv6 link local address to the station interface.
    tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA);
}

void ConnectivityManager::OnStationDisconnected()
{
    // TODO: alert other subsystems of disconnected state
}

void ConnectivityManager::DriveStationState(nl::Weave::System::Layer * aLayer, void * aAppState, nl::Weave::System::Error aError)
{
    ConnectivityMgr.DriveStationState();
}

void ConnectivityManager::DriveAPState(nl::Weave::System::Layer * aLayer, void * aAppState, nl::Weave::System::Error aError)
{
    ConnectivityMgr.DriveAPState();
}

namespace {

const char *ESPWiFiModeToStr(wifi_mode_t wifiMode)
{
    switch (wifiMode)
    {
    case WIFI_MODE_NULL:
        return "NULL";
    case WIFI_MODE_STA:
        return "STA";
    case WIFI_MODE_AP:
        return "AP";
    case WIFI_MODE_APSTA:
        return "STA+AP";
    default:
        return "(unknown)";
    }
}

WEAVE_ERROR ChangeESPWiFiMode(esp_interface_t intf, bool enabled)
{
    WEAVE_ERROR err;
    wifi_mode_t curWiFiMode, targetWiFiMode;
    bool stationEnabled, apEnabled;

    VerifyOrExit(intf == ESP_IF_WIFI_STA || intf == ESP_IF_WIFI_AP, err = WEAVE_ERROR_INVALID_ARGUMENT);

    err = esp_wifi_get_mode(&curWiFiMode);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_get_mode() failed: %s", nl::ErrorStr(err));
    }
    SuccessOrExit(err);

    stationEnabled = (curWiFiMode == WIFI_MODE_STA || curWiFiMode == WIFI_MODE_APSTA);
    apEnabled = (curWiFiMode == WIFI_MODE_AP || curWiFiMode == WIFI_MODE_APSTA);

    if (intf == ESP_IF_WIFI_STA)
    {
        stationEnabled = enabled;
    }
    else
    {
        apEnabled = enabled;
    }

    targetWiFiMode = (stationEnabled)
        ? ((apEnabled) ? WIFI_MODE_APSTA : WIFI_MODE_STA)
        : ((apEnabled) ? WIFI_MODE_AP : WIFI_MODE_NULL);

    if (targetWiFiMode != curWiFiMode)
    {
        ESP_LOGI(TAG, "Changing ESP WiFi mode: %s -> %s", ESPWiFiModeToStr(curWiFiMode), ESPWiFiModeToStr(targetWiFiMode));

        err = esp_wifi_set_mode(targetWiFiMode);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_wifi_set_mode() failed: %s", nl::ErrorStr(err));
        }
        SuccessOrExit(err);
    }

exit:
    return err;
}

} // unnamed namespace

} // namespace WeavePlatform
