// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include <module.h>
#include <azure_c_shared_utility/lock.h>
#include <azure_c_shared_utility/xlogging.h>
#include <iothubtransportamqp.h>
#include <iothub_client.h>

////////////////////////////////
// client code
//
#include <future>
//
////////////////////////////////

#include <bond/core/bond.h>
#include <bond/comm/transport/epoxy.h>

#include "devices_comm.h"
#include "devices_reflection.h"

#include "rpc_sink.h"

using namespace microsoft::azure::devices;

typedef struct rpc_sink_handle
{
    bond::comm::Server transport;
    bond::comm::Server client;
    boost::shared_ptr<bond::comm::epoxy::EpoxyTransport> epoxy;
} rpc_sink_handle;

// Implement service
struct TransportImpl : Transport
{
    void Create(const bond::comm::payload<CreateTransportArgs>& input,
        const std::function<void(const bond::comm::message<Handle>&)>& callback) override
    {
        CreateTransportArgs args = input.value().Deserialize();
        IOTHUB_CLIENT_TRANSPORT_PROVIDER provider = AMQP_Protocol; // ignore args.provider for now, just hard-code AMQP
        const char* iotHubName = args.iotHubName.c_str();
        const char* iotHubSuffix = args.iotHubSuffix.c_str();

        TRANSPORT_HANDLE h = IoTHubTransport_Create(provider, iotHubName, iotHubSuffix);

        Handle transport_h;
        transport_h.value = (uint64_t)h;
        printf("service>> transport handle = %#I64x\n", (uint64_t)h);
        callback(std::move(transport_h));
    }

    void Destroy(const bond::comm::payload<Handle>& input,
        const std::function<void(const bond::comm::message<void>&)>& callback) override
    {
        TRANSPORT_HANDLE h = (TRANSPORT_HANDLE)input.value().Deserialize().value;
        IoTHubTransport_Destroy(h);
        callback(bond::Void());
    }
};

const char* value_or_null(const char* value)
{
    return
        (!value) ? nullptr :
        (!value[0]) ? nullptr :
        value;
}

struct ClientImpl : Client
{
    // Client impl
    void CreateWithTransport(const bond::comm::payload<CreateWithTransportArgs>& input,
        const std::function<void(const bond::comm::message<Handle>&)>& callback) override
    {
        CreateWithTransportArgs args = input.value().Deserialize();

        TRANSPORT_HANDLE transport = (TRANSPORT_HANDLE)args.transport.value;

        IOTHUB_CLIENT_CONFIG cfg;
        cfg.protocol = AMQP_Protocol; // ignore args.config.transport for now, just hard-code AMQP
        cfg.deviceId = value_or_null(args.config.deviceId.c_str());
        cfg.deviceKey = value_or_null(args.config.deviceKey.c_str());
        cfg.deviceSasToken = value_or_null(args.config.deviceSasToken.c_str());
        cfg.iotHubName = value_or_null(args.config.iotHubName.c_str());
        cfg.iotHubSuffix = value_or_null(args.config.iotHubSuffix.c_str());
        cfg.protocolGatewayHostName = value_or_null(args.config.protocolGatewayHostName.c_str());

        IOTHUB_CLIENT_HANDLE h = IoTHubClient_CreateWithTransport(transport, &cfg);

        Handle client_h;
        client_h.value = (uint64_t)h;
        printf("service>> client handle = %#I64x\n", (uint64_t)h);
        callback(std::move(client_h));
    }

    void SendEventAsync(const bond::comm::payload<SendEventArgs>& input,
        const std::function<void(const bond::comm::message<ClientResult>&)>& callback) override
    {
        SendEventArgs args = input.value().Deserialize();

        IOTHUB_CLIENT_HANDLE client = (IOTHUB_CLIENT_HANDLE)args.client.value;
        IOTHUB_MESSAGE_HANDLE message = (IOTHUB_MESSAGE_HANDLE)args.event.value;
        IOTHUB_CLIENT_EVENT_CONFIRMATION_CALLBACK cb = (IOTHUB_CLIENT_EVENT_CONFIRMATION_CALLBACK)args.callback.value;
        void* context = (void*)args.context.value;

        IOTHUB_CLIENT_RESULT result = IoTHubClient_SendEventAsync(client, message, cb, context);

        ClientResult client_result;
        client_result.value = static_cast<ClientResultValue>(result);
        callback(std::move(client_result));
    }

