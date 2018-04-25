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

#include <internal/WeaveDeviceInternal.h>
#include <internal/BLEManager.h>
#include <new>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"

using namespace ::nl;
using namespace ::nl::Ble;

namespace nl {
namespace Weave {
namespace Device {
namespace Internal {

namespace {

struct WeaveServiceData
{
    uint8_t ServiceUUID[2];
    uint8_t DataBlockLen;
    uint8_t DataBlockType;
    uint8_t DataBlockMajorVersion;
    uint8_t DataBlockMinorVersion;
    uint8_t DeviceVendorId[2];
    uint8_t DeviceProductId[2];
    uint8_t DeviceId[8];
    uint8_t PairingStatus;
};

const uint16_t WoBLEAppId = 0x235A;

const uint8_t UUID_PrimaryService[] = { 0x00, 0x28 };
const uint8_t UUID_CharDecl[] = { 0x03, 0x28 };
const uint8_t UUID_ClientCharConfigDesc[] = { 0x02, 0x29 };
const uint8_t UUID_WoBLEService[] = { 0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xAF, 0xFE, 0x00, 0x00 };
const uint8_t ShortUUID_WoBLEService[] = { 0xAF, 0xFE };
const uint8_t UUID_WoBLEChar_RX[] = { 0x11, 0x9D, 0x9F, 0x42, 0x9C, 0x4F, 0x9F, 0x95, 0x59, 0x45, 0x3D, 0x26, 0xF5, 0x2E, 0xEE, 0x18 };
const uint8_t UUID_WoBLEChar_TX[] = { 0x12, 0x9D, 0x9F, 0x42, 0x9C, 0x4F, 0x9F, 0x95, 0x59, 0x45, 0x3D, 0x26, 0xF5, 0x2E, 0xEE, 0x18 };
const WeaveBleUUID WeaveUUID_WoBLEChar_RX = { { 0x18, 0xEE, 0x2E, 0xF5, 0x26, 0x3D, 0x45, 0x59, 0x95, 0x9F, 0x4F, 0x9C, 0x42, 0x9F, 0x9D, 0x11 } };
const WeaveBleUUID WeaveUUID_WoBLEChar_TX = { { 0x18, 0xEE, 0x2E, 0xF5, 0x26, 0x3D, 0x45, 0x59, 0x95, 0x9F, 0x4F, 0x9C, 0x42, 0x9F, 0x9D, 0x12 } };

const uint8_t CharProps_ReadNotify =  ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
const uint8_t CharProps_Write =  ESP_GATT_CHAR_PROP_BIT_WRITE;

// Offsets into WoBLEGATTAttrs for specific attributes.
enum
{
    kAttrIndex_ServiceDeclaration = 0,
    kAttrIndex_RXCharValue = 2,
    kAttrIndex_TXCharValue = 4,
    kAttrIndex_TXCharCCCDValue = 5,
};

// Table of attribute definitions for Weave over BLE GATT service.
const esp_gatts_attr_db_t WoBLEGATTAttrs[] =
{
    // Service Declaration for Weave over BLE Service
    { { ESP_GATT_AUTO_RSP }, { ESP_UUID_LEN_16, (uint8_t *)UUID_PrimaryService, ESP_GATT_PERM_READ, ESP_UUID_LEN_128, ESP_UUID_LEN_128, (uint8_t *)UUID_WoBLEService } },

    // ----- Weave over BLE RX Characteristic -----

    // Characteristic declaration
    { { ESP_GATT_AUTO_RSP }, { ESP_UUID_LEN_16, (uint8_t *)UUID_CharDecl, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&CharProps_Write } },
    // Characteristic value
    { { ESP_GATT_RSP_BY_APP }, { ESP_UUID_LEN_128, (uint8_t *)UUID_WoBLEChar_RX, ESP_GATT_PERM_WRITE, 512, 0, NULL } },

    // ----- Weave over BLE TX Characteristic -----

    // Characteristic declaration
    { { ESP_GATT_AUTO_RSP }, { ESP_UUID_LEN_16, (uint8_t *)UUID_CharDecl, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&CharProps_ReadNotify } },
    // Characteristic value
    { { ESP_GATT_RSP_BY_APP }, { ESP_UUID_LEN_128, (uint8_t *)UUID_WoBLEChar_TX, ESP_GATT_PERM_READ, 512, 0, NULL } },
    // Client characteristic configuration description (CCCD) value
    { { ESP_GATT_RSP_BY_APP }, { ESP_UUID_LEN_16, (uint8_t *)UUID_ClientCharConfigDesc, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE, 2, 0, NULL } },
};

const uint16_t WoBLEGATTAttrCount = sizeof(WoBLEGATTAttrs) / sizeof(WoBLEGATTAttrs[0]);

} // unnamed namespace


WEAVE_ERROR BLEManager::Init()
{
    WEAVE_ERROR err;

    // Initialize the Weave BleLayer.
    err = BleLayer::Init(this, this, &SystemLayer);
    SuccessOrExit(err);

    memset(mCons, 0, sizeof(mCons));
    mServiceMode = ConnectivityManager::kWoBLEServiceMode_Enabled;
    mAppIf = ESP_GATT_IF_NONE;
    mServiceAttrHandle = 0;
    mRXCharAttrHandle = 0;
    mTXCharAttrHandle = 0;
    mTXCharCCCDAttrHandle = 0;
    mFlags = kFlag_AdvertisingEnabled;
    memset(mDeviceName, 0, sizeof(mDeviceName));

    PlatformMgr.ScheduleWork(DriveBLEState, 0);

exit:
    return err;
}

WEAVE_ERROR BLEManager::SetWoBLEServiceMode(WoBLEServiceMode val)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;

