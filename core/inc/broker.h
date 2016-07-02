// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef BROKER_H
#define BROKER_H

#include "azure_c_shared_utility/macro_utils.h"
#include "message.h"

typedef void* BROKER_PUB_HANDLE;
typedef void* BROKER_SUB_HANDLE;

#ifdef __cplusplus
#include <cstddef>
extern "C"
{
#else
#include <stddef.h>
#endif

#define BROKER_RESULT_VALUES \
    BROKER_RESULT_OK, \
    BROKER_RESULT_ERROR, \
    BROKER_RESULT_INVALIDARG

/** @brief	Enumeration describing the result of ::Broker_Publish and
*			::Broker_Subscribe.
*/
DEFINE_ENUM(BROKER_RESULT, BROKER_RESULT_VALUES);

typedef void(*On_Message_Received)(MESSAGE_HANDLE message, void* context);

BROKER_PUB_HANDLE Broker_Create(const char* address);
BROKER_RESULT Broker_Publish(BROKER_PUB_HANDLE publisher, const char* topic, MESSAGE_HANDLE message, int32_t msgSizeHint);
void Broker_Destroy(BROKER_PUB_HANDLE publisher);

BROKER_SUB_HANDLE Broker_Connect(const char* address);
BROKER_RESULT Broker_Subscribe(BROKER_SUB_HANDLE subscriber, const char* topic, On_Message_Received receiver, void* context);
void Broker_Unsubscribe(BROKER_SUB_HANDLE subscriber);

#ifdef __cplusplus
}
#endif

#endif // BROKER_H
