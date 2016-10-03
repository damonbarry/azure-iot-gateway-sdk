// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include "azure_c_shared_utility/gballoc.h"

#include <stddef.h>
#include <azure_c_shared_utility/strings.h>
#include <ctype.h>

#include "azure_c_shared_utility/crt_abstractions.h"
#include "messageproperties.h"
#include "message.h"
#include "broker.h"
#include "identitymap.h"
#include "azure_c_shared_utility/constmap.h"
#include "azure_c_shared_utility/constbuffer.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/vector.h"

#include "schemalib.h"
#include "serializer.h"
#include "schemaserializer.h"

// Define the Model
BEGIN_NAMESPACE(Contoso);

DECLARE_STRUCT(SystemProperties,
    ascii_char_ptr, DeviceID,
    _Bool, Enabled
);

DECLARE_STRUCT(DeviceProperties,
    ascii_char_ptr, DeviceID,
    _Bool, HubEnabledState,
    ascii_char_ptr, Manufacturer,
    ascii_char_ptr, ModelNumber,
    ascii_char_ptr, SerialNumber,
    ascii_char_ptr, FirmwareVersion
);

DECLARE_MODEL(Thermostat,

    /* Event data (temperature, external temperature and humidity) */
    WITH_DATA(int, Temperature),
    WITH_DATA(int, ExternalTemperature),
    WITH_DATA(int, Humidity),
    WITH_DATA(ascii_char_ptr, DeviceId),

    /* Device Info - This is command metadata + some extra fields */
    WITH_DATA(ascii_char_ptr, ObjectType),
    WITH_DATA(_Bool, IsSimulatedDevice),
    // WITH_DATA(ascii_char_ptr, Version),
    WITH_DATA(DeviceProperties, DeviceProperties)//,
    // WITH_DATA(ascii_char_ptr_no_quotes, Commands),

    /* Commands implemented by the device */
    // WITH_ACTION(SetTemperature, int, temperature),
    // WITH_ACTION(SetHumidity, int, humidity)
);

END_NAMESPACE(Contoso);

// EXECUTE_COMMAND_RESULT SetTemperature(Thermostat* thermostat, int temperature)
// {
//     (void)printf("Received temperature %d\r\n", temperature);
//     thermostat->Temperature = temperature;
//     return EXECUTE_COMMAND_SUCCESS;
// }

// EXECUTE_COMMAND_RESULT SetHumidity(Thermostat* thermostat, int humidity)
// {
//     (void)printf("Received humidity %d\r\n", humidity);
//     thermostat->Humidity = humidity;
//     return EXECUTE_COMMAND_SUCCESS;
// }

Thermostat* thermostat = NULL;

typedef struct IDENTITY_MAP_DATA_TAG
{
    BROKER_HANDLE broker;
    size_t mappingSize;
    IDENTITY_MAP_CONFIG * macToDevIdArray;
    IDENTITY_MAP_CONFIG * devIdToMacArray;
    // keep track of the devices we've seen so we can send a special info message
    // for each new device
    VECTOR_HANDLE active_devices;
} IDENTITY_MAP_DATA;

#define IDENTITYMAP_RESULT_VALUES \
    IDENTITYMAP_OK, \
    IDENTITYMAP_ERROR, \
    IDENTITYMAP_MEMORY

DEFINE_ENUM(IDENTITYMAP_RESULT, IDENTITYMAP_RESULT_VALUES)

DEFINE_ENUM_STRINGS(BROKER_RESULT, BROKER_RESULT_VALUES);

/*
 * @brief    duplicate a string and convert to upper case. New string must be released
 *            no longer needed.
 */
static const char * IdentityMapConfig_ToUpperCase(const char * string)
{
    char * result;
    int status;
    status = mallocAndStrcpy_s(&result, string);
    if (status != 0) // failure
    {
        result = NULL;
    }
    else
    {
        char * temp = result;
        while (*temp)
        {
            *temp = toupper(*temp);
            temp++;
        }
    }

    return result;
}

/*
 * @brief    Duplicate and copy all fields in the IDENTITY_MAP_CONFIG struct.
 *            Forces MAC address to be all upper case (to guarantee comparisons are consistent).
 */
static IDENTITYMAP_RESULT IdentityMapConfig_CopyDeep(IDENTITY_MAP_CONFIG * dest, IDENTITY_MAP_CONFIG * source)
{
    IDENTITYMAP_RESULT result;
    int status;
    dest->macAddress = IdentityMapConfig_ToUpperCase(source->macAddress);
    if (dest->macAddress == NULL)
    {
        LogError("Unable to allocate macAddress");
        result = IDENTITYMAP_MEMORY;
    }
    else
    {
        char * temp;
        status = mallocAndStrcpy_s(&temp, source->deviceId);
        if (status != 0)
        {
            LogError("Unable to allocate deviceId");
            free((void*)dest->macAddress);
            dest->macAddress = NULL;
            result = IDENTITYMAP_MEMORY;
        }
        else
        {
            dest->deviceId = temp;
            status = mallocAndStrcpy_s(&temp, source->deviceKey);
            if (status != 0)
            {
                LogError("Unable to allocate deviceKey");
                free((void*)dest->deviceId);
                dest->deviceId = NULL;
                free((void*)dest->macAddress);
                dest->macAddress = NULL;
                result = IDENTITYMAP_MEMORY;
            }
            else
            {
                dest->deviceKey = temp;
                result = IDENTITYMAP_OK;
            }
        }
    }
    return result;
}

/*
 * @brief    Free strings associated with IDENTITY_MAP_CONFIG structure
 */