    VerifyOrExit(val != ConnectivityManager::kWoBLEServiceMode_NotSupported, err = WEAVE_ERROR_INVALID_ARGUMENT);
    VerifyOrExit(mServiceMode == ConnectivityManager::kWoBLEServiceMode_NotSupported, err = WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE);

    if (val != mServiceMode)
    {
        mServiceMode = val;
        PlatformMgr.ScheduleWork(DriveBLEState, 0);
    }

exit:
    return err;
}

WEAVE_ERROR BLEManager::SetAdvertisingEnabled(bool val)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;

    VerifyOrExit(mServiceMode == ConnectivityManager::kWoBLEServiceMode_NotSupported, err = WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE);

    if (val != mServiceMode)
    {
        SetFlag(mFlags, kFlag_AdvertisingEnabled, val);
        PlatformMgr.ScheduleWork(DriveBLEState, 0);
    }

exit:
    return err;
}

WEAVE_ERROR BLEManager::SetFastAdvertisingEnabled(bool val)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;

    VerifyOrExit(mServiceMode == ConnectivityManager::kWoBLEServiceMode_NotSupported, err = WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE);

    if (val != mServiceMode)
    {
        SetFlag(mFlags, kFlag_FastAdvertisingEnabled, val);
        PlatformMgr.ScheduleWork(DriveBLEState, 0);
    }

exit:
    return err;
}

WEAVE_ERROR BLEManager::GetDeviceName(char * buf, size_t bufSize)
{
    if (strlen(mDeviceName) >= bufSize)
    {
        return WEAVE_ERROR_BUFFER_TOO_SMALL;
    }
    strcpy(buf, mDeviceName);
    return WEAVE_NO_ERROR;
}

WEAVE_ERROR BLEManager::SetDeviceName(const char * deviceName)
{
    if (mServiceMode == ConnectivityManager::kWoBLEServiceMode_NotSupported)
    {
        return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
    }
    if (deviceName != NULL && deviceName[0] != 0)
    {
        if (strlen(deviceName) >= kMaxDeviceNameLength)
        {
            return WEAVE_ERROR_INVALID_ARGUMENT;
        }
        strcpy(mDeviceName, deviceName);
        SetFlag(mFlags, kFlag_UseCustomDeviceName);
    }
    else
    {
        mDeviceName[0] = 0;
        ClearFlag(mFlags, kFlag_UseCustomDeviceName);
    }
    return WEAVE_NO_ERROR;
}

void BLEManager::OnPlatformEvent(const WeaveDeviceEvent * event)
{
    switch (event->Type)
    {
    case WeaveDeviceEvent::kInternalEventType_WoBLESubscribe:
        HandleSubscribeReceived(event->WoBLESubscribe.ConId, &WEAVE_BLE_SVC_ID, &WeaveUUID_WoBLEChar_TX);
        break;

    case WeaveDeviceEvent::kInternalEventType_WoBLEUnsubscribe:
        HandleUnsubscribeReceived(event->WoBLEUnsubscribe.ConId, &WEAVE_BLE_SVC_ID, &WeaveUUID_WoBLEChar_TX);
        break;

    case WeaveDeviceEvent::kInternalEventType_WoBLEWriteReceived:
        HandleWriteReceived(event->WoBLEWriteReceived.ConId, &WEAVE_BLE_SVC_ID, &WeaveUUID_WoBLEChar_RX, event->WoBLEWriteReceived.Data);
        break;

    case WeaveDeviceEvent::kInternalEventType_WoBLEIndicateConfirm:
        HandleIndicationConfirmation(event->WoBLEIndicateConfirm.ConId, &WEAVE_BLE_SVC_ID, &WeaveUUID_WoBLEChar_TX);
        break;

    case WeaveDeviceEvent::kInternalEventType_WoBLEConnectionError:
        HandleConnectionError(event->WoBLEConnectionError.ConId, event->WoBLEConnectionError.Reason);
        break;

    default:
        break;
    }
}

bool BLEManager::SubscribeCharacteristic(uint16_t conId, const WeaveBleUUID * svcId, const WeaveBleUUID * charId)
{
    ESP_LOGI(TAG, "BLEManager::SubscribeCharacteristic() not supported");
    return false;
}

bool BLEManager::UnsubscribeCharacteristic(uint16_t conId, const WeaveBleUUID * svcId, const WeaveBleUUID * charId)
{
    ESP_LOGI(TAG, "BLEManager::UnsubscribeCharacteristic() not supported");
    return false;
}

bool BLEManager::CloseConnection(uint16_t conId)
{
    WEAVE_ERROR err;

    ESP_LOGI(TAG, "Closing BLE GATT connection (con %u)", conId);

    // Signal the ESP BLE layer to close the conntion.
    err = esp_ble_gatts_close(mAppIf, conId);
    {
        ESP_LOGE(TAG, "esp_ble_gatts_close() failed: %s", esp_err_to_name(err));
    }

    // Release the associated connection state record.
    ReleaseConnectionState(conId);

    // Arrange to re-enable connectable advertising in case it was disabled due to the
    // maximum connection limit being reached.
    ClearFlag(mFlags, kFlag_Advertising);
    PlatformMgr.ScheduleWork(DriveBLEState, 0);

    return (err == WEAVE_NO_ERROR);
}

uint16_t BLEManager::GetMTU(uint16_t conId) const
{
    WoBLEConState * conState = const_cast<BLEManager *>(this)->GetConnectionState(conId);
    return (conState != NULL) ? conState->MTU : 0;
}

bool BLEManager::SendIndication(uint16_t conId, const WeaveBleUUID * svcId, const WeaveBleUUID * charId, PacketBuffer * data)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    WoBLEConState * conState = GetConnectionState(conId);

