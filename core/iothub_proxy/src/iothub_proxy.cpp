// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <future>

#include <bond/core/bond.h>
#include <bond/comm/transport/epoxy.h>

#include "devices_comm.h"
#include "devices_reflection.h"

#include <iothubtransport.h>
#include <iothub_client.h>

using namespace microsoft::azure::devices;

static Transport::Proxy::Using<std::promise> get_transport_proxy()
{
    bond::comm::SocketAddress loopback("127.0.0.1", 25188);
    bond::comm::epoxy::EpoxyTransport transport;
    return Transport::Proxy::Using<std::promise>(transport.Connect(loopback));
}

static Client::Proxy::Using<std::promise> get_client_proxy()
{
    bond::comm::SocketAddress loopback("127.0.0.1", 25189);
    bond::comm::epoxy::EpoxyTransport transport;
    return Client::Proxy::Using<std::promise>(transport.Connect(loopback));
}

//
// IoTHubTransport
//

TRANSPORT_HANDLE IoTHubTransport_Create(IOTHUB_CLIENT_TRANSPORT_PROVIDER protocol, const char* iotHubName, const char* iotHubSuffix)
{
    CreateTransportArgs transportArgs;
    transportArgs.provider = Amqp; // don't hard-code
    transportArgs.iotHubName = iotHubName;
    transportArgs.iotHubSuffix = iotHubSuffix;

    Handle h = get_transport_proxy().Create(std::move(transportArgs)).get().value().Deserialize();

    printf("proxy>> transport handle = %#I64x\n", (uint64_t)h.value);
    return (TRANSPORT_HANDLE)h.value;
}

void IoTHubTransport_Destroy(TRANSPORT_HANDLE transportHandle)
{
    Handle h;
    h.value = (uint64_t)transportHandle;

    get_transport_proxy().Destroy(h);
}

//
// IoTHubClient
//

IOTHUB_CLIENT_HANDLE IoTHubClient_CreateWithTransport(TRANSPORT_HANDLE transportHandle, const IOTHUB_CLIENT_CONFIG* config)
{
    Handle transport_h;
    transport_h.value = (uint64_t)transportHandle;

    ClientConfig proxyConfig;
    proxyConfig.deviceId = config->deviceId;
    proxyConfig.deviceKey = config->deviceKey;
    if (config->deviceSasToken)
        proxyConfig.deviceSasToken = config->deviceSasToken;
    proxyConfig.iotHubName = config->iotHubName;
    proxyConfig.iotHubSuffix = config->iotHubSuffix;
    if (config->protocolGatewayHostName)
        proxyConfig.protocolGatewayHostName = config->protocolGatewayHostName;

    CreateWithTransportArgs clientArgs;
    clientArgs.transport = transport_h;
    clientArgs.config = proxyConfig;

    Handle h = get_client_proxy().CreateWithTransport(std::move(clientArgs)).get().value().Deserialize();

    printf("proxy>> client handle = %#I64x\n", (uint64_t)h.value);
    return (IOTHUB_CLIENT_HANDLE)h.value;
}

void IoTHubClient_Destroy(IOTHUB_CLIENT_HANDLE iotHubClientHandle)
{
    Handle h;
    h.value = (uint64_t)iotHubClientHandle;

    get_client_proxy().Destroy(h);
}
