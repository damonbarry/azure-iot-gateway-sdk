// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include "azure_c_shared_utility/gballoc.h"

#include "azure_c_shared_utility/gb_stdio.h"
#include "azure_c_shared_utility/gb_time.h"

#include "logger.h"

#include <errno.h>

#include "azure_c_shared_utility/strings.h"
#include "azure_c_shared_utility/iot_logging.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/base64.h"
#include "azure_c_shared_utility/map.h"
#include "azure_c_shared_utility/constmap.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/lock.h"

#include <nn.h>
#include <pubsub.h>

typedef struct LOGGER_HANDLE_DATA_TAG
{
    FILE* fout;
    THREAD_HANDLE threadHandle;
    LOCK_HANDLE lockHandle;
    int stopThread;
    STRING_HANDLE brokerAddress;
    STRING_HANDLE subscription;
}LOGGER_HANDLE_DATA;

/*this function adds a JSON object to the output*/
/*function assumes the file already has a json array in it. and that the last character is ] (when the first json is appended) or , when subsequent jsons are appended*/
static int addJSONString(FILE* fout, const char* jsonString)
{
    int result;
    if (fseek(fout, -1, SEEK_END) != 0) /*rewind 1 character*/ /*What is this in C standard... "A binary stream need not meaningfully support fseek calls with a whence value of SEEK_END."???*/
    {
        LogError("unable to fseek");
        result = __LINE__;
    }
    else
    {
        if (fprintf(fout, "%s", jsonString) < 0)
        {
            LogError("fprintf failed");
            result = __LINE__;
        }
        else
        {
            result = 0;
        }
    }
    return result;
}



static int LogStartStop_Print(char* destination, size_t destinationSize, bool appendStart, bool isAbsoluteStart)
{
    int result;
    time_t temp = time(NULL);
    if (temp == (time_t)-1)
    {
        LogError("unable to time(NULL)");
        result = __LINE__;
    }
    else
    {
        struct tm* t = localtime(&temp);
        if (t == NULL)
        {
            LogError("localtime failed");
            result = __LINE__;
        }
        else
        {
            const char* format = appendStart ?
                (isAbsoluteStart?"{\"time\":\"%c\",\"content\":\"Log started\"}]" : ",{\"time\":\"%c\",\"content\":\"Log started\"}]"):
                ",{\"time\":\"%c\",\"content\":\"Log stopped\"}]";
            if (strftime(destination, destinationSize, format, t) == 0) 
            {
                LogError("unable to strftime");
                result = __LINE__;
            }
            else
            {
                result = 0;
            }
        }
    }
    return result;
}

static int append_logStartStop(FILE* fout, bool appendStart, bool isAbsoluteStart)
{
    int result;
    /*Codes_SRS_LOGGER_02_017: [Logger_Create shall add the following JSON value to the existing array of JSON values in the file:]*/
    char temp[80] = { 0 };
    if (LogStartStop_Print(temp, sizeof(temp) / sizeof(temp[0]), appendStart, isAbsoluteStart) != 0 )
    {
        LogError("unable to create start/stop time json string");
        result = __LINE__;
    }
    else
    {
        if (addJSONString(fout, temp) != 0)
        {
            LogError("internal error in addJSONString");
            result = __LINE__;
        }
        else
        {
            result = 0; /*all is fine*/
        }
    }
    return result;
}

int loggerThread(void *param);