    ESP_LOGD(TAG, "Sending indication for WoBLE TX characteristic (con %u, len %u)", conId, data->DataLength());

    VerifyOrExit(conState != NULL, err = WEAVE_ERROR_INVALID_ARGUMENT);

    VerifyOrExit(conState->PendingIndBuf == NULL, err = WEAVE_ERROR_INCORRECT_STATE);

    err = esp_ble_gatts_send_indicate(mAppIf, conId, mTXCharAttrHandle, data->DataLength(), data->Start(), false);
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "esp_ble_gatts_send_indicate() failed: %s", esp_err_to_name(err));
        ExitNow();
    }

    // Save a reference to the buffer until we get a indication from the ESP BLE layer that it
    // has been sent.
    conState->PendingIndBuf = data;
    data = NULL;

exit:
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "BLEManager::SendIndication() failed: %s", ErrorStr(err));
        PacketBuffer::Free(data);
        return false;
    }
    return true;
}

bool BLEManager::SendWriteRequest(uint16_t conId, const WeaveBleUUID * svcId, const WeaveBleUUID * charId, PacketBuffer * pBuf)
{
    ESP_LOGE(TAG, "BLEManager::SendWriteRequest() not supported");
    return false;
}

bool BLEManager::SendReadRequest(uint16_t conId, const WeaveBleUUID * svcId, const WeaveBleUUID * charId, PacketBuffer * pBuf)
{
    ESP_LOGE(TAG, "BLEManager::SendReadRequest() not supported");
    return false;
}

bool BLEManager::SendReadResponse(uint16_t conId, BLE_READ_REQUEST_CONTEXT requestContext, const WeaveBleUUID * svcId, const WeaveBleUUID * charId)
{
    ESP_LOGE(TAG, "BLEManager::SendReadResponse() not supported");
    return false;
}

void BLEManager::NotifyWeaveConnectionClosed(uint16_t conId)
{
}

void BLEManager::DriveBLEState(void)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;

    // If there's already a control operation in progress, wait until it completes.
    VerifyOrExit(!GetFlag(mFlags, kFlag_ControlOpInProgress), /* */);

    // Initializes the ESP BLE layer if needed.
    if (mServiceMode == ConnectivityManager::kWoBLEServiceMode_Enabled && !GetFlag(mFlags, kFlag_ESPBLELayerInitialized))
    {
        err = InitESPBleLayer();
        SuccessOrExit(err);
    }

    // Register the WoBLE application with the ESP BLE layer if needed.
    if (mServiceMode == ConnectivityManager::kWoBLEServiceMode_Enabled && !GetFlag(mFlags, kFlag_AppRegistered))
    {
        err = esp_ble_gatts_app_register(WoBLEAppId);
        if (err != WEAVE_NO_ERROR)
        {
            ESP_LOGE(TAG, "esp_ble_gatts_app_register() failed: %s", esp_err_to_name(err));
            ExitNow();
        }

        SetFlag(mFlags, kFlag_ControlOpInProgress);

        ExitNow();
    }

    // Register the WoBLE GATT attributes with the ESP BLE layer if needed.
    if (mServiceMode == ConnectivityManager::kWoBLEServiceMode_Enabled && !GetFlag(mFlags, kFlag_AttrsRegistered))
    {
        err = esp_ble_gatts_create_attr_tab(WoBLEGATTAttrs, mAppIf, WoBLEGATTAttrCount, 0);
        if (err != WEAVE_NO_ERROR)
        {
            ESP_LOGE(TAG, "esp_ble_gatts_create_attr_tab() failed: %s", esp_err_to_name(err));
            ExitNow();
        }

        SetFlag(mFlags, kFlag_ControlOpInProgress);

        ExitNow();
    }

    // Start the WoBLE GATT service if needed.
    if (mServiceMode == ConnectivityManager::kWoBLEServiceMode_Enabled && !GetFlag(mFlags, kFlag_GATTServiceStarted))
    {
        err = esp_ble_gatts_start_service(mServiceAttrHandle);
        if (err != WEAVE_NO_ERROR)
        {
            ESP_LOGE(TAG, "esp_ble_gatts_start_service() failed: %s", esp_err_to_name(err));
            ExitNow();
        }

        SetFlag(mFlags, kFlag_ControlOpInProgress);

        ExitNow();
    }

    // Start advertising if needed...
    if (mServiceMode == ConnectivityManager::kWoBLEServiceMode_Enabled && GetFlag(mFlags, kFlag_AdvertisingEnabled))
    {
        // Configure advertising data if needed.
        if (!GetFlag(mFlags, kFlag_AdvertisingConfigured))
        {
            err = ConfigureAdvertisingData();
            ExitNow();
        }

        // Start advertising if needed.
        if (!GetFlag(mFlags, kFlag_Advertising))
        {
            err = StartAdvertising();
            ExitNow();
        }
    }

    // Otherwise stop advertising if needed...
    else
    {
        if (GetFlag(mFlags, kFlag_Advertising))
        {
            err = esp_ble_gap_stop_advertising();
            if (err != WEAVE_NO_ERROR)
            {
                ESP_LOGE(TAG, "esp_ble_gap_stop_advertising() failed: %s", esp_err_to_name(err));
                ExitNow();
            }

            SetFlag(mFlags, kFlag_ControlOpInProgress);

            ExitNow();
        }
    }

    // Stop the WoBLE GATT service if needed.
    if (mServiceMode != ConnectivityManager::kWoBLEServiceMode_Enabled && GetFlag(mFlags, kFlag_GATTServiceStarted))
    {
        // TODO: what to do about existing connections??

        err = esp_ble_gatts_stop_service(mServiceAttrHandle);
        if (err != WEAVE_NO_ERROR)
        {
            ESP_LOGE(TAG, "esp_ble_gatts_stop_service() failed: %s", esp_err_to_name(err));
            ExitNow();
        }

        SetFlag(mFlags, kFlag_ControlOpInProgress);

        ExitNow();
    }