static void IdentityMapConfig_Free(IDENTITY_MAP_CONFIG * element)
{
    /*Codes_SRS_IDMAP_17_015: [IdentityMap_Destroy shall release all resources allocated for the module.]*/
    free((void*)element->macAddress);
    free((void*)element->deviceId);
    free((void*)element->deviceKey);
}

/*Codes_SRS_IDMAP_17_006: [If any macAddress string in configuration is not a MAC address in canonical form, this function shall fail and return NULL.]*/
static bool IdentityMapConfig_IsCanonicalMAC(const char * macAddress)
{
    /* Every MAC address must be in the form "XX:XX:XX:XX:XX:XX" X=[0-9,a-f,A-F] */
    bool recognized;
    const size_t fixedSize = 17;
    const size_t numDigits = 12;
    const size_t numColons = 5;
    const size_t digits[] = { 0, 1, 3, 4, 6, 7, 9, 10, 12, 13, 15, 16 };
    const size_t colons[] = { 2, 5, 8, 11, 14 };
    size_t macSize = strlen(macAddress);
    if (macSize != fixedSize)
    {
        recognized = false;
    }
    else
    {
        recognized = true;
        size_t i;
        for (i = 0; i < numDigits; i++)
        {
            if (!isxdigit(macAddress[digits[i]]))
            {
                recognized = false;
                break;
            }
        }
        if (recognized == true)
        {
            for (i = 0; i < numColons; i++)
            {
                if (macAddress[colons[i]] != ':')
                {
                    recognized = false;
                    break;
                }
            }
        }
    }
    return recognized;
}

/*
 * @brief    Comparison function by MAC address for two IDENTITY_MAP_CONFIG structures
 */
static int IdentityMapConfig_MacCompare(const void * a, const void * b)
{
    const IDENTITY_MAP_CONFIG * idA = a;
    const IDENTITY_MAP_CONFIG * idB = b;
    return strcmp(idA->macAddress, idB->macAddress);
}
/*
* @brief    Comparison function by deviceId for two IDENTITY_MAP_CONFIG structures
*/
static int IdentityMapConfig_IdCompare(const void * a, const void * b)
{
    const IDENTITY_MAP_CONFIG * idA = a;
    const IDENTITY_MAP_CONFIG * idB = b;
    return strcmp(idA->deviceId, idB->deviceId);
}

/*
 * @brief    Walks through our mappingVector to ensure it is correct for our identity map module.
 */
static bool IdentityMap_ValidateConfig(const VECTOR_HANDLE mappingVector)
{
    size_t mappingSize = VECTOR_size(mappingVector);
    bool mappingOk;
    if (mappingSize == 0)
    {
        /*Codes_SRS_IDMAP_17_041: [If the configuration has no vector elements, this function shall fail and return NULL.*/
        LogError("nothing for this module to do, no mapping data");
        mappingOk = false;
    }
    else
    {
        mappingOk = true;
        size_t index;
        for (index = 0; (index < mappingSize) && (mappingOk != false); index++)
        {
            IDENTITY_MAP_CONFIG * element = (IDENTITY_MAP_CONFIG *)VECTOR_element(mappingVector, index);
            if ((element->deviceId == NULL) ||
                (element->deviceKey == NULL) ||
                (element->macAddress == NULL))
            {
                /*Codes_SRS_IDMAP_17_019: [If any macAddress, deviceId or deviceKey are NULL, this function shall fail and return NULL.]*/
                LogError("Empty mapping data values, mac=%p, ID=%p, key=%p",
                    element->macAddress, element->deviceId, element->deviceKey);
                mappingOk = false;
                break;
            }
            else
            {
                if (IdentityMapConfig_IsCanonicalMAC(element->macAddress) == false)
                {
                    /*Codes_SRS_IDMAP_17_006: [If any macAddress string in configuration is not a MAC address in canonical form, this function shall fail and return NULL.]*/
                    LogError("Non-canonical MAC Address: %s", element->macAddress);
                    mappingOk = false;
                    break;
                }
            }
        }
    }
    return mappingOk;
}

static bool init_device_property(char** property)
{
    *property = NULL;
    char* newStr = realloc(*property, 1);
    if (!newStr) return false;

    *newStr = 0;
    *property = newStr;
    return true;
}

/*
 * @brief    Create an identity map module.
 */
