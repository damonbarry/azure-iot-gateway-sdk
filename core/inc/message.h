// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

/** @file		message.h
*
*	@brief		Defines structures and function prototypes for creating, 
*				inspecting and disposing off messages published on to the 
*				message bus.
*
*	@details	A message essentially has two components:
*					- Properties represented as key/value pairs where both the
*					key and value are strings
*					- The content of the message which is simply a memory buffer
*					(a @c BUFFER_HANDLE)
*			
*				This header provides functions that enable the creation, inspection
*				and destruction of messages. A message is represented via a
*				#MESSAGE_HANDLE which is an opaque reference to a message
*				instance. Messages are immutable and reference counted; their 
*				lifetime is managed via the #Message_Clone and #Message_Destroy 
*				APIs which atomically increment and decrement the reference 
*				count respectively. #Message_Destroy deallocates the message 
*				completely when the reference count becomes zero.
*/

#ifndef MESSAGE_H
#define MESSAGE_H

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/map.h"
#include "azure_c_shared_utility/constmap.h"
#include "azure_c_shared_utility/constbuffer.h"

#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
extern "C"
{
#else
#include <stdint.h>
#include <stddef.h>
#endif

/** @brief Struct representing a particular message. */
typedef struct MESSAGE_HANDLE_DATA_TAG* MESSAGE_HANDLE;

/** @brief	Struct defining the Message configuration; messages are constructed 
*			using this structure.
*/
typedef struct MESSAGE_CONFIG_TAG
{
	/** @brief	Specifies the size of the buffer pointed at by @c source. This
	*			can be zero when the message has only properties and no content.
	*			It is an error for the size to be greater than zero when @c
	*			source is equal to @c NULL.
	*/
    size_t size;

	/** @brief	Pointer to the buffer containing the data that will be the 
	*			content of this message. This can be @c NULL when @c size is
	*			zero.
	*/
    const unsigned char* source;

	/** @brief	A collection of key/value pairs where both the key and value are
	*			strings representing the properties of this message. This field 
	*			must not be @c NULL.
	*/
    MAP_HANDLE sourceProperties;
}MESSAGE_CONFIG;

/** @brief Struct defining the Message buffer configuration. */
typedef struct MESSAGE_BUFFER_CONFIG_TAG
{
	/** @brief	The buffer containing the data that will be the content of the 
	*			message.
	*/
    CONSTBUFFER_HANDLE sourceContent;
    
	/** @brief	A collection of key/value pairs where both the key and value are
	*			strings representing the properties of this message. This field 
	*			must not be @c NULL.
	*/
	MAP_HANDLE sourceProperties;
}MESSAGE_BUFFER_CONFIG;

/** @brief		Creates a new reference counted message from a #MESSAGE_CONFIG
*				structure with the reference count initialized to 1.
*
*	@details	This function will create its own @c CONSTBUFFER and @c ConstMap
*				with copies of the @c source and @c sourceProperties contained
*				within the #MESSAGE_CONFIG structure parameter. It is the
*				responsibility of the Message to dispose of these resources.
*
*	@param		cfg		Pointer to a #MESSAGE_CONFIG structure.
*
*	@return		A non-NULL #MESSAGE_HANDLE for the newly created message, or NULL
*				upon failure.
*/
extern MESSAGE_HANDLE Message_Create(const MESSAGE_CONFIG* cfg);

/** @brief		Creates a new reference counted message from a byte array containing
*               the serialized form of a message.
*
*	@details	The newly created message shall have all the properties of the original
*               message and the same content.
*
*	@param		source		Pointer to a byte array.
*               size        size in bytes of the array
*
*	@return		A non-NULL #MESSAGE_HANDLE for the newly created message, or NULL
*				upon failure.
*/
extern MESSAGE_HANDLE Message_CreateFromByteArray(const unsigned char* source, int32_t size);

/** @brief		Creates a byte array representation of a MESSAGE_HANDLE. 
*
*	@details	The byte array created can be used with function #Message_CreateFromByteArray
*               to reproduce the message. The returned byte array needs to be deallocated by free function.
*
*	@param		messageHandle		A #MESSAGE_HANDLE. This parameter cannot be NULL.
*               arraySize           pointer to a int32_t that will receive the size of the byte array
*
*	@return		A non-NULL const unsigned char* is returned when creating the serialized form succeeds. *size is updated to the size of the array
*               A NULL value is returned when the array cannot be created.
*/
extern const unsigned char* Message_ToByteArray(MESSAGE_HANDLE messageHandle, const char* prefix, int32_t* arraySize);


/** @brief		Creates a new message from a @c CONSTBUFFER source and @c MAP_HANDLE.
*
*	@details	This function will create its own @c CONSTBUFFER and @c ConstMap
*				with copies of the @c source and @c sourceProperties contained
*				within the #MESSAGE_CONFIG structure parameter. It is the
*				responsibility of the Message to dispose of these resources.
*
*	@param		cfg		Pointer to a #MESSAGE_BUFFER_CONFIG structure.
*
*	@return		A non-NULL #MESSAGE_HANDLE for the newly created message, or @c NULL
*				upon failure.
*/
extern MESSAGE_HANDLE Message_CreateFromBuffer(const MESSAGE_BUFFER_CONFIG* cfg);

/** @brief		Creates a clone of the message.
*
*	@details	Since messages are immutable, this function only increments the inner
				reference count.
*
*	@param		message		The #MESSAGE_HANDLE that will be cloned.
*
*	@return		A non-NULL #MESSAGE_HANDLE cloned from @c message, or @c NULL upon
*				failure.
*/
extern MESSAGE_HANDLE Message_Clone(MESSAGE_HANDLE message);

/** @brief		Gets the properties of a message.
*
*	@details	The returned @c CONSTMAP handle should be destroyed when no longer
*				needed.
*
*	@param		message		The #MESSAGE_HANDLE from which properties will be
*							fetched.
*
*	@return		A non-NULL @c CONSTMAP_HANDLE representing the properties of the
*				message, or @c NULL upon failure.
*
*/
extern CONSTMAP_HANDLE Message_GetProperties(MESSAGE_HANDLE message);

/** @brief		Gets the content of a message.
*
*	@details	The returned @c CONSTMAP_HANDLE need not be freed by the caller.
*
*	@param		message		The #MESSAGE_HANDLE from which the content will be
*							fetched.
*
*	@return		A non-NULL pointer to a @c CONSTBUFFER representing the content of 
*				the message, or @c NULL upon failure.
*/
extern const CONSTBUFFER* Message_GetContent(MESSAGE_HANDLE message);

/** @brief		Gets the @c CONSTBUFFER handle that may be used to access the message
*				content.
*
*	@details	This handle must be destroyed when no longer needed.
*
*	@param		message		The #MESSAGE_HANDLE from which the content will be
*							fetched.
*							
*	@return		A non-NULL @c CONSTBUFFER_HANDLE representing the message content, or
*				@c NULL upon failure.
*/
extern CONSTBUFFER_HANDLE Message_GetContentHandle(MESSAGE_HANDLE message);

/** @brief Disposes of resources allocated by the message.
*
*	@param message		The #MESSAGE_HANDLE to be destroyed.
*/
extern void Message_Destroy(MESSAGE_HANDLE message);

#ifdef __cplusplus
}
#else
#endif

#endif /*MESSAGE_H*/