exit:
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "Disabling WoBLE service due to error: %s", ErrorStr(err));
        mServiceMode = ConnectivityManager::kWoBLEServiceMode_Disabled;
    }
}

WEAVE_ERROR BLEManager::InitESPBleLayer(void)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;

    VerifyOrExit(!GetFlag(mFlags, kFlag_ESPBLELayerInitialized), /* */);

    // If the ESP Bluetooth controller has not been initialized...
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE)
    {
        // Since Weave only uses BLE, release memory held by ESP classic bluetooth stack.
        err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
        if (err != WEAVE_NO_ERROR)
        {
            ESP_LOGE(TAG, "esp_bt_controller_mem_release() failed: %s", esp_err_to_name(err));
            ExitNow();
        }

        // Initialize the ESP Bluetooth controller.
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        err = esp_bt_controller_init(&bt_cfg);
        if (err != WEAVE_NO_ERROR)
        {
            ESP_LOGE(TAG, "esp_bt_controller_init() failed: %s", esp_err_to_name(err));
            ExitNow();
        }
    }

    // If the ESP Bluetooth controller has not been enabled, enable it now.
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED)
    {
        err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if (err != WEAVE_NO_ERROR)
        {
            ESP_LOGE(TAG, "esp_bt_controller_enable() failed: %s", esp_err_to_name(err));
            ExitNow();
        }
    }

    // If the ESP Bluedroid stack has not been initialized, initialize it now.
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED)
    {
        err = esp_bluedroid_init();
        if (err != WEAVE_NO_ERROR)
        {
            ESP_LOGE(TAG, "esp_bluedroid_init() failed: %s", esp_err_to_name(err));
            ExitNow();
        }
    }

    // If the ESP Bluedroid stack has not been enabled, enable it now.
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED)
    {
        err = esp_bluedroid_enable();
        if (err != WEAVE_NO_ERROR)
        {
            ESP_LOGE(TAG, "esp_bluedroid_enable() failed: %s", esp_err_to_name(err));
            ExitNow();
        }
    }

    // Register a callback to receive GATT events.
    err = esp_ble_gatts_register_callback(HandleGATTEvent);
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "esp_ble_gatts_register_callback() failed: %s", esp_err_to_name(err));
        ExitNow();
    }

    // Register a callback to receive GAP events.
    err = esp_ble_gap_register_callback(HandleGAPEvent);
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "esp_ble_gap_register_callback() failed: %s", esp_err_to_name(err));
        ExitNow();
    }

    // Set the maximum supported MTU size.
    err = esp_ble_gatt_set_local_mtu(ESP_GATT_MAX_MTU_SIZE);
    if (err != WEAVE_NO_ERROR){
        ESP_LOGE(TAG, "esp_ble_gatt_set_local_mtu() failed: %s", esp_err_to_name(err));
    }
    SuccessOrExit(err);

    SetFlag(mFlags, kFlag_ESPBLELayerInitialized);

exit:
    return err;
}

