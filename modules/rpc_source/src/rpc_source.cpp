// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include <future>

#include <module.h>
#include <azure_c_shared_utility/lock.h>
#include <azure_c_shared_utility/xlogging.h>

#include <bond/core/bond.h>
#include <bond/comm/transport/epoxy.h>

#include "devices_comm.h"
#include "devices_reflection.h"

#include "rpc_source.h"

#include <iothubtransport.h>
#include <iothub_client.h>

using namespace microsoft::azure::devices;

Transport::Proxy::Using<std::promise> get_transport_proxy()
{
    bond::comm::SocketAddress loopback("127.0.0.1", 25188);
    bond::comm::epoxy::EpoxyTransport transport;
    return Transport::Proxy::Using<std::promise>(transport.Connect(loopback));
}

Client::Proxy::Using<std::promise> get_client_proxy()
{
    bond::comm::SocketAddress loopback("127.0.0.1", 25189);
    bond::comm::epoxy::EpoxyTransport transport;
    return Client::Proxy::Using<std::promise>(transport.Connect(loopback));
}

//
// iothubtransport.h
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
// iothub_client.h
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

typedef struct rpc_source_handle
{
    char dontcare;
} rpc_source_handle;

static MODULE_HANDLE rpc_source_create(BROKER_HANDLE broker, const void* configuration)
{
    return malloc(sizeof(rpc_source_handle));
}

static void* rpc_source_parse_config_from_json(const char* configuration)
{
	(void)configuration;
    return NULL;
}

static void rpc_source_free_config(void* configuration)
{
	(void)configuration;
}

static void rpc_source_start(MODULE_HANDLE module)
{
    (void)module;

    IOTHUB_CLIENT_CONFIG config;
    config.protocol = NULL;
    config.deviceId = "dlbtest01";
    config.deviceKey = "ZDRJDsfDbNHeUs832SzYCxi73WEkgHM+4dU+zViHXfI=";
    config.deviceSasToken = NULL;
    config.iotHubName = "iot-sdks-test";
    config.iotHubSuffix = "azure-devices.net";
    config.protocolGatewayHostName = NULL;

    TRANSPORT_HANDLE transport_h = IoTHubTransport_Create(
        NULL,
        "iot-sdks-test",
        "azure-devices.net"
    );

    IOTHUB_CLIENT_HANDLE client_h = IoTHubClient_CreateWithTransport(transport_h, &config);

    IoTHubClient_Destroy(client_h);
    IoTHubTransport_Destroy(transport_h);
}

static void rpc_source_destroy(MODULE_HANDLE module)
{
    free(module);
}

static void rpc_source_receive(MODULE_HANDLE moduleHandle, MESSAGE_HANDLE messageHandle)
{
    (void)moduleHandle;
    (void)messageHandle;
}

static const MODULE_API_1 rpc_source_api =
{
    {MODULE_API_VERSION_1},
    rpc_source_parse_config_from_json,
	rpc_source_free_config,
    rpc_source_create,
    rpc_source_destroy,
    rpc_source_receive,
    rpc_source_start
};

#ifdef BUILD_MODULE_TYPE_STATIC
MODULE_EXPORT const MODULE_API* MODULE_STATIC_GETAPI(RPCSOURCE_MODULE)(MODULE_API_VERSION version)
#else
MODULE_EXPORT const MODULE_API* Module_GetApi(MODULE_API_VERSION version)
#endif
{
    return (version >= rpc_source_api.base.version)
        ? (const MODULE_API*)&rpc_source_api
        : NULL;
}
