// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#include <nn.h>
#include <pubsub.h>

#include "azure_c_shared_utility/iot_logging.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/lock.h"
#include "broker.h"

typedef struct
{
    int pubSocket;
} BROKER_PUB_HANDLE_DATA;

typedef struct
{
    int subSocket;
    STRING_HANDLE brokerAddress;
    STRING_HANDLE subscription;

    THREAD_HANDLE threadHandle;
    LOCK_HANDLE lockHandle;
    int stopThread;

    On_Message_Received recvHandler;
    void* context;
} BROKER_SUB_HANDLE_DATA;

BROKER_PUB_HANDLE Broker_Create(const char* address)
{
    BROKER_PUB_HANDLE_DATA* result;

    if (address == NULL)
    {
        LogError("Invalid arg");
        result = NULL;
    }
    else
    {
        result = malloc(sizeof(BROKER_PUB_HANDLE_DATA));
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
    BROKER_PUB_HANDLE_DATA* data = (BROKER_PUB_HANDLE_DATA*)publisher;

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
    BROKER_PUB_HANDLE_DATA* data = (BROKER_PUB_HANDLE_DATA*)publisher;

    if (data != NULL)
    {
        nn_close(data->pubSocket);
        free(data);
    }
}

BROKER_SUB_HANDLE Broker_Connect(const char* address)
{
    BROKER_SUB_HANDLE_DATA* result;

    if (address == NULL)
    {
        LogError("Invalid arg");
        result = NULL;
    }
    else
    {
        result = malloc(sizeof(BROKER_SUB_HANDLE_DATA));
        if (result == NULL)
        {
            LogError("malloc failed");
        }
        else
        {
            result->subSocket = nn_socket(AF_SP, NN_SUB);
            if (result->subSocket == -1)
            {
                LogError("unable to create NN_SUB socket");
                free(result);
                result = NULL;
            }
            else
            {
                int endpointId = nn_connect(result->subSocket, address);
                if (endpointId == -1) {
                    LogError("unable to connect NN_SUB socket to endpoint %s", address);
                    nn_close(result->subSocket);
                    free(result);
                    result = NULL;
                }
            }
        }
    }

    return result;
}

static int subscriberThread(void *param)
{
    BROKER_SUB_HANDLE_DATA* data = param;

    while (1)
    {
        if (Lock(data->lockHandle) == LOCK_OK)
        {
            if (data->stopThread)
            {
                (void)Unlock(data->lockHandle);
                nn_close(data->subSocket);
                break; /*gets out of the thread*/
            }
            else
            {
                (void)Unlock(data->lockHandle);

                uint8_t* buf = NULL;
                int nbytes = nn_recv(data->subSocket, &buf, NN_MSG, 0);
                if (nbytes == -1)
                {
                    LogError("error in nn_recv");
                }
                else
                {
                    LogInfo("RECV [%d bytes] : %.5s", nbytes, (char*)buf);

                    /*skip message prefix (topic), which is a null-terminated string*/
                    const int32_t prefixSize = strlen(buf) + 1;

                    MESSAGE_HANDLE messageHandle = Message_CreateFromByteArray(buf + prefixSize, nbytes - prefixSize);
                    if (messageHandle == NULL)
                    {
                        LogError("error in Message_CreateFromByteArray");
                    }
                    else
                    {
                        (void)data->recvHandler(messageHandle, data->context);
                    }

                    nn_freemsg(buf);
                }
            }
        }
        else
        {
            /*shall retry*/
        }
    }

    return 0;
}


// TODO: handle out-of-order calls & redundant calls (all functions)
BROKER_RESULT Broker_Subscribe(BROKER_SUB_HANDLE subscriber, const char* topic, On_Message_Received receiver, void* context)
{
    BROKER_RESULT result;
    BROKER_SUB_HANDLE_DATA* data = (BROKER_SUB_HANDLE_DATA*)subscriber;

    if (!subscriber || !topic || !receiver)
    {
        LogError("Invalid arg");
        result = BROKER_RESULT_INVALIDARG;
    }
    else
    {
        data->lockHandle = Lock_Init();
        if (data->lockHandle == NULL)
        {
            LogError("unable to Lock_Init");
            result = BROKER_RESULT_ERROR;
        }
        else
        {
            data->stopThread = 0;
            data->recvHandler = receiver;
            data->context = context;
            data->subscription = STRING_construct(topic);
            if (!data->subscription)
            {
                LogError("unable to copy topic name");
                result = BROKER_RESULT_ERROR;
            }
            else if (nn_setsockopt(data->subSocket, NN_SUB, NN_SUB_SUBSCRIBE, topic, 0) == -1)
            {
                LogError("unable to subscribe to topic");
                (void)Lock_Deinit(data->lockHandle);
                STRING_delete(data->subscription);
                result = BROKER_RESULT_ERROR;
            }
            else if (ThreadAPI_Create(&data->threadHandle, subscriberThread, data) != THREADAPI_OK)
            {
                LogError("failed to spawn a thread");
                (void)Lock_Deinit(data->lockHandle);
                STRING_delete(data->subscription);
                result = BROKER_RESULT_ERROR;
            }
            else
            {
                result = BROKER_RESULT_OK;
            }
        }
    }

    return result;
}

void Broker_Unsubscribe(BROKER_SUB_HANDLE subscriber)
{
    BROKER_SUB_HANDLE_DATA* data = (BROKER_SUB_HANDLE_DATA*)subscriber;

    if (subscriber != NULL)
    {
        int notUsed;
        if (Lock(data->lockHandle) != LOCK_OK)
        {
            LogError("not able to Lock, still setting the thread to finish");
            data->stopThread = 1;
        }
        else
        {
            data->stopThread = 1;
            Unlock(data->lockHandle);
        }

        if (ThreadAPI_Join(data->threadHandle, &notUsed) != THREADAPI_OK)
        {
            LogError("unable to ThreadAPI_Join, still proceeding in Unsubscribe");
        }

        (void)Lock_Deinit(data->lockHandle);
    }
}
