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
#include <internal/ServiceProvisioningServer.h>

using namespace ::nl;
using namespace ::nl::Weave;
using namespace ::nl::Weave::Profiles;
using namespace ::nl::Weave::Profiles::ServiceProvisioning;

namespace nl {
namespace Weave {
namespace Device {
namespace Internal {

WEAVE_ERROR ServiceProvisioningServer::Init(void)
{
    WEAVE_ERROR err;

    // Call init on the server base class.
    err = ServerBaseClass::Init(&::nl::Weave::Device::ExchangeMgr);
    SuccessOrExit(err);

    // Set the pointer to the delegate object.
    SetDelegate(this);

    mProvServiceBinding = NULL;
    mAwaitingServiceConnectivity = false;

exit:
    return err;
}

WEAVE_ERROR ServiceProvisioningServer::HandleRegisterServicePairAccount(RegisterServicePairAccountMessage & msg)
{
    WEAVE_ERROR err;
    uint64_t curServiceId;

    // Check if a service is already provisioned. If so respond with "Too Many Services".
    err = ConfigurationMgr.GetServiceId(curServiceId);
    if (err == WEAVE_NO_ERROR)
    {
        err = ServiceProvisioningSvr.SendStatusReport(kWeaveProfile_ServiceProvisioning,
                (curServiceId == msg.ServiceId) ? kStatusCode_ServiceAlreadyRegistered : kStatusCode_TooManyServices);
        ExitNow();
    }
    if (err == WEAVE_PLATFORM_ERROR_CONFIG_NOT_FOUND)
    {
        err = WEAVE_NO_ERROR;
    }
    SuccessOrExit(err);

    // Validate the service config. We don't want to get any further along before making sure the data is good.
    if (!ServiceProvisioningServer::IsValidServiceConfig(msg.ServiceConfig, msg.ServiceConfigLen))
    {
        err = ServiceProvisioningSvr.SendStatusReport(kWeaveProfile_ServiceProvisioning, kStatusCode_InvalidServiceConfig);
        ExitNow();
    }

    ESP_LOGI(TAG, "Registering new service: %" PRIx64 " (account id %*s)", msg.ServiceId, (int)msg.AccountIdLen, msg.AccountId);

    // Store the service id and the service config in persistent storage.
    err = ConfigurationMgr.StoreServiceProvisioningData(msg.ServiceId, msg.ServiceConfig, msg.ServiceConfigLen, NULL, 0);
    SuccessOrExit(err);

    // Post an event alerting other subsystems to the change in the service provisioning state.
    {
        WeaveDeviceEvent event;
        event.Type = WeaveDeviceEvent::kEventType_ServiceProvisioningChange;
        event.ServiceProvisioningChange.IsServiceProvisioned = true;
        event.ServiceProvisioningChange.ServiceConfigUpdated = false;
        PlatformMgr.PostEvent(&event);
    }

#if !WEAVE_PLATFORM_CONFIG_DISABLE_ACCOUNT_PAIRING

    // Initiate the process of sending a PairDeviceToAccount request to the Service Provisioning service.
    PlatformMgr.ScheduleWork(AsyncStartPairDeviceToAccount);

#else // !WEAVE_PLATFORM_CONFIG_DISABLE_ACCOUNT_PAIRING

    // Store the account id in persistent storage.
    err = ConfigurationMgr.StoreAccountId(msg.AccountId, msg.AccountIdLen);
    SuccessOrExit(err);

    // Post an event alerting other subsystems that the device is now paired to an account.
    {
        WeaveDeviceEvent event;
        event.Type = WeaveDeviceEvent::kEventType_AccountPairingChange;
        event.AccountPairingChange.IsPairedToAccount = true;
        PlatformMgr.PostEvent(&event);
    }

    // Send a success StatusReport for the RegisterServicePairDevice request.
    SendSuccessResponse();

#endif // !WEAVE_PLATFORM_CONFIG_DISABLE_ACCOUNT_PAIRING

exit:
    return err;
}

WEAVE_ERROR ServiceProvisioningServer::HandleUpdateService(UpdateServiceMessage& msg)
{
    WEAVE_ERROR err;
    uint64_t curServiceId;

    // Verify that the service id matches the existing service.  If not respond with "No Such Service".
    err = ConfigurationMgr.GetServiceId(curServiceId);
    if (err == WEAVE_PLATFORM_ERROR_CONFIG_NOT_FOUND || curServiceId != msg.ServiceId)
    {
        err = ServiceProvisioningSvr.SendStatusReport(kWeaveProfile_ServiceProvisioning, kStatusCode_NoSuchService);
        ExitNow();
    }
    SuccessOrExit(err);

    // Validate the service config. We don't want to get any further along before making sure the data is good.
    if (!ServiceProvisioningServer::IsValidServiceConfig(msg.ServiceConfig, msg.ServiceConfigLen))
    {
        err = ServiceProvisioningSvr.SendStatusReport(kWeaveProfile_ServiceProvisioning, kStatusCode_InvalidServiceConfig);
        ExitNow();
    }

    // Save the new service configuration in device persistent storage, replacing the existing value.
    err = ConfigurationMgr.StoreServiceConfig(msg.ServiceConfig, msg.ServiceConfigLen);
    SuccessOrExit(err);

    // Post an event alerting other subsystems that the service config has changed.
    {
        WeaveDeviceEvent event;
        event.Type = WeaveDeviceEvent::kEventType_ServiceProvisioningChange;
        event.ServiceProvisioningChange.IsServiceProvisioned = true;
        event.ServiceProvisioningChange.ServiceConfigUpdated = true;
        PlatformMgr.PostEvent(&event);
    }

    // Send "Success" back to the requestor.
    err = ServiceProvisioningSvr.SendSuccessResponse();
    SuccessOrExit(err);

exit:
    return err;
}

WEAVE_ERROR ServiceProvisioningServer::HandleUnregisterService(uint64_t serviceId)
{
    WEAVE_ERROR err;
    uint64_t curServiceId;

    // Verify that the service id matches the existing service.  If not respond with "No Such Service".
    err = ConfigurationMgr.GetServiceId(curServiceId);
    if (err == WEAVE_PLATFORM_ERROR_CONFIG_NOT_FOUND || curServiceId != serviceId)
    {
        err = ServiceProvisioningSvr.SendStatusReport(kWeaveProfile_ServiceProvisioning, kStatusCode_NoSuchService);
        ExitNow();
    }
    SuccessOrExit(err);

    // Clear the persisted service.
    err = ConfigurationMgr.ClearServiceProvisioningData();
    SuccessOrExit(err);

    // Post an event alerting other subsystems to the change in the account pairing state.
    {
        WeaveDeviceEvent event;
        event.Type = WeaveDeviceEvent::kEventType_AccountPairingChange;
        event.AccountPairingChange.IsPairedToAccount = false;
        PlatformMgr.PostEvent(&event);
    }

    // Post an event alerting other subsystems to the change in the service provisioning state.
    {
        WeaveDeviceEvent event;
        event.Type = WeaveDeviceEvent::kEventType_ServiceProvisioningChange;
        event.ServiceProvisioningChange.IsServiceProvisioned = false;
        event.ServiceProvisioningChange.ServiceConfigUpdated = false;
        PlatformMgr.PostEvent(&event);
    }

    // Send "Success" back to the requestor.
    err = ServiceProvisioningSvr.SendSuccessResponse();
    SuccessOrExit(err);

exit:
    return err;
}

bool ServiceProvisioningServer::IsPairedToAccount(void) const
{
    return ConfigurationMgr.IsServiceProvisioned() && ConfigurationMgr.IsPairedToAccount();
}

void ServiceProvisioningServer::OnPlatformEvent(const WeaveDeviceEvent * event)
{
#if !WEAVE_PLATFORM_CONFIG_DISABLE_ACCOUNT_PAIRING

    // If connectivity to the service has been established...
    if (event->Type == WeaveDeviceEvent::kEventType_ServiceConnectivityChange &&
        event->ServiceConnectivityChange.Result == kConnectivity_Established)
    {
        // If a RegisterServicePairAccount request is active and the system is waiting for
        // connectivity to the service, initiate the PairDeviceToAccount request now.
        if (mCurClientOp != NULL && mAwaitingServiceConnectivity)
        {
            StartPairDeviceToAccount();
        }
    }

#endif // !WEAVE_PLATFORM_CONFIG_DISABLE_ACCOUNT_PAIRING
}

#if !WEAVE_PLATFORM_CONFIG_DISABLE_ACCOUNT_PAIRING

void ServiceProvisioningServer::StartPairDeviceToAccount(void)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;

