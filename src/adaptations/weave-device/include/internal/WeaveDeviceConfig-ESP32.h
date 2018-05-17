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

#ifndef WEAVE_DEVICE_CONFIG_ESP32_H
#define WEAVE_DEVICE_CONFIG_ESP32_H

/* Map device configuration values onto the values generated by ESP32 Kconfig.
 */
#define WEAVE_DEVICE_CONFIG_WEAVE_TASK_STACK_SIZE CONFIG_WEAVE_TASK_STACK_SIZE
#define WEAVE_DEVICE_CONFIG_WEAVE_TASK_PRIORITY CONFIG_WEAVE_TASK_PRIORITY
#define WEAVE_DEVICE_CONFIG_MAX_EVENT_QUEUE_SIZE CONFIG_MAX_EVENT_QUEUE_SIZE
#define WEAVE_DEVICE_CONFIG_SERVICE_DIRECTORY_CACHE_SIZE CONFIG_SERVICE_DIRECTORY_CACHE_SIZE
#define WEAVE_DEVICE_CONFIG_DEVICE_VENDOR_ID CONFIG_DEVICE_VENDOR_ID
#define WEAVE_DEVICE_CONFIG_DEVICE_PRODUCT_ID CONFIG_DEVICE_PRODUCT_ID
#define WEAVE_DEVICE_CONFIG_DEVICE_PRODUCT_REVISION CONFIG_DEVICE_PRODUCT_REVISION
#define WEAVE_DEVICE_CONFIG_DEVICE_FIRMWARE_REVISION CONFIG_DEVICE_FIRMWARE_REVISION
#define WEAVE_DEVICE_CONFIG_WIFI_STATION_RECONNECT_INTERVAL CONFIG_WIFI_STATION_RECONNECT_INTERVAL
#define WEAVE_DEVICE_CONFIG_MAX_SCAN_NETWORKS_RESULTS CONFIG_MAX_SCAN_NETWORKS_RESULTS
#define WEAVE_DEVICE_CONFIG_WIFI_SCAN_COMPLETION_TIMEOUT CONFIG_WIFI_SCAN_COMPLETION_TIMEOUT
#define WEAVE_DEVICE_CONFIG_WIFI_CONNECTIVITY_TIMEOUT CONFIG_WIFI_CONNECTIVITY_TIMEOUT
#define WEAVE_DEVICE_CONFIG_WIFI_AP_SSID_PREFIX CONFIG_WIFI_AP_SSID_PREFIX
#define WEAVE_DEVICE_CONFIG_WIFI_AP_CHANNEL CONFIG_WIFI_AP_CHANNEL
#define WEAVE_DEVICE_CONFIG_WIFI_AP_MAX_STATIONS CONFIG_WIFI_AP_MAX_STATIONS
#define WEAVE_DEVICE_CONFIG_WIFI_AP_BEACON_INTERVAL CONFIG_WIFI_AP_BEACON_INTERVAL
#define WEAVE_DEVICE_CONFIG_WIFI_AP_IDLE_TIMEOUT CONFIG_WIFI_AP_IDLE_TIMEOUT
#define WEAVE_DEVICE_CONFIG_ENABLE_WOBLE CONFIG_ENABLE_WOBLE
#define WEAVE_DEVICE_CONFIG_BLE_DEVICE_NAME_PREFIX CONFIG_BLE_DEVICE_NAME_PREFIX
#define WEAVE_DEVICE_CONFIG_BLE_FAST_ADVERTISING_INTERVAL CONFIG_BLE_FAST_ADVERTISING_INTERVAL
#define WEAVE_DEVICE_CONFIG_BLE_SLOW_ADVERTISING_INTERVAL CONFIG_BLE_SLOW_ADVERTISING_INTERVAL
#define WEAVE_DEVICE_CONFIG_ENABLE_SERVICE_DIRECTORY_TIME_SYNC CONFIG_ENABLE_SERVICE_DIRECTORY_TIME_SYNC
#define WEAVE_DEVICE_CONFIG_ENABLE_WEAVE_TIME_SERVICE_TIME_SYNC CONFIG_ENABLE_WEAVE_TIME_SERVICE_TIME_SYNC
#define WEAVE_DEVICE_CONFIG_WEAVE_TIME_SERVICE_ENDPOINT_ID CONFIG_WEAVE_TIME_SERVICE_ENDPOINT_ID
#define WEAVE_DEVICE_CONFIG_DEFAULT_TIME_SYNC_INTERVAL CONFIG_DEFAULT_TIME_SYNC_INTERVAL
#define WEAVE_DEVICE_CONFIG_TIME_SYNC_TIMEOUT CONFIG_TIME_SYNC_TIMEOUT
#define WEAVE_DEVICE_CONFIG_SERVICE_PROVISIONING_ENDPOINT_ID CONFIG_SERVICE_PROVISIONING_ENDPOINT_ID
#define WEAVE_DEVICE_CONFIG_SERVICE_PROVISIONING_CONNECTIVITY_TIMEOUT CONFIG_SERVICE_PROVISIONING_CONNECTIVITY_TIMEOUT
#define WEAVE_DEVICE_CONFIG_SERVICE_PROVISIONING_REQUEST_TIMEOUT CONFIG_SERVICE_PROVISIONING_REQUEST_TIMEOUT
#define WEAVE_DEVICE_CONFIG_ENABLE_TEST_DEVICE_IDENTITY CONFIG_ENABLE_TEST_DEVICE_IDENTITY
#define WEAVE_DEVICE_CONFIG_ENABLE_FIXED_TUNNEL_SERVER CONFIG_ENABLE_FIXED_TUNNEL_SERVER
#define WEAVE_DEVICE_CONFIG_TUNNEL_SERVER_ADDRESS CONFIG_TUNNEL_SERVER_ADDRESS
#define WEAVE_DEVICE_CONFIG_DISABLE_ACCOUNT_PAIRING CONFIG_DISABLE_ACCOUNT_PAIRING

#endif // WEAVE_DEVICE_CONFIG_ESP32_H