WEAVE_ERROR BLEManager::ConfigureAdvertisingData(void)
{
    WEAVE_ERROR err;
    esp_ble_adv_data_t advertData;
    WeaveServiceData weaveServiceData;

    // If a custom device name has not been specified, generate a Nest-standard name based on the
    // bottom digits of the Weave device id.
    if (!GetFlag(mFlags, kFlag_UseCustomDeviceName))
    {
        snprintf(mDeviceName, sizeof(mDeviceName), "%s%04" PRIX32,
                 WEAVE_PLATFORM_CONFIG_BLE_DEVICE_NAME_PREFIX,
                 (uint32_t)FabricState.LocalNodeId);
        mDeviceName[kMaxDeviceNameLength] = 0;
    }

    // Configure the BLE device name.
    err = esp_ble_gap_set_device_name(mDeviceName);
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "esp_ble_gap_set_device_name() failed: %s", esp_err_to_name(err));
        ExitNow();
    }

    // Configure the contents of the advertising packet.
    memset(&advertData, 0, sizeof(advertData));
    advertData.set_scan_rsp = false;
    advertData.include_name = true;
    advertData.include_txpower = false;
    advertData.min_interval = 0;
    advertData.max_interval = 0;
    advertData.appearance = 0;
    advertData.manufacturer_len = 0;
    advertData.p_manufacturer_data = NULL;
    advertData.service_data_len = 0;
    advertData.p_service_data = NULL;
    advertData.service_uuid_len = sizeof(UUID_WoBLEService);
    advertData.p_service_uuid = (uint8_t *)UUID_WoBLEService;
    advertData.flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
    err = esp_ble_gap_config_adv_data(&advertData);
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "esp_ble_gap_config_adv_data(<advertising data>) failed: %s", esp_err_to_name(err));
        ExitNow();
    }

    // Construct the Weave Service Data to be sent in the scan response packet.
    memcpy(weaveServiceData.ServiceUUID, ShortUUID_WoBLEService, sizeof(weaveServiceData.ServiceUUID));
    weaveServiceData.DataBlockLen = 16;
    weaveServiceData.DataBlockType = 1;
    weaveServiceData.DataBlockMajorVersion = 0;
    weaveServiceData.DataBlockMinorVersion = 1;
    Encoding::LittleEndian::Put16(weaveServiceData.DeviceVendorId, (uint16_t)WEAVE_PLATFORM_CONFIG_DEVICE_VENDOR_ID);
    Encoding::LittleEndian::Put16(weaveServiceData.DeviceProductId, (uint16_t)WEAVE_PLATFORM_CONFIG_DEVICE_PRODUCT_ID);
    Encoding::LittleEndian::Put64(weaveServiceData.DeviceId, FabricState.LocalNodeId);
    weaveServiceData.PairingStatus = ConfigurationMgr.IsPairedToAccount() ? 1 : 0;

    // Configure the contents of the scan response packet.
    memset(&advertData, 0, sizeof(advertData));
    advertData.set_scan_rsp = true;
    advertData.include_name = false;
    advertData.include_txpower = true;
    advertData.min_interval = 0;
    advertData.max_interval = 0;
    advertData.appearance = 0;
    advertData.manufacturer_len = 0;
    advertData.p_manufacturer_data = NULL;
    advertData.service_data_len = sizeof(weaveServiceData);
    advertData.p_service_data = (uint8_t *)&weaveServiceData;
    advertData.service_uuid_len = 0;
    advertData.p_service_uuid = NULL;
    advertData.flag = 0;
    err = esp_ble_gap_config_adv_data(&advertData);
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "esp_ble_gap_config_adv_data(<scan response>) failed: %s", esp_err_to_name(err));
        ExitNow();
    }

    SetFlag(mFlags, kFlag_ControlOpInProgress);

exit:
    return err;
}

WEAVE_ERROR BLEManager::StartAdvertising(void)
{
    WEAVE_ERROR err;
    esp_ble_adv_params_t advertParams =
    {
        0,                                  // adv_int_min
        0,                                  // adv_int_max
        ADV_TYPE_IND,                       // adv_type
        BLE_ADDR_TYPE_PUBLIC,               // own_addr_type
        { 0 },                              // peer_addr
        BLE_ADDR_TYPE_RANDOM,               // peer_addr_type
        ADV_CHNL_ALL,                       // channel_map
        ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,  // adv_filter_policy
    };

    // Advertise connectable if we haven't reached the maximum number of connections.
    size_t numCons = NumConnections();
    bool connectable = (numCons < kMaxConnections);
    advertParams.adv_type = connectable ? ADV_TYPE_IND : ADV_TYPE_NONCONN_IND;

    // Advertise in fast mode if not paired to an account and there are no WoBLE connections.
    advertParams.adv_int_min = advertParams.adv_int_max =
        (numCons == 0 && !ConfigurationMgr.IsPairedToAccount())
        ? WEAVE_PLATFORM_CONFIG_BLE_FAST_ADVERTISING_INTERVAL
        : WEAVE_PLATFORM_CONFIG_BLE_SLOW_ADVERTISING_INTERVAL;

    ESP_LOGI(TAG, "Configuring BLE advertising (interval %" PRIu32 " ms, %sconnectable, device name %s)",
             (((uint32_t)advertParams.adv_int_min) * 10) / 16,
             (connectable) ? "" : "non-",
             mDeviceName);

    err = esp_ble_gap_start_advertising(&advertParams);
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "esp_ble_gap_start_advertising() failed: %s", esp_err_to_name(err));
        ExitNow();
    }

    SetFlag(mFlags, kFlag_ControlOpInProgress);

exit:
    return err;
}

