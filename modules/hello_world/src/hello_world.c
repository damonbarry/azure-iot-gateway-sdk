// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "module.h"
#include "azure_c_shared_utility/iot_logging.h"

#include "azure_c_shared_utility/threadapi.h"
#include "hello_world.h"
#include "azure_c_shared_utility/iot_logging.h"
#include "azure_c_shared_utility/lock.h"

#include "broker.h"

typedef struct HELLOWORLD_HANDLE_DATA_TAG
{
    THREAD_HANDLE threadHandle;
    LOCK_HANDLE lockHandle;
    int stopThread;
    BROKER_PUB_HANDLE pubHandle;
    STRING_HANDLE topic;
}HELLOWORLD_HANDLE_DATA;

#define HELLOWORLD_MESSAGE "hello world"

int helloWorldThread(void *param)
{
    HELLOWORLD_HANDLE_DATA* handleData = param;

    MESSAGE_CONFIG msgConfig;
    MAP_HANDLE propertiesMap = Map_Create(NULL);
    if (propertiesMap == NULL)
    {
        LogError("unable to create a Map");
    }
    else if (Map_AddOrUpdate(propertiesMap, "helloWorld", "from Azure IoT Gateway SDK simple sample!") != MAP_OK)
    {
        LogError("unable to Map_AddOrUpdate");
    }
    else
    {
        msgConfig.size = strlen(HELLOWORLD_MESSAGE);
        msgConfig.source = HELLOWORLD_MESSAGE;
    
        msgConfig.sourceProperties = propertiesMap;

        MESSAGE_HANDLE helloWorldMessage = Message_Create(&msgConfig);
        if (helloWorldMessage == NULL)
        {
            LogError("unable to create \"hello world\" message");
        }
        else
        {
            int32_t prefixSize = STRING_length(handleData->topic) + 1;
            int32_t size = Message_ToByteArray(helloWorldMessage, NULL, 0);
            if (size == -1)
            {
                LogError("Unable to determine message size");
            }
            else
            {
                while (1)
                {
                    if (Lock(handleData->lockHandle) == LOCK_OK)
                    {
                        if (handleData->stopThread)
                        {
                            (void)Unlock(handleData->lockHandle);
                            break; /*gets out of the thread*/
                        }
                        else if (Broker_Publish(handleData->pubHandle, STRING_c_str(handleData->topic), helloWorldMessage, size) != BROKER_RESULT_OK)
                        {
                            LogError("unable to send \"hello world\" message");
                        }

                        (void)Unlock(handleData->lockHandle);
                    }
                    else
                    {
                        /*shall retry*/
                    }

                    (void)ThreadAPI_Sleep(3000); /*every 3 seconds*/
                }
            }

            Message_Destroy(helloWorldMessage);
        }
    }

    return 0;
}

static MODULE_HANDLE HelloWorld_Create(const void* configuration)
{
    HELLOWORLD_HANDLE_DATA* result;

    if (configuration == NULL)
    {
        LogError("invalid arg configuration=%p", configuration);
        result = NULL;
    }
    else
    {
        const HELLO_WORLD_CONFIG* config = configuration;
        result = malloc(sizeof(HELLOWORLD_HANDLE_DATA));
        if (result == NULL)
        {
            LogError("unable to malloc");
        }
        else
        {
            result->lockHandle = Lock_Init();
            if (result->lockHandle == NULL)
            {
                LogError("unable to Lock_Init");
                free(result);
                result = NULL;
            }
            else
            {
                result->pubHandle = Broker_Create(config->brokerAddress);
                if (result->pubHandle == NULL)
                {
                    LogError("unable to create broker");
                    (void)Lock_Deinit(result->lockHandle);
                    free(result);
                    result = NULL;
                }
                else
                {
                    result->stopThread = 0;
                    result->topic = STRING_construct(config->brokerTopic);
                    if (ThreadAPI_Create(&result->threadHandle, helloWorldThread, result) != THREADAPI_OK)
                    {
                        LogError("failed to spawn a thread");
                        (void)Lock_Deinit(result->lockHandle);
                        Broker_Destroy(result->pubHandle);
                        free(result);
                        result = NULL;
                    }
                    else
                    {
                        /*all is fine*/
                    }
                }
            }
        }
    }
    return result;
}

static void HelloWorld_Destroy(MODULE_HANDLE module)
{
    /*first stop the thread*/
    HELLOWORLD_HANDLE_DATA* handleData = module;
    int notUsed;
    if (Lock(handleData->lockHandle) != LOCK_OK)
    {
        LogError("not able to Lock, still setting the thread to finish");
        handleData->stopThread = 1;
    }
    else
    {
        handleData->stopThread = 1;
        Unlock(handleData->lockHandle);
    }

    if(ThreadAPI_Join(handleData->threadHandle, &notUsed) != THREADAPI_OK)
    {
        LogError("unable to ThreadAPI_Join, still proceeding in _Destroy");
    }
    
    (void)Lock_Deinit(handleData->lockHandle);
    (void)Broker_Destroy(handleData->pubHandle);
    free(handleData);
}

static const MODULE_APIS HelloWorld_APIS_all =
{
	HelloWorld_Create,
	HelloWorld_Destroy
};

#ifdef BUILD_MODULE_TYPE_STATIC
MODULE_EXPORT const MODULE_APIS* MODULE_STATIC_GETAPIS(HELLOWORLD_MODULE)(void)
#else
MODULE_EXPORT const MODULE_APIS* Module_GetAPIS(void)
#endif
{
	return &HelloWorld_APIS_all;
}
