// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "module.h"
#include "azure_c_shared_utility/iot_logging.h"
#include <stdio.h>
#include "hello_world.h"
#include "hello_world_hl.h"
#include "parson.h"

static MODULE_HANDLE HelloWorld_HL_Create(const void* configuration)
{
    MODULE_HANDLE result;
    if (configuration == NULL)
    {
        LogError("NULL parameter detected configuration=%p", configuration);
        result = NULL;
    }
    else
    {
        JSON_Value* json = json_parse_string((const char*)configuration);
        if (json == NULL)
        {
            LogError("unable to json_parse_string");
            result = NULL;
        }
        else
        {
            JSON_Object* obj = json_value_get_object(json);
            if (obj == NULL)
            {
                LogError("unable to json_value_get_object");
                result = NULL;
            }
            else
            {
                JSON_Object* broker = json_object_get_object(obj, "broker");
                if (broker == NULL)
                {
                    LogError("json_object_get_object failed");
                    result = NULL;
                }
                else
                {
                    const char* brokerAddress = json_object_get_string(broker, "address");
                    if (brokerAddress == NULL)
                    {
                        LogError("json_object_get_string failed");
                        result = NULL;
                    }
                    else
                    {
                        const char* brokerTopic = json_object_get_string(broker, "topic");
                        if (brokerTopic == NULL)
                        {
                            LogError("json_object_get_string failed");
                            result = NULL;
                        }
                        else
                        {
                            HELLO_WORLD_CONFIG config;
                            config.brokerAddress = brokerAddress;
                            config.brokerTopic = brokerTopic;

                            if ((result = MODULE_STATIC_GETAPIS(HELLOWORLD_MODULE)()->Module_Create(&config)) == NULL)
                            {
                                LogError("unable to Module_Create HELLOWORLD static");
                            }
                            else
                            {
                                /*all is fine, return as is*/
                            }
                        }
                    }
                }
            }
        }
    }
    return result;
}

static void HelloWorld_HL_Destroy(MODULE_HANDLE module)
{
    MODULE_STATIC_GETAPIS(HELLOWORLD_MODULE)()->Module_Destroy(module);
}

static void HelloWorld_HL_Receive(MODULE_HANDLE moduleHandle, MESSAGE_HANDLE messageHandle)
{
    //MODULE_STATIC_GETAPIS(HELLOWORLD_MODULE)()->Module_Receive(moduleHandle, messageHandle);
}

static const MODULE_APIS HelloWorld_HL_APIS_all =
{
	HelloWorld_HL_Create,
	HelloWorld_HL_Destroy
};

#ifdef BUILD_MODULE_TYPE_STATIC
MODULE_EXPORT const MODULE_APIS* MODULE_STATIC_GETAPIS(HELLOWORLD_HL_MODULE)(void)
#else
MODULE_EXPORT const MODULE_APIS* Module_GetAPIS(void)
#endif
{
	return &HelloWorld_HL_APIS_all;
}