    // If the system does not currently have service connectivity, wait a period of time for it to be established.
    if (!ConnectivityMgr.HaveServiceConnectivity())
    {
        mAwaitingServiceConnectivity = true;

        err = SystemLayer.StartTimer(WEAVE_PLATFORM_CONFIG_SERVICE_PROVISIONING_CONNECTIVITY_TIMEOUT,
                HandleConnectivityTimeout,
                NULL);
        SuccessOrExit(err);
        ExitNow();

        ESP_LOGI(TAG, "Waiting for service connectivity to complete RegisterServicePairDevice action");
    }

    mAwaitingServiceConnectivity = false;

    ESP_LOGI(TAG, "Initiating communication with Service Provisioning service");

    // Create a binding and begin the process of preparing it for talking to the Service Provisioning
    // service. When this completes HandleProvServiceBindingEvent will be called with a BindingReady event.
    mProvServiceBinding = ExchangeMgr->NewBinding(HandleProvServiceBindingEvent, NULL);
    VerifyOrExit(mProvServiceBinding != NULL, err = WEAVE_ERROR_NO_MEMORY);
    err = mProvServiceBinding->BeginConfiguration()
            .Target_ServiceEndpoint(WEAVE_PLATFORM_CONFIG_SERVICE_PROVISIONING_ENDPOINT_ID)
            .Transport_UDP_WRM()
            .Exchange_ResponseTimeoutMsec(WEAVE_PLATFORM_CONFIG_SERVICE_PROVISIONING_REQUEST_TIMEOUT)
            .Security_SharedCASESession()
            .PrepareBinding();
    SuccessOrExit(err);

exit:
    if (err != WEAVE_NO_ERROR)
    {
        HandlePairDeviceToAccountResult(err, kWeaveProfile_Common, Profiles::Common::kStatus_InternalServerProblem);
    }
}

void ServiceProvisioningServer::SendPairDeviceToAccountRequest(void)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    const RegisterServicePairAccountMessage & regServiceMsg = mCurClientOpMsg.RegisterServicePairAccount;
    uint8_t devDesc[100]; // TODO: make configurable
    size_t devDescLen;

