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

using namespace microsoft::azure::devices;

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

    bond::comm::SocketAddress transportLoopback("127.0.0.1", 25188);
    bond::comm::SocketAddress clientLoopback("127.0.0.1", 25189);
    bond::comm::epoxy::EpoxyTransport transport;

    CreateTransportArgs transportArgs;
    transportArgs.provider = Amqp;
    transportArgs.iotHubName = "iot-sdks-test";
    transportArgs.iotHubSuffix = "azure-devices.net";

    Transport::Proxy::Using<std::promise> transportProxy(transport.Connect(transportLoopback));

    Handle transport_h = transportProxy.Create(std::move(transportArgs)).get().value().Deserialize();

    printf("proxy>> transport handle = %#I64x\n", (uint64_t)transport_h.value);

    ClientConfig config;
    config.deviceId = "dlbtest01";
    config.deviceKey = "ZDRJDsfDbNHeUs832SzYCxi73WEkgHM+4dU+zViHXfI=";
    config.iotHubName = "iot-sdks-test";
    config.iotHubSuffix = "azure-devices.net";

    CreateWithTransportArgs clientArgs;
    clientArgs.transport = transport_h;
    clientArgs.config = config;

    Client::Proxy::Using<std::promise> clientProxy(transport.Connect(clientLoopback));

    Handle client_h = clientProxy.CreateWithTransport(std::move(clientArgs)).get().value().Deserialize();

    printf("proxy>> client handle = %#I64x\n", (uint64_t)client_h.value);

    ///*
    //struct SendEventArgs
    //{
    //    ::microsoft::azure::devices::Handle client;
    //    ::microsoft::azure::devices::Handle event;
    //    ::microsoft::azure::devices::Ptr callback;
    //    ::microsoft::azure::devices::Ptr context;
    //}
    //*/
    //// TODO: call SendEvent

    clientProxy.Destroy(client_h);
    transportProxy.Destroy(transport_h);
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