    void SetMessageCallback(const bond::comm::payload<SetMessageCallbackArgs>& input,
        const std::function<void(const bond::comm::message<ClientResult>&)>& callback) override
    {
        SetMessageCallbackArgs args = input.value().Deserialize();

        IOTHUB_CLIENT_HANDLE client = (IOTHUB_CLIENT_HANDLE)args.client.value;
        IOTHUB_CLIENT_MESSAGE_CALLBACK_ASYNC cb = (IOTHUB_CLIENT_MESSAGE_CALLBACK_ASYNC)args.callback.value;
        void* context = (void*)args.context.value;

        IOTHUB_CLIENT_RESULT result = IoTHubClient_SetMessageCallback(client, cb, context);

        ClientResult client_result;
        client_result.value = static_cast<ClientResultValue>(result);
        callback(std::move(client_result));
    }

    void Destroy(const bond::comm::payload<Handle>& input,
        const std::function<void(const bond::comm::message<void>&)>& callback) override
    {
        IOTHUB_CLIENT_HANDLE client = (IOTHUB_CLIENT_HANDLE)input.value().Deserialize().value;
        IoTHubClient_Destroy(client);
        callback(bond::Void());
    }
};

static MODULE_HANDLE rpc_sink_create(BROKER_HANDLE broker, const void* configuration)
{
    auto transportLoopback = boost::make_shared<bond::comm::SocketAddress>("127.0.0.1", 25188);
    auto clientLoopback = boost::make_shared<bond::comm::SocketAddress>("127.0.0.1", 25189);
    auto transport = boost::make_shared<bond::comm::epoxy::EpoxyTransport>();

    // do we try...catch for errors? check for nullptr? something else?
    auto transportService = transport->Bind(*transportLoopback, boost::make_shared<TransportImpl>());
    auto clientService = transport->Bind(*clientLoopback, boost::make_shared<ClientImpl>());

    try
    {
        rpc_sink_handle* module_h = new rpc_sink_handle;
        // we want the services to listen for the lifetime of the module
        module_h->transport = transportService;
        module_h->client = clientService;
        module_h->epoxy = transport;
        return module_h;
    }
    catch (std::bad_alloc)
    {
        return nullptr;
    }
}

static void* rpc_sink_parse_config_from_json(const char* configuration)
{
	(void)configuration;
    return NULL;
}

static void rpc_sink_free_config(void* configuration)
{
	(void)configuration;
}

static void rpc_sink_start(MODULE_HANDLE module)
{
    (void)module;
}

static void rpc_sink_destroy(MODULE_HANDLE module)
{
    rpc_sink_handle* module_h = (rpc_sink_handle*)module;
    delete module_h;
}

static void rpc_sink_receive(MODULE_HANDLE moduleHandle, MESSAGE_HANDLE messageHandle)
{
    (void)moduleHandle;
    (void)messageHandle;
}

static const MODULE_API_1 rpc_sink_api =
{
    {MODULE_API_VERSION_1},
    rpc_sink_parse_config_from_json,
	rpc_sink_free_config,
    rpc_sink_create,
    rpc_sink_destroy,
    rpc_sink_receive,
    rpc_sink_start
};

#ifdef BUILD_MODULE_TYPE_STATIC
MODULE_EXPORT const MODULE_API* MODULE_STATIC_GETAPI(RPCSINK_MODULE)(MODULE_API_VERSION version)
#else
MODULE_EXPORT const MODULE_API* Module_GetApi(MODULE_API_VERSION version)
#endif
{
    return (version >= rpc_sink_api.base.version)
        ? (const MODULE_API*)&rpc_sink_api
        : NULL;
}