void BLEManager::HandleGATTControlEvent(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t * param)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    bool controlOpComplete = false;

    // Ignore GATT control events that do not pertain to the WoBLE application, except for ESP_GATTS_REG_EVT.
    if (event != ESP_GATTS_REG_EVT && (!GetFlag(mFlags, kFlag_AppRegistered) || gatts_if != mAppIf))
    {
        ExitNow();
    }

    switch (event)
    {
    case ESP_GATTS_REG_EVT:

        if (param->reg.app_id == WoBLEAppId)
        {
            if (param->reg.status != ESP_GATT_OK)
            {
                ESP_LOGE(TAG, "ESP_GATTS_REG_EVT error: %d", (int)param->reg.status);
                ExitNow(err = ESP_ERR_INVALID_RESPONSE);
            }

            // Save the 'interface type' assigned to the WoBLE application by the ESP BLE layer.
            mAppIf = gatts_if;

            SetFlag(mFlags, kFlag_AppRegistered);
            controlOpComplete = true;
        }

        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:

        if (param->add_attr_tab.status != ESP_GATT_OK)
        {
            ESP_LOGE(TAG, "ESP_GATTS_CREAT_ATTR_TAB_EVT error: %d", (int)param->add_attr_tab.status);
            ExitNow(err = ESP_ERR_INVALID_RESPONSE);
        }

        // Save the attribute handles assigned by the ESP BLE layer to the WoBLE attributes.
        mServiceAttrHandle = param->add_attr_tab.handles[kAttrIndex_ServiceDeclaration];
        mRXCharAttrHandle = param->add_attr_tab.handles[kAttrIndex_RXCharValue];
        mTXCharAttrHandle = param->add_attr_tab.handles[kAttrIndex_TXCharValue];
        mTXCharCCCDAttrHandle = param->add_attr_tab.handles[kAttrIndex_TXCharCCCDValue];

        SetFlag(mFlags, kFlag_AttrsRegistered);
        controlOpComplete = true;

        break;

    case ESP_GATTS_START_EVT:

        if (param->start.status != ESP_GATT_OK)
        {
            ESP_LOGE(TAG, "ESP_GATTS_START_EVT error: %d", (int)param->start.status);
            ExitNow(err = ESP_ERR_INVALID_RESPONSE);
        }

        ESP_LOGI(TAG, "WoBLE GATT service started");

        SetFlag(mFlags, kFlag_GATTServiceStarted);
        controlOpComplete = true;

        break;


    case ESP_GATTS_STOP_EVT:

        if (param->stop.status != ESP_GATT_OK)
        {
            ESP_LOGE(TAG, "ESP_GATTS_STOP_EVT error: %d", (int)param->stop.status);
            ExitNow(err = ESP_ERR_INVALID_RESPONSE);
        }

        ESP_LOGI(TAG, "WoBLE GATT service stopped");

        ClearFlag(mFlags, kFlag_GATTServiceStarted);
        controlOpComplete = true;

        break;

    case ESP_GATTS_RESPONSE_EVT:
        ESP_LOGD(TAG, "ESP_GATTS_RESPONSE_EVT (handle %u, status %d)", param->rsp.handle, (int)param->rsp.status);
        break;

    default:
        // Ignore all other event types.
        break;
    }

exit:
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "Disabling WoBLE service due to error: %s", ErrorStr(err));
        mServiceMode = ConnectivityManager::kWoBLEServiceMode_Disabled;
    }
    if (controlOpComplete)
    {
        ClearFlag(mFlags, kFlag_ControlOpInProgress);
        PlatformMgr.ScheduleWork(DriveBLEState, 0);
    }
}

void BLEManager::HandleGATTCommEvent(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t * param)
{
    // Ignore the event if the WoBLE service hasn't been started, or if the event is for a different
    // BLE application.
    if (!GetFlag(BLEMgr.mFlags, kFlag_GATTServiceStarted) || gatts_if != BLEMgr.mAppIf)
    {
        return;
    }

    switch (event)
    {
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "BLE GATT connection established (con %u)", param->connect.conn_id);

        // Allocate a connection state record for the new connection.
        GetConnectionState(param->mtu.conn_id, true);

        // Receiving a connection stops the advertising processes, so force it to be re-enabled.
        ClearFlag(mFlags, kFlag_Advertising);
        PlatformMgr.ScheduleWork(DriveBLEState, 0);

        break;

    case ESP_GATTS_DISCONNECT_EVT:
        HandleDisconnect(param);
        break;

    case ESP_GATTS_READ_EVT:
        if (param->read.handle == mTXCharAttrHandle)
        {
            HandleTXCharRead(param);
        }
        if (param->read.handle == mTXCharCCCDAttrHandle)
        {
            HandleTXCharCCCDRead(param);
        }
        break;

    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == mRXCharAttrHandle)
        {
            HandleRXCharWrite(param);
        }
        if (param->write.handle == mTXCharCCCDAttrHandle)
        {
            HandleTXCharCCCDWrite(param);
        }
        break;

    case ESP_GATTS_CONF_EVT:
        {
            WoBLEConState * conState = GetConnectionState(param->conf.conn_id);
            if (conState != NULL)
            {
                HandleTXCharConfirm(conState, param);
            }
        }
        break;

    case ESP_GATTS_MTU_EVT:
        {
            ESP_LOGD(TAG, "MTU for con %u: %u", param->mtu.conn_id, param->mtu.mtu);
            WoBLEConState * conState = GetConnectionState(param->mtu.conn_id);
            if (conState != NULL)
            {
                conState->MTU = param->mtu.mtu;
            }
        }
        break;

    default:
        break;
    }
}

void BLEManager::HandleRXCharWrite(esp_ble_gatts_cb_param_t * param)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    PacketBuffer * buf = NULL;
    bool needResp = param->write.need_rsp;

    ESP_LOGD(TAG, "Write request received for WoBLE RX characteristic (con %u, len %u)", param->write.conn_id, param->write.len);

    // Disallow long writes.
    VerifyOrExit(param->write.is_prep == false, err = WEAVE_ERROR_INVALID_ARGUMENT);

    // Copy the data to a PacketBuffer.
    buf = PacketBuffer::New(0);
    VerifyOrExit(buf != NULL, err = WEAVE_ERROR_NO_MEMORY);
    VerifyOrExit(buf->AvailableDataLength() >= param->write.len, err = WEAVE_ERROR_BUFFER_TOO_SMALL);
    memcpy(buf->Start(), param->write.value, param->write.len);
    buf->SetDataLength(param->write.len);

    // Send a response if requested.
    if (needResp)
    {
        esp_ble_gatts_send_response(mAppIf, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        needResp = false;
    }

    // Post an event to the Weave queue to deliver the data into the Weave stack.
    {
        WeaveDeviceEvent event;
        event.Type = WeaveDeviceEvent::kInternalEventType_WoBLEWriteReceived;
        event.WoBLEWriteReceived.ConId = param->write.conn_id;
        event.WoBLEWriteReceived.Data = buf;
        PlatformMgr.PostEvent(&event);
        buf = NULL;
    }

exit:
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "HandleRXCharWrite() failed: %s", ErrorStr(err));
        if (needResp)
        {
            esp_ble_gatts_send_response(mAppIf, param->write.conn_id, param->write.trans_id, ESP_GATT_INTERNAL_ERROR, NULL);
        }
        // TODO: fail connection???
    }
    PacketBuffer::Free(buf);
}