static MODULE_HANDLE Logger_Create(const void* configuration)
{
    LOGGER_HANDLE_DATA* result;
    /*Codes_SRS_LOGGER_02_002: [If configuration is NULL then Logger_Create shall fail and return NULL.]*/
    if (configuration == NULL)
    {
        LogError("invalid arg configuration=%p", configuration);
        result = NULL;
    }
    else
    {
        const LOGGER_CONFIG* config = configuration;
        /*Codes_SRS_LOGGER_02_003: [If configuration->selector has a value different than LOGGING_TO_FILE then Logger_Create shall fail and return NULL.]*/
        if (config->selector != LOGGING_TO_FILE)
        {
            LogError("invalid arg config->selector=%d", config->selector);
            result = NULL;
        }
        else
        {
            /*Codes_SRS_LOGGER_02_004: [If configuration->selectee.loggerConfigFile.name is NULL then Logger_Create shall fail and return NULL.]*/
            if (config->selectee.loggerConfigFile.name == NULL)
            {
                LogError("invalid arg config->selectee.loggerConfigFile.name=NULL");
                result = NULL;
            }
            else
            {
                /*Codes_SRS_LOGGER_02_005: [Logger_Create shall allocate memory for the below structure.]*/
                result = malloc(sizeof(LOGGER_HANDLE_DATA));
                /*Codes_SRS_LOGGER_02_007: [If Logger_Create encounters any errors while creating the LOGGER_HANDLE_DATA then it shall fail and return NULL.]*/
                if (result == NULL)
                {
                    LogError("malloc failed");
                    /*return as is*/
                }
                else
                {
                    /*Codes_SRS_LOGGER_02_006: [Logger_Create shall open the file configuration the filename selectee.loggerConfigFile.name in update (reading and writing) mode and assign the result of fopen to fout field. ]*/
                    result->fout = fopen(config->selectee.loggerConfigFile.name, "r+b"); /*open binary file for update (reading and writing)*/
                    if (result->fout == NULL) 
                    {
                        /*if the file does not exist [or other error, indistinguishable here] try to create it*/
                        /*Codes_SRS_LOGGER_02_020: [If the file selectee.loggerConfigFile.name does not exist, it shall be created.]*/
                        result->fout = fopen(config->selectee.loggerConfigFile.name, "w+b");/*truncate to zero length or create binary file for update*/
                    }

                    /*Codes_SRS_LOGGER_02_007: [If Logger_Create encounters any errors while creating the LOGGER_HANDLE_DATA then it shall fail and return NULL.]*/
                    /*Codes_SRS_LOGGER_02_021: [If creating selectee.loggerConfigFile.name fails then Logger_Create shall fail and return NULL.]*/
                    if (result->fout == NULL)
                    {
                        LogError("unable to open file %s", config->selectee.loggerConfigFile.name);
                        free(result);
                        result = NULL;
                    }
                    else
                    {
                        /*Codes_SRS_LOGGER_02_018: [If the file does not contain a JSON array, then it shall create it.]*/
                        if (fseek(result->fout, 0, SEEK_END) != 0)
                        {
                            /*Codes_SRS_LOGGER_02_007: [If Logger_Create encounters any errors while creating the LOGGER_HANDLE_DATA then it shall fail and return NULL.]*/
                            LogError("unable to fseek to end of file");
                            if (fclose(result->fout) != 0)
                            {
                                LogError("unable to close file %s", config->selectee.loggerConfigFile.name);
                            }
                            free(result);
                            result = NULL;
                        }
                        else
                        {
                            /*the verifications here are weak, content of file is not verified*/
                            errno = 0;
                            long int fileSize;
                            if ((fileSize = ftell(result->fout)) == -1L)
                            {
                                /*Codes_SRS_LOGGER_02_007: [If Logger_Create encounters any errors while creating the LOGGER_HANDLE_DATA then it shall fail and return NULL.]*/
                                LogError("unable to ftell (errno=%d)", errno);
                                if (fclose(result->fout) != 0)
                                {
                                    LogError("unable to close file %s", config->selectee.loggerConfigFile.name);
                                }
                                free(result);
                                result = NULL;
                            }
                            else
                            {
                                /*Codes_SRS_LOGGER_02_018: [If the file does not contain a JSON array, then it shall create it.]*/
                                if (fileSize == 0)
                                {
                                    /*Codes_SRS_LOGGER_02_018: [If the file does not contain a JSON array, then it shall create it.]*/
                                    if (fprintf(result->fout, "[]") < 0) /*add an empty array of JSONs to the output file*/
                                    {
                                        /*Codes_SRS_LOGGER_02_007: [If Logger_Create encounters any errors while creating the LOGGER_HANDLE_DATA then it shall fail and return NULL.]*/
                                        LogError("unable to write to output file");
                                        if (fclose(result->fout) != 0)
                                        {
                                            LogError("unable to close file %s", config->selectee.loggerConfigFile.name);
                                        }
                                        free(result);
                                        result = NULL;
                                    }
                                    else
                                    {
                                        /*Codes_SRS_LOGGER_02_017: [Logger_Create shall add the following JSON value to the existing array of JSON values in the file:]*/
                                        if (append_logStartStop(result->fout, true, true) != 0)
                                        {
                                            LogError("append_logStartStop failed");
                                            if (fclose(result->fout) != 0)
                                            {
                                                LogError("unable to close file %s", config->selectee.loggerConfigFile.name);
                                            }
                                            free(result);
                                            result = NULL;
                                        }
                                        /*Codes_SRS_LOGGER_02_008: [Otherwise Logger_Create shall return a non-NULL pointer.]*/
                                        /*that is, return as is*/
                                    }
                                }
                                else
                                {
                                    /*Codes_SRS_LOGGER_02_017: [Logger_Create shall add the following JSON value to the existing array of JSON values in the file:]*/
                                    if (append_logStartStop(result->fout, true, false) != 0)
                                    {
                                        LogError("append_logStartStop failed");
                                    }
                                    /*Codes_SRS_LOGGER_02_008: [Otherwise Logger_Create shall return a non-NULL pointer.]*/
                                    /*that is, return as is*/
                                }

                                result->lockHandle = Lock_Init();
                                if (result->lockHandle == NULL)
                                {
                                    LogError("unable to Lock_Init");
                                    if (fclose(result->fout) != 0)
                                    {
                                        LogError("unable to close file %s", config->selectee.loggerConfigFile.name);
                                    }
                                    free(result);
                                    result = NULL;
                                }
                                else
                                {
                                    result->stopThread = 0;
                                    result->brokerAddress = STRING_construct(config->brokerAddress);
                                    result->subscription = STRING_construct(config->brokerSubscription);
                                    if (ThreadAPI_Create(&result->threadHandle, loggerThread, result) != THREADAPI_OK)
                                    {
                                        LogError("failed to spawn a thread");
                                        (void)Lock_Deinit(result->lockHandle);
                                        if (fclose(result->fout) != 0)
                                        {
                                            LogError("unable to close file %s", config->selectee.loggerConfigFile.name);
                                        }
                                        free(result);
                                        result = NULL;
                                    }
                                    else
                                    {
                                        /*all is fine apparently*/
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return result;
}

static void Logger_Destroy(MODULE_HANDLE module)
{
    /*Codes_SRS_LOGGER_02_014: [If moduleHandle is NULL then Logger_Destroy shall return.]*/
    if (module != NULL)
    {
        /*Codes_SRS_LOGGER_02_019: [Logger_Destroy shall add to the log file the following end of log JSON object:]*/
        LOGGER_HANDLE_DATA* moduleHandleData = (LOGGER_HANDLE_DATA *)module;
        int notUsed;
        if (Lock(moduleHandleData->lockHandle) != LOCK_OK)
        {
            LogError("not able to Lock, still setting the thread to finish");
            moduleHandleData->stopThread = 1;
        }
        else
        {
            moduleHandleData->stopThread = 1;
            Unlock(moduleHandleData->lockHandle);
        }

        if (ThreadAPI_Join(moduleHandleData->threadHandle, &notUsed) != THREADAPI_OK)
        {
            LogError("unable to ThreadAPI_Join, still proceeding in _Destroy");
        }

        (void)Lock_Deinit(moduleHandleData->lockHandle);

        if (append_logStartStop(moduleHandleData->fout, false, false) != 0)
        {
            LogError("unable to append log ending time");
        }

        /*Codes_SRS_LOGGER_02_015: [Otherwise Logger_Destroy shall unuse all used resources.]*/
        if (fclose(moduleHandleData->fout) != 0)
        {
            LogError("unable to fclose");
        }

        free(moduleHandleData);

    }
}

int loggerThread(void *param)
{
    LOGGER_HANDLE_DATA* handleData = param;

    int subSocket = nn_socket(AF_SP, NN_SUB);
    if (subSocket == -1)
    {
        LogError("unable to create NN_SUB socket");
    }
    else
    {
        if (nn_setsockopt(subSocket, NN_SUB, NN_SUB_SUBSCRIBE, STRING_c_str(handleData->subscription), 0) == -1)
        {
            LogError("unable to subscribe to topic");
            nn_close(subSocket);
        }
        else
        {
            int endpointId = nn_connect(subSocket, STRING_c_str(handleData->brokerAddress));
            if (endpointId == -1) {
                LogError("unable to connect NN_SUB socket to endpoint %s", STRING_c_str(handleData->brokerAddress));
                nn_close(subSocket);
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
                            nn_close(subSocket);
                            break; /*gets out of the thread*/
                        }
                        else
                        {
                            (void)Unlock(handleData->lockHandle);

                            uint8_t* buf = NULL;
                            int nbytes = nn_recv(subSocket, &buf, NN_MSG, 0);
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
                                    /*the function will gather first all the values then will dump them into a STRING_HANDLE that is JSON*/

                                    /*getting the time*/
                                    time_t temp = time(NULL);
                                    if (temp == (time_t)-1)
                                    {
                                        LogError("time function failed");
                                    }
                                    else
                                    {
                                        struct tm* t = localtime(&temp);
                                        if (t == NULL)
                                        {
                                            LogError("localtime failed");
                                        }
                                        else
                                        {
                                            char timetemp[80] = { 0 };
                                            if (strftime(timetemp, sizeof(timetemp) / sizeof(timetemp[0]), "%c", t) == 0)
                                            {
                                                LogError("unable to strftime");
                                                /*Codes_SRS_LOGGER_02_012: [If producing the JSON format or writing it to the file fails, then Logger_Receive shall fail and return.]*/
                                                /*just return*/
                                            }
                                            else
                                            {
                                                /*getting the properties*/
                                                /*getting the constmap*/
                                                CONSTMAP_HANDLE originalProperties = Message_GetProperties(messageHandle); /*by contract this is never NULL*/
                                                MAP_HANDLE propertiesAsMap = ConstMap_CloneWriteable(originalProperties); /*sigh, if only there'd be a constmap_tojson*/
                                                if (propertiesAsMap == NULL)
                                                {
                                                    LogError("ConstMap_CloneWriteable failed");
                                                }
                                                else
                                                {
                                                    STRING_HANDLE jsonProperties = Map_ToJSON(propertiesAsMap);
                                                    if (jsonProperties == NULL)
                                                    {
                                                        LogError("unable to Map_ToJSON");
                                                    }
                                                    else
                                                    {
                                                        /*getting the base64 encode of the message*/
                                                        const CONSTBUFFER * content = Message_GetContent(messageHandle); /*by contract, this is never NULL*/
                                                        STRING_HANDLE contentAsJSON = Base64_Encode_Bytes(content->buffer, content->size);
                                                        if (contentAsJSON == NULL)
                                                        {
                                                            LogError("unable to Base64_Encode_Bytes");
                                                        }
                                                        else
                                                        {
                                                            STRING_HANDLE jsonToBeAppended = STRING_construct(",{\"time\":\"");
                                                            if (jsonToBeAppended == NULL)
                                                            {
                                                                LogError("unable to STRING_construct");
                                                            }
                                                            else
                                                            {

                                                                if (!(
                                                                    (STRING_concat(jsonToBeAppended, timetemp) == 0) &&
                                                                    (STRING_concat(jsonToBeAppended, "\",\"properties\":") == 0) &&
                                                                    (STRING_concat_with_STRING(jsonToBeAppended, jsonProperties) == 0) &&
                                                                    (STRING_concat(jsonToBeAppended, ",\"content\":\"") == 0) &&
                                                                    (STRING_concat_with_STRING(jsonToBeAppended, contentAsJSON) == 0) &&
                                                                    (STRING_concat(jsonToBeAppended, "\"}]") == 0)
                                                                    ))
                                                                {
                                                                    LogError("STRING concatenation error");
                                                                }
                                                                else
                                                                {
                                                                    if (addJSONString(handleData->fout, STRING_c_str(jsonToBeAppended)) != 0)
                                                                    {
                                                                        LogError("failed top add a json string to the output file");
                                                                    }
                                                                    else
                                                                    {
                                                                        /*all seems fine*/
                                                                    }
                                                                }
                                                                STRING_delete(jsonToBeAppended);
                                                            }
                                                            STRING_delete(contentAsJSON);
                                                        }
                                                        STRING_delete(jsonProperties);
                                                    }
                                                    Map_Destroy(propertiesAsMap);
                                                }
                                                ConstMap_Destroy(originalProperties);
                                            }
                                        }
                                    }
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
            }
        }
    }

    return 0;
}

/*
 *	Required for all modules:  the public API and the designated implementation functions.
 */
static const MODULE_APIS Logger_APIS_all =
{
	Logger_Create,
	Logger_Destroy
};

#ifdef BUILD_MODULE_TYPE_STATIC
MODULE_EXPORT const MODULE_APIS* MODULE_STATIC_GETAPIS(LOGGER_MODULE)(void)
#else
MODULE_EXPORT const MODULE_APIS* Module_GetAPIS(void)
#endif
{
    /*Codes_SRS_LOGGER_02_016: [Module_GetAPIS shall return a non-NULL pointer to a structure of type MODULE_APIS that has all fields non-NULL.]*/
	return &Logger_APIS_all;
}
