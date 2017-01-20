// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include <azure_c_shared_utility/threadapi.h>
#include <azure_c_shared_utility/xlogging.h>
#include <azure_c_shared_utility/lock.h>
#include <module.h>

#include "rpc_sink.h"

typedef struct rpc_sink_handle
{
    char dontcare;
} rpc_sink_handle;

static MODULE_HANDLE rpc_sink_create(BROKER_HANDLE broker, const void* configuration)
{
    return malloc(sizeof(rpc_sink_handle));
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
    free(module);
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