static MODULE_HANDLE IdentityMap_Create(BROKER_HANDLE broker, const void* configuration)
{
    IDENTITY_MAP_DATA* result;
    if (broker == NULL || configuration == NULL)
    {
        /*Codes_SRS_IDMAP_17_004: [If the broker is NULL, this function shall fail and return NULL.]*/
        /*Codes_SRS_IDMAP_17_005: [If the configuration is NULL, this function shall fail and return NULL.]*/
        LogError("invalid parameter (NULL).");
        result = NULL;
    }
    else
    {
        if (serializer_init(NULL) != SERIALIZER_OK)
        {
            LogError("Failed on serializer_init");
            result = NULL;
        }
        else
        {
            thermostat = CREATE_MODEL_INSTANCE(Contoso, Thermostat);
            if (thermostat == NULL)
            {
                LogError("Failed on CREATE_MODEL_INSTANCE");
                result = NULL;
            }
            else
            {
                if (!init_device_property(&(thermostat->DeviceProperties.Manufacturer)) ||
                    !init_device_property(&(thermostat->DeviceProperties.ModelNumber)) ||
                    !init_device_property(&(thermostat->DeviceProperties.SerialNumber)) ||
                    !init_device_property(&(thermostat->DeviceProperties.FirmwareVersion)))
                {
                    LogError("failed to allocate memory for device properties");
                    free(thermostat->DeviceProperties.Manufacturer);
                    free(thermostat->DeviceProperties.ModelNumber);
                    free(thermostat->DeviceProperties.SerialNumber);
                    free(thermostat->DeviceProperties.FirmwareVersion);
                    DESTROY_MODEL_INSTANCE(thermostat);
                    serializer_deinit();
                    result = NULL;
                }
                else
                {
                    VECTOR_HANDLE mappingVector = (VECTOR_HANDLE)configuration;
                    if (IdentityMap_ValidateConfig(mappingVector) == false)
                    {
                        LogError("unable to validate mapping table");
                        free(thermostat->DeviceProperties.Manufacturer);
                        free(thermostat->DeviceProperties.ModelNumber);
                        free(thermostat->DeviceProperties.SerialNumber);
                        free(thermostat->DeviceProperties.FirmwareVersion);
                        DESTROY_MODEL_INSTANCE(thermostat);
                        serializer_deinit();
                        result = NULL;
                    }
                    else
                    {
                        result = (IDENTITY_MAP_DATA*)malloc(sizeof(IDENTITY_MAP_DATA));
                        if (result == NULL)
                        {
                            /*Codes_SRS_IDMAP_17_010: [If IdentityMap_Create fails to allocate a new IDENTITY_MAP_DATA structure, then this function shall fail, and return NULL.]*/
                            LogError("Could not Allocate Module");
                            free(thermostat->DeviceProperties.Manufacturer);
                            free(thermostat->DeviceProperties.ModelNumber);
                            free(thermostat->DeviceProperties.SerialNumber);
                            free(thermostat->DeviceProperties.FirmwareVersion);
                            DESTROY_MODEL_INSTANCE(thermostat);
                            serializer_deinit();
                        }
                        else
                        {
                            result->active_devices = VECTOR_create(sizeof(STRING_HANDLE));
                            if (result->active_devices == NULL)
                            {
                                LogError("Could not allcate the active_devices vector");
                                free(thermostat->DeviceProperties.Manufacturer);
                                free(thermostat->DeviceProperties.ModelNumber);
                                free(thermostat->DeviceProperties.SerialNumber);
                                free(thermostat->DeviceProperties.FirmwareVersion);
                                DESTROY_MODEL_INSTANCE(thermostat);
                                serializer_deinit();
                                free(result);
                                result = NULL;
                            }
                            else
                            {
                                size_t mappingSize = VECTOR_size(mappingVector);
                                /* validation ensures the vector is greater than zero */
                                result->macToDevIdArray = (IDENTITY_MAP_CONFIG*)malloc(mappingSize*sizeof(IDENTITY_MAP_CONFIG));
                                if (result->macToDevIdArray == NULL)
                                {
                                    /*Codes_SRS_IDMAP_17_011: [If IdentityMap_Create fails to create memory for the macToDeviceArray, then this function shall fail and return NULL.*/
                                    LogError("Could not allocate mac to device mapping table");
                                    VECTOR_destroy(result->active_devices);
                                    free(thermostat->DeviceProperties.Manufacturer);
                                    free(thermostat->DeviceProperties.ModelNumber);
                                    free(thermostat->DeviceProperties.SerialNumber);
                                    free(thermostat->DeviceProperties.FirmwareVersion);
                                    DESTROY_MODEL_INSTANCE(thermostat);
                                    serializer_deinit();
                                    free(result);
                                    result = NULL;
                                }
                                else
                                {
                                    result->devIdToMacArray = (IDENTITY_MAP_CONFIG*)malloc(mappingSize*sizeof(IDENTITY_MAP_CONFIG));
                                    if (result->devIdToMacArray == NULL)
                                    {
                                        /*Codes_SRS_IDMAP_17_042: [ If IdentityMap_Create fails to create memory for the deviceToMacArray, then this function shall fail and return NULL. */
                                        LogError("Could not allocate devicee to mac mapping table");
                                        free(result->macToDevIdArray);
                                        VECTOR_destroy(result->active_devices);
                                        free(thermostat->DeviceProperties.Manufacturer);
                                        free(thermostat->DeviceProperties.ModelNumber);
                                        free(thermostat->DeviceProperties.SerialNumber);
                                        free(thermostat->DeviceProperties.FirmwareVersion);
                                        DESTROY_MODEL_INSTANCE(thermostat);
                                        serializer_deinit();
                                        free(result);
                                        result = NULL;
                                    }
                                    else
                                    {

                                        size_t index;
                                        size_t failureIndex = mappingSize;
                                        for (index = 0; index < mappingSize; index++)
                                        {
                                            IDENTITY_MAP_CONFIG * element = (IDENTITY_MAP_CONFIG *)VECTOR_element(mappingVector, index);
                                            IDENTITY_MAP_CONFIG * dest = &(result->macToDevIdArray[index]);
                                            IDENTITYMAP_RESULT copyResult;
                                            copyResult = IdentityMapConfig_CopyDeep(dest, element);
                                            if (copyResult != IDENTITYMAP_OK)
                                            {
                                                failureIndex = index;
                                                break;
                                            }
                                            dest = &(result->devIdToMacArray[index]);
                                            copyResult = IdentityMapConfig_CopyDeep(dest, element);
                                            if (copyResult != IDENTITYMAP_OK)
                                            {
                                                IdentityMapConfig_Free(&(result->macToDevIdArray[index]));
                                                failureIndex = index;
                                                break;
                                            }
                                        }
                                        if (failureIndex < mappingSize)
                                        {
                                            /*Codes_SRS_IDMAP_17_012: [If IdentityMap_Create fails to add a MAC address triplet to the macToDeviceArray, then this function shall fail, release all resources, and return NULL.]*/
                                            /*Codes_SRS_IDMAP_17_043: [ If IdentityMap_Create fails to add a MAC address triplet to the deviceToMacArray, then this function shall fail, release all resources, and return NULL. */
                                            for (index = 0; index < failureIndex; index++)
                                            {
                                                IdentityMapConfig_Free(&(result->macToDevIdArray[index]));
                                                IdentityMapConfig_Free(&(result->devIdToMacArray[index]));
                                            }
                                            free(result->macToDevIdArray);
                                            free(result->devIdToMacArray);
                                            VECTOR_destroy(result->active_devices);
                                            free(thermostat->DeviceProperties.Manufacturer);
                                            free(thermostat->DeviceProperties.ModelNumber);
                                            free(thermostat->DeviceProperties.SerialNumber);
                                            free(thermostat->DeviceProperties.FirmwareVersion);
                                            DESTROY_MODEL_INSTANCE(thermostat);
                                            serializer_deinit();
                                            free(result);
                                            result = NULL;
                                        }
                                        else
                                        {
                                            /*Codes_SRS_IDMAP_17_003: [Upon success, this function shall return a valid pointer to a MODULE_HANDLE.]*/
                                            qsort(result->macToDevIdArray, mappingSize, sizeof(IDENTITY_MAP_CONFIG),
                                                IdentityMapConfig_MacCompare);
                                            qsort(result->devIdToMacArray, mappingSize, sizeof(IDENTITY_MAP_CONFIG),
                                                IdentityMapConfig_IdCompare);
                                            result->mappingSize = mappingSize;
                                            result->broker = broker;
                                        }
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

/*
* @brief    Destroy an identity map module.
*/
static void IdentityMap_Destroy(MODULE_HANDLE moduleHandle)
{
    /*Codes_SRS_IDMAP_17_018: [If moduleHandle is NULL, IdentityMap_Destroy shall return.]*/
    if (moduleHandle != NULL)
    {
        /*Codes_SRS_IDMAP_17_015: [IdentityMap_Destroy shall release all resources allocated for the module.]*/
        IDENTITY_MAP_DATA * idModule = (IDENTITY_MAP_DATA*)moduleHandle;
        for (size_t index = 0; index < idModule->mappingSize; index++)
        {
            IdentityMapConfig_Free(&(idModule->macToDevIdArray[index]));
            IdentityMapConfig_Free(&(idModule->devIdToMacArray[index]));
        }
        free(idModule->macToDevIdArray);
        free(idModule->devIdToMacArray);

        for (size_t i = 0; i < VECTOR_size(idModule->active_devices); ++i)
        {
            STRING_delete(VECTOR_element(idModule->active_devices, i));
        }
        // VECTOR_destroy(idModule->active_devices);

        free(idModule);
    }

    if (thermostat != NULL)
    {
        free(thermostat->DeviceProperties.Manufacturer);
        free(thermostat->DeviceProperties.ModelNumber);
        free(thermostat->DeviceProperties.SerialNumber);
        free(thermostat->DeviceProperties.FirmwareVersion);
        DESTROY_MODEL_INSTANCE(thermostat);
        serializer_deinit();
    }
}

static void publish_with_new_properties(MAP_HANDLE newProperties, MESSAGE_HANDLE messageHandle, IDENTITY_MAP_DATA * idModule)
{
    /*Codes_SRS_IDMAP_17_034: [IdentityMap_Receive shall clone message content.] */ 
    CONSTBUFFER_HANDLE content = Message_GetContentHandle(messageHandle);
    if (content == NULL)
    {
        /*Codes_SRS_IDMAP_17_035: [If cloning message content fails, IdentityMap_Receive shall deallocate all resources and return.]*/
        LogError("Could not extract message content");
    }
    else
    {
        MESSAGE_BUFFER_CONFIG newMessageConfig =
        {
            content,
            newProperties
        };
        /*Codes_SRS_IDMAP_17_036: [IdentityMap_Receive shall create a new message by calling Message_CreateFromBuffer with new map and cloned content.]*/
        MESSAGE_HANDLE newMessage = Message_CreateFromBuffer(&newMessageConfig);
        if (newMessage == NULL)
        {
            /*Codes_SRS_IDMAP_17_037: [If creating new message fails, IdentityMap_Receive shall deallocate all resources and return.]*/
            LogError("Could not create new message to publish");
        }
        else
        {
            BROKER_RESULT brokerStatus;
            /*Codes_SRS_IDMAP_17_038: [IdentityMap_Receive shall call Broker_Publish with broker and new message.]*/
            brokerStatus = Broker_Publish(idModule->broker, (MODULE_HANDLE)idModule, newMessage);
            if (brokerStatus != BROKER_OK)
            {
                LogError("Message broker publish failure: %s", ENUM_TO_STRING(BROKER_RESULT, brokerStatus));
            }
            Message_Destroy(newMessage);
        }
        /*Codes_SRS_IDMAP_17_039: [IdentityMap_Receive will destroy all resources it created.]*/
        CONSTBUFFER_Destroy(content);
    }
}

static void sensortag_temp_convert(
    uint16_t rawAmbTemp,
    uint16_t rawObjTemp,
    float *tAmb,
    float *tObj
)
{
    const float SCALE_LSB = 0.03125;
    float t;
    int it;

    it = (int)((rawObjTemp) >> 2);
    t = ((float)(it)) * SCALE_LSB;
    *tObj = t;

    it = (int)((rawAmbTemp) >> 2);
    t = (float)it;
    *tAmb = t * SCALE_LSB;
}

static void get_temperature(const CONSTBUFFER* buffer, float* ambient, float* object)
{
    if (buffer->size == 4)
    {
        uint16_t* temps = (uint16_t *)buffer->buffer;
        sensortag_temp_convert(temps[0], temps[1], ambient, object);
    }
}

static void get_humidity(const CONSTBUFFER* buffer, float* temperature, float* humidity)
{
    if (buffer->size == 4)
    {
        uint16_t* vals = (uint16_t *)buffer->buffer;
        sensortag_temp_convert(vals[0], vals[1], temperature, humidity);
    }
}

/*
 * @brief    Republish message with new data from our matching identities.
 */
static void IdentityMap_RepublishD2C(
    IDENTITY_MAP_DATA * idModule,
    MESSAGE_HANDLE messageHandle,
    IDENTITY_MAP_CONFIG * match)
{
    CONSTMAP_HANDLE properties = Message_GetProperties(messageHandle);
    if (properties == NULL)
    {

        LogError("Could not extract message properties");
    }
    else
    {
        /*Codes_SRS_IDMAP_17_026: [On a message which passes all checks, IdentityMap_Receive shall call ConstMap_CloneWriteable on the message properties.]*/
        MAP_HANDLE newProperties = ConstMap_CloneWriteable(properties);
        if (newProperties == NULL)
        {
            /*Codes_SRS_IDMAP_17_027: [If ConstMap_CloneWriteable fails, IdentityMap_Receive shall deallocate any resources and return.] */
            LogError("Could not make writeable new properties map");
        }
        else
        {
            /*Codes_SRS_IDMAP_17_028: [IdentityMap_Receive shall call Map_AddOrUpdate with key of "deviceName" and value of found deviceId.]*/
            if (Map_AddOrUpdate(newProperties, GW_DEVICENAME_PROPERTY, match->deviceId) != MAP_OK)
            {
                /*Codes_SRS_IDMAP_17_029: [If adding deviceName fails,IdentityMap_Receive shall deallocate all resources and return.]*/
                LogError("Could not attach %s property to message", GW_DEVICENAME_PROPERTY);
            }
            /*Codes_SRS_IDMAP_17_030: [IdentityMap_Receive shall call Map_AddOrUpdate with key of "deviceKey" and value of found deviceKey.]*/
            else if (Map_AddOrUpdate(newProperties, GW_DEVICEKEY_PROPERTY, match->deviceKey) != MAP_OK)
            {
                /*Codes_SRS_IDMAP_17_031: [If adding deviceKey fails, IdentityMap_Receive shall deallocate all resources and return.]*/
                LogError("Could not attach %s property to message", GW_DEVICEKEY_PROPERTY);
            }
            /*Codes_SRS_IDMAP_17_032: [IdentityMap_Receive shall call Map_AddOrUpdate with key of "source" and value of "mapping".]*/
            else if (Map_AddOrUpdate(newProperties, GW_SOURCE_PROPERTY, GW_IDMAP_MODULE) != MAP_OK)
            {
                /*Codes_SRS_IDMAP_17_033: [If adding source fails, IdentityMap_Receive shall deallocate all resources and return.]*/
                LogError("Could not attach %s property to message", GW_SOURCE_PROPERTY);
            }
            /*Codes_SRS_IDMAP_17_053: [ IdentityMap_Receive shall call Map_Delete to remove the "macAddress" property. ]*/
            else if (Map_Delete(newProperties, GW_MAC_ADDRESS_PROPERTY) != MAP_OK)
            {
                /*Codes_SRS_IDMAP_17_054: [ If deleting the MAC Address fails, IdentityMap_Receive shall deallocate all resources and return. ]*/
                LogError("Could not remove %s property from message", GW_MAC_ADDRESS_PROPERTY);
            }
            else
            {
                unsigned char* buffer = NULL;
                size_t bufferSize = 0;

                const char* characteristic_uuid = ConstMap_GetValue(properties, GW_CHARACTERISTIC_UUID_PROPERTY);
                if (characteristic_uuid != NULL)
                {
                    if (strcmp(characteristic_uuid, "F000AA01-0451-4000-B000-000000000000") == 0) // temperature
                    {
                        float ambient, object;

                        CONSTBUFFER_HANDLE content = Message_GetContentHandle(messageHandle);
                        get_temperature(CONSTBUFFER_GetContent(content), &ambient, &object);

                        thermostat->DeviceId = (char*)match->deviceId;
                        thermostat->Temperature = (int)ambient;
                        thermostat->ExternalTemperature = (int)object; // we'll pretend object temp is external temp

                        if (SERIALIZE(&buffer, &bufferSize, thermostat->DeviceId,
                            thermostat->Temperature, thermostat->Humidity, thermostat->ExternalTemperature) != IOT_AGENT_OK)
                        {
                            LogError("Failed to serialize temperature");
                        }
                    }
                    else if (strcmp(characteristic_uuid, "F000AA21-0451-4000-B000-000000000000") == 0) // humidity
                    {
                        float temp, humidity;

                        CONSTBUFFER_HANDLE content = Message_GetContentHandle(messageHandle);
                        get_humidity(CONSTBUFFER_GetContent(content), &temp, &humidity);

                        thermostat->DeviceId = (char*)match->deviceId;
                        thermostat->Humidity = (int)humidity;

                        // if (SERIALIZE(&buffer, &bufferSize, thermostat->DeviceId,
                        //     thermostat->Humidity) != IOT_AGENT_OK)
                        // {
                        //     LogError("Failed to serialize temperature");
                        // }
                    }
                    else if (strcmp(characteristic_uuid, "00002A29-0000-1000-8000-00805F9B34FB") == 0) // manufacturer
                    {
                        CONSTBUFFER_HANDLE content = Message_GetContentHandle(messageHandle);
                        const CONSTBUFFER* buf = CONSTBUFFER_GetContent(content);
                        char* newStr = realloc(thermostat->DeviceProperties.Manufacturer, buf->size + 1);
                        if (newStr != NULL)
                        {
                            memcpy(newStr, buf->buffer, buf->size);
                            newStr[buf->size] = 0;
                            thermostat->DeviceProperties.Manufacturer = newStr;
                        }
                    }
                    else if (strcmp(characteristic_uuid, "00002A24-0000-1000-8000-00805F9B34FB") == 0) // model number
                    {
                        CONSTBUFFER_HANDLE content = Message_GetContentHandle(messageHandle);
                        const CONSTBUFFER* buf = CONSTBUFFER_GetContent(content);
                        char* newStr = realloc(thermostat->DeviceProperties.ModelNumber, buf->size);
                        if (newStr != NULL)
                        {
                            memcpy(newStr, buf->buffer, buf->size);
                            newStr[buf->size] = 0;
                            thermostat->DeviceProperties.ModelNumber = newStr;
                        }
                    }
                    else if (strcmp(characteristic_uuid, "00002A25-0000-1000-8000-00805F9B34FB") == 0) // serial number
                    {
                        CONSTBUFFER_HANDLE content = Message_GetContentHandle(messageHandle);
                        const CONSTBUFFER* buf = CONSTBUFFER_GetContent(content);
                        char* newStr = realloc(thermostat->DeviceProperties.SerialNumber, buf->size);
                        if (newStr != NULL)
                        {
                            memcpy(newStr, buf->buffer, buf->size);
                            newStr[buf->size] = 0;
                            thermostat->DeviceProperties.SerialNumber = newStr;
                        }
                    }
                    else if (strcmp(characteristic_uuid, "00002A26-0000-1000-8000-00805F9B34FB") == 0) // firmware version
                    {
                        CONSTBUFFER_HANDLE content = Message_GetContentHandle(messageHandle);
                        const CONSTBUFFER* buf = CONSTBUFFER_GetContent(content);
                        char* newStr = realloc(thermostat->DeviceProperties.FirmwareVersion, buf->size);
                        if (newStr != NULL)
                        {
                            memcpy(newStr, buf->buffer, buf->size);
                            newStr[buf->size] = 0;
                            thermostat->DeviceProperties.FirmwareVersion = newStr;
                        }
                    }

                    if (buffer != NULL && bufferSize != 0)
                    {
                        CONSTBUFFER_HANDLE newContent = CONSTBUFFER_Create(buffer, bufferSize);
                        free(buffer);
                        if (!newContent)
                        {
                            LogError("Could not create new message content");
                        }
                        else
                        {
                            MESSAGE_BUFFER_CONFIG newMessageConfig =
                            {
                                newContent,
                                newProperties
                            };

                            MESSAGE_HANDLE newMessage = Message_CreateFromBuffer(&newMessageConfig);
                            if (newMessage == NULL)
                            {
                                LogError("Could not create new message to publish");
                            }
                            else
                            {
                                messageHandle = newMessage;
                                publish_with_new_properties(newProperties, messageHandle, idModule);
                            }
                        }
                    }
                }
            }
            Map_Destroy(newProperties);
        }
        ConstMap_Destroy(properties);
    }
}

/*
* @brief    Republish message with new data from our matching identities.
*/
static void IdentityMap_RepublishC2D(
    IDENTITY_MAP_DATA * idModule,
    MESSAGE_HANDLE messageHandle,
    IDENTITY_MAP_CONFIG * match)
{
    CONSTMAP_HANDLE properties = Message_GetProperties(messageHandle);
    if (properties == NULL)
    {

        LogError("Could not extract message properties");
    }
    else
    {
        /*Codes_SRS_IDMAP_17_049: [ On a C2D message received, IdentityMap_Receive shall call ConstMap_CloneWriteable on the message properties. ]*/
        MAP_HANDLE newProperties = ConstMap_CloneWriteable(properties);
        if (newProperties == NULL)
        {
            /*Codes_SRS_IDMAP_17_050: [ If ConstMap_CloneWriteable fails, IdentityMap_Receive shall deallocate any resources and return. ]*/
            LogError("Could not make writeable new properties map");
        }
        else
        {
            /*Codes_SRS_IDMAP_17_051: [ IdentityMap_Receive shall call Map_AddOrUpdate with key of "macAddress" and value of found macAddress. ]*/
            if (Map_AddOrUpdate(newProperties, GW_MAC_ADDRESS_PROPERTY, match->macAddress) != MAP_OK)
            {
                /*Codes_SRS_IDMAP_17_052: [ If adding macAddress fails, IdentityMap_Receive shall deallocate all resources and return. ]*/
                LogError("Could not attach %s property to message", GW_MAC_ADDRESS_PROPERTY);
            }
            /*Codes_SRS_IDMAP_17_032: [IdentityMap_Receive shall call Map_AddOrUpdate with key of "source" and value of "mapping".]*/
            else if (Map_AddOrUpdate(newProperties, GW_SOURCE_PROPERTY, GW_IDMAP_MODULE) != MAP_OK)
            {
                /*Codes_SRS_IDMAP_17_033: [If adding source fails, IdentityMap_Receive shall deallocate all resources and return.]*/
                LogError("Could not attach %s property to message", GW_SOURCE_PROPERTY);
            }
            /*Codes_SRS_IDMAP_17_055: [ IdentityMap_Receive shall call Map_Delete to remove the "deviceName" property. ]*/
            else if (Map_Delete(newProperties, GW_DEVICENAME_PROPERTY) != MAP_OK)
            {
                /*Codes_SRS_IDMAP_17_056: [ If deleting the device name fails, IdentityMap_Receive shall deallocate all resources and return. ]*/
                LogError("Could not remove %s property from message", GW_DEVICENAME_PROPERTY);
            }
            /*Codes_SRS_IDMAP_17_057: [ IdentityMap_Receive shall call Map_Delete to remove the "deviceKey" property. ]*/
            else if (Map_Delete(newProperties, GW_DEVICEKEY_PROPERTY) == MAP_INVALIDARG)
            {
                /*Codes_SRS_IDMAP_17_058: [ If deleting the device key does not return MAP_OK or MAP_KEYNOTFOUND, IdentityMap_Receive shall deallocate all resources and return. ]*/
                LogError("Could not remove %s property from message", GW_DEVICEKEY_PROPERTY);
            }
            else
            {
                publish_with_new_properties(newProperties, messageHandle, idModule);
            }
            Map_Destroy(newProperties);
        }
        ConstMap_Destroy(properties);
    }
}

/* returns true if the message should continue to be processed, sets direction */
static bool determine_message_direction(const char * source, bool * isC2DMessage)
{
    bool result; 
    if (source != NULL)
    {
        if (strcmp(source, GW_IOTHUB_MODULE) == 0)
        {
            result = true;
            *isC2DMessage = true;
        }
        else if (strcmp(source, GW_IDMAP_MODULE) != 0)
        {
            result = true;
            *isC2DMessage = false;
        }
        else
        {
            /*Codes_SRS_IDMAP_17_047: [ If messageHandle property "source" is not equal to "iothub", then the message shall not be marked as a C2D message. ]*/
            /*Codes_SRS_IDMAP_17_044: [ If messageHandle properties contains a "source" property that is set to "mapping", the message shall not be marked as a D2C message. ]*/
            result = false;
            *isC2DMessage = false;
        }
    }
    else
    {
        /*Codes_SRS_IDMAP_17_046: [ If messageHandle properties does not contain a "source" property, then the message shall not be marked as a C2D message. ]*/
        result = false;
        *isC2DMessage = false;
    }
    return result;
}

static bool matches_mac(const void* element, const void* value)
{
    return (strcmp(STRING_c_str((STRING_HANDLE)element), value) == 0);
}

static bool send_new_device_message(IDENTITY_MAP_DATA* data, const IDENTITY_MAP_CONFIG* identity, const unsigned char* buffer, size_t size)
{
    bool result;

    MAP_HANDLE properties = Map_Create(NULL);
    if (properties == NULL)
    {
        LogError("Could not create properties map");
        result = false;
    }
    else
    {
        if (Map_Add(properties, GW_DEVICENAME_PROPERTY, identity->deviceId) != MAP_OK)
        {
            LogError("Could not attach %s property to message", GW_DEVICENAME_PROPERTY);
            result = false;
        }
        else if (Map_AddOrUpdate(properties, GW_DEVICEKEY_PROPERTY, identity->deviceKey) != MAP_OK)
        {
            LogError("Could not attach %s property to message", GW_DEVICEKEY_PROPERTY);
            result = false;
        }
        else if (Map_AddOrUpdate(properties, GW_SOURCE_PROPERTY, GW_IDMAP_MODULE) != MAP_OK)
        {
            LogError("Could not attach %s property to message", GW_SOURCE_PROPERTY);
            result = false;
        }
        else
        {
            MESSAGE_CONFIG config = { size, buffer, properties };
            MESSAGE_HANDLE message = Message_Create(&config);
            if (message == NULL)
            {
                LogError("Failed to create new device message");
                result = false;
            }
            else
            {
                if (Broker_Publish(data->broker, (MODULE_HANDLE)data, message) != BROKER_OK)
                {
                    LogError("Message broker publish failure");
                    result = false;
                }
                else
                {
                    result = true;
                }

                Message_Destroy(message);
            }
        }

        Map_Destroy(properties);
    }

    return result;
}


/*
 * @brief    Receive a message from the message broker.
 */
static void IdentityMap_Receive(MODULE_HANDLE moduleHandle, MESSAGE_HANDLE messageHandle)
{
    if (moduleHandle == NULL || messageHandle == NULL)
    {
        /*Codes_SRS_IDMAP_17_020: [If moduleHandle or messageHandle is NULL, then the function shall return.]*/
        LogError("Received NULL arguments: module = %p, massage = %p", moduleHandle, messageHandle);
    }
    else
    {
        IDENTITY_MAP_DATA * idModule = (IDENTITY_MAP_DATA*)moduleHandle;

        CONSTMAP_HANDLE properties = Message_GetProperties(messageHandle);

        const char * source = ConstMap_GetValue(properties, GW_SOURCE_PROPERTY);
        bool isC2DMessage;
        if (determine_message_direction(source, &isC2DMessage))
        {
            if (isC2DMessage == true)
            {
                const char * deviceName = ConstMap_GetValue(properties, GW_DEVICENAME_PROPERTY);
                /*Codes_SRS_IDMAP_17_045: [ If messageHandle properties does not contain "deviceName" property, then the message shall not be marked as a C2D message. */
                if (deviceName != NULL)
                {
                    IDENTITY_MAP_CONFIG key = { NULL,deviceName,NULL };

                    IDENTITY_MAP_CONFIG * match = bsearch(&key,
                        idModule->devIdToMacArray, idModule->mappingSize,
                        sizeof(IDENTITY_MAP_CONFIG),
                        IdentityMapConfig_IdCompare);
                    if (match == NULL)
                    {
                        /*Codes_SRS_IDMAP_17_048: [ If the deviceName of the message is not found in deviceToMacArray, then the message shall not be marked as a C2D message. ]*/
                        LogInfo("Did not find device Id [%s] of current message", deviceName);
                    }
                    else
                    {
                        IdentityMap_RepublishC2D(idModule, messageHandle, match);
                    }
                }
            }
            else
            {
                const char * messageMac = IdentityMapConfig_ToUpperCase(
                    ConstMap_GetValue(properties, GW_MAC_ADDRESS_PROPERTY));

                /*Codes_SRS_IDMAP_17_021: [If messageHandle properties does not contain "macAddress" property, then the function shall return.]*/
                if (messageMac != NULL)
                {
                    /*Codes_SRS_IDMAP_17_024: [If messageHandle properties contains properties "deviceName" and "deviceKey", then this function shall return.] */
                    if ((ConstMap_GetValue(properties, GW_DEVICENAME_PROPERTY) == NULL ||
                        ConstMap_GetValue(properties, GW_DEVICEKEY_PROPERTY) == NULL))
                    {
                        if (IdentityMapConfig_IsCanonicalMAC(messageMac) == false)
                        {
                            /*Codes_SRS_IDMAP_17_040: [If the macAddress of the message is not in canonical form, then this function shall return.]*/
                            LogInfo("MAC address not valid: %s", messageMac);
                        }
                        else
                        {
                            IDENTITY_MAP_CONFIG key = { messageMac,NULL,NULL };

                            IDENTITY_MAP_CONFIG * match = bsearch(&key,
                                idModule->macToDevIdArray, idModule->mappingSize,
                                sizeof(IDENTITY_MAP_CONFIG),
                                IdentityMapConfig_MacCompare);
                            if (match == NULL)
                            {
                                /*Codes_SRS_IDMAP_17_025: [If the macAddress of the message is not found in the macToDeviceArray list, then this function shall return.]*/
                                LogInfo("Did not find message MAC Address: %s", messageMac);
                            }
                            else
                            {
                                STRING_HANDLE found = VECTOR_find_if(idModule->active_devices, matches_mac, messageMac);
                                if (found == NULL)
                                {
                                    STRING_HANDLE clone = STRING_construct(messageMac);
                                    if (clone == NULL)
                                    {
                                        LogError("Couldn't clone MAC string");
                                    }
                                    else
                                    {
                                        // STRING_HANDLE commandsMetadata;

                                        thermostat->ObjectType = "DeviceInfo";
                                        thermostat->IsSimulatedDevice = false;
                                        // thermostat->Version = "1.0";
                                        thermostat->DeviceProperties.HubEnabledState = true;
                                        thermostat->DeviceProperties.DeviceID = (char*)match->deviceId;
                                        thermostat->Temperature = 0;
                                        thermostat->ExternalTemperature = 0;
                                        thermostat->Humidity = 0;
                                        thermostat->DeviceId = (char*)match->deviceId;

                                        // commandsMetadata = STRING_new();
                                        // if (commandsMetadata == NULL)
                                        // {
                                        //     LogError("Failed create a string for commands metadata");
                                        // }
                                        // else
                                        // {
                                        //     /* Serialize the commands metadata as a JSON string before sending */
                                        //     if (SchemaSerializer_SerializeCommandMetadata(GET_MODEL_HANDLE(Contoso, Thermostat), commandsMetadata) != SCHEMA_SERIALIZER_OK)
                                        //     {
                                        //         LogError("Failed to serialize commands metadata");
                                        //     }
                                        //     else
                                        //     {
                                                if (*(thermostat->DeviceProperties.Manufacturer) != 0 &&
                                                    *(thermostat->DeviceProperties.ModelNumber) != 0 &&
                                                    *(thermostat->DeviceProperties.SerialNumber) != 0 &&
                                                    *(thermostat->DeviceProperties.FirmwareVersion) != 0)
                                                {
                                                    unsigned char* buffer;
                                                    size_t bufferSize;

                                                    // thermostat->Commands = (char*)STRING_c_str(commandsMetadata);

                                                    if (SERIALIZE(&buffer, &bufferSize,
                                                        thermostat->ObjectType,
                                                        // thermostat->Version,
                                                        thermostat->IsSimulatedDevice,
                                                        thermostat->DeviceProperties,
                                                        thermostat->DeviceId/*, thermostat->Commands*/) != CODEFIRST_OK)
                                                    {
                                                        LogError("Failed to serialize DeviceInfo message");
                                                    }
                                                    else
                                                    {
                                                        if (!send_new_device_message(idModule, match, buffer, bufferSize))
                                                        {
                                                            LogError("Couldn't send DeviceInfo message");
                                                        }
                                                        else
                                                        {
                                                            if (VECTOR_push_back(idModule->active_devices, clone, 1) != 0)
                                                            {
                                                                LogError("Couldn't push MAC to active_devices");
                                                            }
                                                        }

                                                        free(buffer);
                                                    }
                                                }
                                        //     }

                                        //     STRING_delete(commandsMetadata);
                                        // }
                                    }
                                }

                                IdentityMap_RepublishD2C(idModule, messageHandle, match);
                            }
                        }
                    }
                    /*Codes_SRS_IDMAP_17_039: [IdentityMap_Receive will destroy all resources it created.]*/
                    free((void*)messageMac);
                }
            }
        }

        ConstMap_Destroy(properties);
    }
}


/*
 *    Required for all modules:  the public API and the designated implementation functions.
 */
static const MODULE_APIS IdentityMap_APIS_all =
{
    IdentityMap_Create,
    IdentityMap_Destroy,
    IdentityMap_Receive
};

/*Codes_SRS_IDMAP_17_001 [ Module_GetAPIs shall return a non-NULL pointer to a MODULE_APIS structure.]*/
/*Codes_SRS_IDMAP_17_002: [The MODULE_APIS structure shall have non-NULL Module_Create, Module_Destroy, and Module_Receive fields.]*/
#ifdef BUILD_MODULE_TYPE_STATIC
MODULE_EXPORT const MODULE_APIS* MODULE_STATIC_GETAPIS(IDENTITYMAP_MODULE)(void)
#else
MODULE_EXPORT const MODULE_APIS* Module_GetAPIS(void)
#endif
{
    return &IdentityMap_APIS_all;
}
