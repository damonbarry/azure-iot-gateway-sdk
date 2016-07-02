// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#include <nn.h>
#include <pubsub.h>

#include "azure_c_shared_utility/iot_logging.h"
#include "broker.h"

typedef struct
{
    int pubSocket;
} BROKER_HANDLE_DATA;

BROKER_PUB_HANDLE Broker_Create(const char* address)
{
    BROKER_HANDLE_DATA* result;

    if (address == NULL)
    {
        LogError("Invalid arg");
        result = NULL;
    }
    else
    {
        result = malloc(sizeof(BROKER_HANDLE_DATA));
        if (result == NULL)
        {
            LogError("malloc failed");
        }
        else
        {
            result->pubSocket = nn_socket(AF_SP, NN_PUB);
            if (result->pubSocket == -1)
            {
                LogError("unable to create NN_PUB socket");
                free(result);
                result = NULL;
            }
            else
            {
                int endpointId = nn_bind(result->pubSocket, address);
                if (endpointId == -1) {
                    LogError("unable to bind NN_PUB socket to endpoint %s", address);
                    nn_close(result->pubSocket);
                    free(result);
                    result = NULL;
                }
            }
        }
    }

    return result;
}

BROKER_RESULT Broker_Publish(BROKER_PUB_HANDLE publisher, const char* topic, MESSAGE_HANDLE message, int32_t msgSizeHint)
{
    BROKER_RESULT result;
    BROKER_HANDLE_DATA* data = (BROKER_HANDLE_DATA*)publisher;

    if (!publisher || !topic || !message)
    {
        LogError("Invalid arg");
        result = BROKER_RESULT_INVALIDARG;
    }
    else
    {
        int32_t prefixSize = strlen(topic) + 1;
        int32_t size = (msgSizeHint == 0) ? Message_ToByteArray(message, NULL, 0) : msgSizeHint;
        if (size == -1)
        {
            LogError("Unable to determine message size");
            result = BROKER_RESULT_ERROR;
        }
        else
        {
            uint8_t* buf = nn_allocmsg(size + prefixSize, 0);
            /*
            Note: We'll use the zero-copy form of nn_send below,
            so nanomsg will take ownership of buf. We aren't
            responsible for freeing it.
            */
            if (buf == NULL)
            {
                LogError("Unable to allocate buffer");
                result = BROKER_RESULT_ERROR;
            }
            else
            {
                strcpy(buf, topic);
                if (Message_ToByteArray(message, buf + prefixSize, size) != size)
                {
                    LogError("unable to serialize \"hello world\" message");
                    result = BROKER_RESULT_ERROR;
                }
                else
                {
                    int nbytes = nn_send(data->pubSocket, &buf, NN_MSG, 0);
                    if (nbytes == -1)
                    {
                        LogError("unable to send \"hello world\" message");
                        result = BROKER_RESULT_ERROR;
                    }
                    else
                    {
                        LogInfo("NN_SEND sent %d bytes", nbytes);
                        result = BROKER_RESULT_OK;
                    }
                }
            }
        }
    }

    return result;
}

void Broker_Destroy(BROKER_PUB_HANDLE publisher)
{
    BROKER_HANDLE_DATA* data = (BROKER_HANDLE_DATA*)publisher;

    if (data != NULL)
    {
        nn_close(data->pubSocket);
        free(data);
    }
}

BROKER_SUB_HANDLE Broker_Connect(const char* address)
{
    return NULL;
}

BROKER_RESULT Broker_Subscribe(BROKER_SUB_HANDLE subscriber, const char* topic, On_Message_Received receiver, void* context)
{
    return BROKER_RESULT_ERROR;
}

void Broker_Unsubscribe(BROKER_SUB_HANDLE subscriber)
{
}