void BLEManager::HandleTXCharRead(esp_ble_gatts_cb_param_t * param)
{
    WEAVE_ERROR err;
    esp_gatt_rsp_t rsp;

    ESP_LOGD(TAG, "Read request received for WoBLE TX characteristic (con %u)", param->read.conn_id);

    // Send a zero-length response.
    memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
    rsp.attr_value.handle = param->read.handle;
    err = esp_ble_gatts_send_response(mAppIf, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "esp_ble_gatts_send_response() failed: %s", esp_err_to_name(err));
    }
}

void BLEManager::HandleTXCharCCCDRead(esp_ble_gatts_cb_param_t * param)
{
    WEAVE_ERROR err;
    WoBLEConState * conState;
    esp_gatt_rsp_t rsp;

    ESP_LOGD(TAG, "Read request received for WoBLE TX characteristic CCCD (con %u)", param->read.conn_id);

    // Find the connection state record.
    conState = GetConnectionState(param->read.conn_id);

    // Send current CCCD value, or an error if we failed to allocate a connection state object.
    memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
    rsp.attr_value.handle = param->read.handle;
    if (conState != NULL)
    {
        rsp.attr_value.len = 2;
        rsp.attr_value.value[0] = conState->Subscribed ? 1 : 0;
    }
    err = esp_ble_gatts_send_response(mAppIf, param->read.conn_id, param->read.trans_id, (conState != NULL) ? ESP_GATT_OK : ESP_GATT_INTERNAL_ERROR, &rsp);
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "esp_ble_gatts_send_response() failed: %s", esp_err_to_name(err));
    }
}

void BLEManager::HandleTXCharCCCDWrite(esp_ble_gatts_cb_param_t * param)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    WoBLEConState * conState;
    bool needResp = param->write.need_rsp;
    bool indicationsEnabled;

    ESP_LOGD(TAG, "Write request received for WoBLE TX characteristic CCCD (con %u, len %u)", param->write.conn_id, param->write.len);

    // Find the connection state record.
    conState = GetConnectionState(param->read.conn_id);
    VerifyOrExit(conState != NULL, err = WEAVE_ERROR_NO_MEMORY);

    // Disallow long writes.
    VerifyOrExit(param->write.is_prep == false, err = WEAVE_ERROR_INVALID_ARGUMENT);

    // Determine if the client is enabling or disabling indications.
    indicationsEnabled = (param->write.len > 0 && (param->write.value[0] != 0));

    // Send a response to the Write if requested.
    if (needResp)
    {
        esp_ble_gatts_send_response(mAppIf, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        needResp = false;
    }

    // Post an event to the Weave queue to process either a WoBLE Subscribe or Unsubscribe based on
    // whether the client is enabling or disabling indications.
    {
        WeaveDeviceEvent event;
        event.Type = (indicationsEnabled) ? WeaveDeviceEvent::kInternalEventType_WoBLESubscribe : WeaveDeviceEvent::kInternalEventType_WoBLEUnsubscribe;
        event.WoBLESubscribe.ConId = param->write.conn_id;
        PlatformMgr.PostEvent(&event);
    }

    ESP_LOGI(TAG, "WoBLE %s received", indicationsEnabled ? "subscribe" : "unsubscribe");

exit:
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "HandleTXCharCCCDWrite() failed: %s", ErrorStr(err));
        if (needResp)
        {
            esp_ble_gatts_send_response(mAppIf, param->write.conn_id, param->write.trans_id, ESP_GATT_INTERNAL_ERROR, NULL);
        }
        // TODO: fail connection???
    }
}

void BLEManager::HandleTXCharConfirm(WoBLEConState * conState, esp_ble_gatts_cb_param_t * param)
{
    ESP_LOGD(TAG, "Confirm received for WoBLE TX characteristic indication (con %u, status %u)", param->conf.conn_id, param->conf.status);

    // If there is a pending indication buffer for the connection, release it now.
    PacketBuffer::Free(conState->PendingIndBuf);
    conState->PendingIndBuf = NULL;

    // If the confirmation was successful...
    if (param->conf.status == ESP_GATT_OK)
    {
        // Post an event to the Weave queue to process the indicate confirmation.
        WeaveDeviceEvent event;
        event.Type = WeaveDeviceEvent::kInternalEventType_WoBLEIndicateConfirm;
        event.WoBLEIndicateConfirm.ConId = param->conf.conn_id;
        PlatformMgr.PostEvent(&event);
    }

    else
    {
        WeaveDeviceEvent event;
        event.Type = WeaveDeviceEvent::kInternalEventType_WoBLEConnectionError;
        event.WoBLEConnectionError.ConId = param->disconnect.conn_id;
        event.WoBLEConnectionError.Reason = BLE_ERROR_WOBLE_PROTOCOL_ABORT;
        PlatformMgr.PostEvent(&event);
    }
}