    // Generate a device descriptor for the local device in TLV.
    err = ConfigurationMgr.GetDeviceDescriptorTLV(devDesc, sizeof(devDesc), devDescLen);
    SuccessOrExit(err);

    // Call up to a helper function the server base class to encode and send a PairDeviceToAccount request to
    // the Service Provisioning service.  This will ultimately result in a call to HandlePairDeviceToAccountResult
    // with the result.
    //
    // Pass through the values for Service Id, Account Id, Pairing Token and Pairing Init Data that
    // were received in the Register Service message.  For Device Init Data, pass the encoded device
    // descriptor.  Finally, pass the id of the Weave fabric for which the device is a member.
    //
    ESP_LOGI(TAG, "Sending PairDeviceToAccount request to Service Provisioning service");
    err = ServerBaseClass::SendPairDeviceToAccountRequest(mProvServiceBinding,
            regServiceMsg.ServiceId, FabricState->FabricId,
            regServiceMsg.AccountId, regServiceMsg.AccountIdLen,
            regServiceMsg.PairingToken, regServiceMsg.PairingTokenLen,
            regServiceMsg.PairingInitData, regServiceMsg.PairingInitDataLen,
            devDesc, devDescLen);
    SuccessOrExit(err);

exit:
    if (err != WEAVE_NO_ERROR)
    {
        HandlePairDeviceToAccountResult(err, kWeaveProfile_Common, Profiles::Common::kStatus_InternalServerProblem);
    }
}

void ServiceProvisioningServer::HandlePairDeviceToAccountResult(WEAVE_ERROR err, uint32_t statusReportProfileId, uint16_t statusReportStatusCode)
{
    // Close the binding if necessary.
    if (mProvServiceBinding != NULL)
    {
        mProvServiceBinding->Close();
        mProvServiceBinding = NULL;
    }

    // Return immediately if for some reason the client's RegisterServicePairAccount request
    // is no longer pending.
    if (mCurClientOp == NULL)
    {
        ExitNow(err = WEAVE_NO_ERROR);
    }

    // If the PairDeviceToAccount request was successful, send a success StatusReport back
    // to the client.
    if (err == WEAVE_NO_ERROR)
    {
        const RegisterServicePairAccountMessage & regServiceMsg = mCurClientOpMsg.RegisterServicePairAccount;

        // Store the account id in persistent storage.
        err = ConfigurationMgr.StoreAccountId(regServiceMsg.AccountId, regServiceMsg.AccountIdLen);
        SuccessOrExit(err);

        // Post an event alerting other subsystems that the device is now paired to an account.
        {
            WeaveDeviceEvent event;
            event.Type = WeaveDeviceEvent::kEventType_AccountPairingChange;
            event.AccountPairingChange.IsPairedToAccount = true;
            PlatformMgr.PostEvent(&event);
        }

        ESP_LOGI(TAG, "PairDeviceToAccount request completed successfully");

        err = SendSuccessResponse();
        SuccessOrExit(err);
    }

exit:

    if (err != WEAVE_NO_ERROR)
    {
        ESP_LOGE(TAG, "PairDeviceToAccount request failed with %s: %s",
                 (err == WEAVE_ERROR_STATUS_REPORT_RECEIVED) ? "status report from service" : "local error",
                 (err == WEAVE_ERROR_STATUS_REPORT_RECEIVED)
                  ? ::nl::StatusReportStr(statusReportProfileId, statusReportStatusCode)
                  : ::nl::ErrorStr(err));

        // Since we're failing the RegisterServicePairDevice request, clear the persisted service configuration.
        ConfigurationMgr.ClearServiceProvisioningData();

        // Choose an appropriate StatusReport to return if not already given.
        if (statusReportProfileId == 0 && statusReportStatusCode == 0)
        {
            if (err == WEAVE_ERROR_TIMEOUT)
            {
                statusReportProfileId = kWeaveProfile_ServiceProvisioning;
                statusReportStatusCode = Profiles::ServiceProvisioning::kStatusCode_ServiceCommuncationError;
            }
            else
            {
                statusReportProfileId = kWeaveProfile_Common;
                statusReportStatusCode = Profiles::Common::kStatus_InternalServerProblem;
            }
        }

        // Send an error StatusReport back to the client.  Only include the local error code if it isn't
        // WEAVE_ERROR_STATUS_REPORT_RECEIVED.
        SendStatusReport(statusReportProfileId, statusReportStatusCode,
                (err != WEAVE_ERROR_STATUS_REPORT_RECEIVED) ? err : WEAVE_NO_ERROR);
    }
}

void ServiceProvisioningServer::AsyncStartPairDeviceToAccount(intptr_t arg)
{
    ServiceProvisioningSvr.StartPairDeviceToAccount();
}

void ServiceProvisioningServer::HandleConnectivityTimeout(System::Layer * /* unused */, void * /* unused */, System::Error /* unused */)
{
    ServiceProvisioningSvr.HandlePairDeviceToAccountResult(WEAVE_ERROR_TIMEOUT, 0, 0);
}

void ServiceProvisioningServer::HandleProvServiceBindingEvent(void * appState, Binding::EventType eventType,
            const Binding::InEventParam & inParam, Binding::OutEventParam & outParam)
{
    uint32_t statusReportProfileId;
    uint16_t statusReportStatusCode;

    switch (eventType)
    {
    case Binding::kEvent_BindingReady:
        ServiceProvisioningSvr.SendPairDeviceToAccountRequest();
        break;
    case Binding::kEvent_PrepareFailed:
        if (inParam.PrepareFailed.StatusReport != NULL)
        {
            statusReportProfileId = inParam.PrepareFailed.StatusReport->mProfileId;
            statusReportStatusCode = inParam.PrepareFailed.StatusReport->mStatusCode;
        }
        else
        {
            statusReportProfileId = kWeaveProfile_ServiceProvisioning;
            statusReportStatusCode = Profiles::ServiceProvisioning::kStatusCode_ServiceCommuncationError;
        }
        ServiceProvisioningSvr.HandlePairDeviceToAccountResult(inParam.PrepareFailed.Reason,
                statusReportProfileId, statusReportStatusCode);
        break;
    default:
        Binding::DefaultEventHandler(appState, eventType, inParam, outParam);
        break;
    }
}

#else // !WEAVE_PLATFORM_CONFIG_DISABLE_ACCOUNT_PAIRING

void ServiceProvisioningServer::HandlePairDeviceToAccountResult(WEAVE_ERROR err, uint32_t statusReportProfileId, uint16_t statusReportStatusCode)
{
}

#endif // !WEAVE_PLATFORM_CONFIG_DISABLE_ACCOUNT_PAIRING

} // namespace Internal
} // namespace Device
} // namespace Weave
} // namespace nl