void BLEManager::HandleDisconnect(esp_ble_gatts_cb_param_t * param)
{
    ESP_LOGI(TAG, "BLE GATT connection closed (con %u, reason %u)", param->disconnect.conn_id, param->disconnect.reason);

    // If this was a WoBLE connection, release the associated connection state record
    // and post an event to deliver a connection error to the WoBLE layer.
    if (ReleaseConnectionState(param->disconnect.conn_id))
    {
        WeaveDeviceEvent event;
        event.Type = WeaveDeviceEvent::kInternalEventType_WoBLEConnectionError;
        event.WoBLEConnectionError.ConId = param->disconnect.conn_id;
        switch (param->disconnect.reason)
        {
        case ESP_GATT_CONN_TERMINATE_PEER_USER:
            event.WoBLEConnectionError.Reason = BLE_ERROR_REMOTE_DEVICE_DISCONNECTED;
            break;
        case ESP_GATT_CONN_TERMINATE_LOCAL_HOST:
            event.WoBLEConnectionError.Reason = BLE_ERROR_APP_CLOSED_CONNECTION;
            break;
        default:
            event.WoBLEConnectionError.Reason = BLE_ERROR_WOBLE_PROTOCOL_ABORT;
            break;
        }
        PlatformMgr.PostEvent(&event);

        // Arrange to re-enable connectable advertising in case it was disabled due to the
        // maximum connection limit being reached.
        ClearFlag(mFlags, kFlag_Advertising);
        PlatformMgr.ScheduleWork(DriveBLEState, 0);
    }
}

BLEManager::WoBLEConState * BLEManager::GetConnectionState(uint16_t conId, bool allocate)
{
    size_t freeIndex = kMaxConnections;

    for (size_t i = 0; i < kMaxConnections; i++)
    {
        if (mCons[i].Allocated == 1)
        {
            if (mCons[i].ConId == conId)
            {
                return &mCons[i];
            }
        }

        else if (i < freeIndex)
        {
            freeIndex = i;
        }
    }

    if (allocate)
    {
        if (freeIndex < kMaxConnections)
        {
            memset(&mCons[freeIndex], 0, sizeof(WoBLEConState));
            mCons[freeIndex].Allocated = 1;
            mCons[freeIndex].ConId = conId;
            return &mCons[freeIndex];
        }

        ESP_LOGE(TAG, "Failed to allocate WoBLEConState");
    }

    return NULL;
}

bool BLEManager::ReleaseConnectionState(uint16_t conId)
{
    for (size_t i = 0; i < kMaxConnections; i++)
    {
        if (mCons[i].Allocated && mCons[i].ConId == conId)
        {
            PacketBuffer::Free(mCons[i].PendingIndBuf);
            mCons[i].Allocated = 0;
            return true;
        }
    }

    return false;
}

size_t BLEManager::NumConnections(void)
{
    size_t numCons = 0;
    for (size_t i = 0; i < kMaxConnections; i++)
    {
        if (mCons[i].Allocated)
        {
            numCons++;
        }
    }

    return numCons;
}

void BLEManager::HandleGATTEvent(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t * param)
{
    ESP_LOGV(TAG, "GATT Event: %d (if %d)", (int)event, (int)gatts_if);

    // This method is invoked on the ESP BLE thread.  Therefore we must hold a lock
    // on the Weave stack while processing the event.
    PlatformMgr.LockWeaveStack();

    BLEMgr.HandleGATTControlEvent(event, gatts_if, param);
    BLEMgr.HandleGATTCommEvent(event, gatts_if, param);

    PlatformMgr.UnlockWeaveStack();
}

void BLEManager::HandleGAPEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;

    ESP_LOGV(TAG, "GAP Event: %d", (int)event);

    // This method is invoked on the ESP BLE thread.  Therefore we must hold a lock
    // on the Weave stack while processing the event.
    PlatformMgr.LockWeaveStack();

    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:

        if (param->adv_data_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT error: %d", (int)param->adv_data_cmpl.status);
            ExitNow(err = ESP_ERR_INVALID_RESPONSE);
        }

        SetFlag(BLEMgr.mFlags, kFlag_AdvertisingConfigured);
        ClearFlag(BLEMgr.mFlags, kFlag_ControlOpInProgress);

        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:

        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "ESP_GAP_BLE_ADV_START_COMPLETE_EVT error: %d", (int)param->adv_start_cmpl.status);
            ExitNow(err = ESP_ERR_INVALID_RESPONSE);
        }

        ESP_LOGI(TAG, "BLE advertising started");

        SetFlag(BLEMgr.mFlags, kFlag_Advertising);
        ClearFlag(BLEMgr.mFlags, kFlag_ControlOpInProgress);

        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:

        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT error: %d", (int)param->adv_stop_cmpl.status);
            ExitNow(err = ESP_ERR_INVALID_RESPONSE);
        }

        ESP_LOGI(TAG, "BLE advertising stopped");

        ClearFlag(BLEMgr.mFlags, kFlag_Advertising);
        ClearFlag(BLEMgr.mFlags, kFlag_ControlOpInProgress);

        break;

    default:
        break;
    }

exit:
    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "Disabling WoBLE service due to error: %s", ErrorStr(err));
        BLEMgr.mServiceMode = ConnectivityManager::kWoBLEServiceMode_Disabled;
    }
    PlatformMgr.ScheduleWork(Internal::BLEMgr.DriveBLEState, 0);
    PlatformMgr.UnlockWeaveStack();
}

void BLEManager::DriveBLEState(intptr_t arg)
{
    BLEMgr.DriveBLEState();
}


} // namespace Internal
} // namespace Device
} // namespace Weave
} // namespace nl
