// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

/** @file		module.h
*	@brief		Interface for modules which connect to a message bus.
*
*	@details	Every module on the message bus must implement this interface.
*				A module can only belong to 1 message bus and a message bus may
*				have many modules connected to it.
*
*				A module is a pointer to a structure containing several function
*				pointers. By convention, every module exports a function that 
*				returns a pointer to an instance of the #MODULE_APIS structure..
*/

#ifndef MODULE_H
#define MODULE_H

/** @brief Represents a handle to a particular module.*/
typedef void* MODULE_HANDLE;
typedef struct MODULE_APIS_TAG MODULE_APIS;

#include "azure_c_shared_utility/macro_utils.h"
#include "message.h"

#ifdef __cplusplus
extern "C"
{
#endif

	/** @brief		Creates a module using the specified configuration connecting
	*				to the specified message bus.
	*
	*	@details	This function is to be implemented by the module creator.
	*
	*	@param		configureation	A pointer to the user-defined configuration 
	*								structure for this module.
	*
	*	@return		A non-NULL #MODULE_HANDLE upon success, or @c NULL upon 
	*			failure.
	*/
    typedef MODULE_HANDLE(*pfModule_Create)(const void* configuration);

	/** @brief		Disposes of the resources allocated by/for this module.
	*
	*	@details	This function is to be implemented by the module creator.
	*
	*	@param		moduleHandle	The #MODULE_HANDLE of the module to be destroyed.
	*/
    typedef void(*pfModule_Destroy)(MODULE_HANDLE moduleHandle);

	/** @brief	Structure returned by ::Module_GetAPIS containing the function
	*			pointers of the module-specific implementations of the interface.
	*/
    typedef struct MODULE_APIS_TAG
    {
		/** @brief Function pointer to the #Module_Create function. */
        pfModule_Create Module_Create;

		/** @brief Function pointer to the #Module_Destroy function. */
        pfModule_Destroy Module_Destroy;
    }MODULE_APIS;

	/** @brief	This is the only function exported by a module. Using the 
	*			exported function, the caller learns the functions for the 
	*			particular module.
	*/
    typedef const MODULE_APIS* (*pfModule_GetAPIS)(void);

    /** @brief Returns the module APIS name.*/
#define MODULE_GETAPIS_NAME ("Module_GetAPIS")

#ifdef _WIN32
#define MODULE_EXPORT __declspec(dllexport)
#else
#define MODULE_EXPORT
#endif // _WIN32

/** @brief	Forms a new name that is by convention the name of the (only) exported 
*			function from a static module library.
*/
#define MODULE_STATIC_GETAPIS(MODULE_NAME) C2(Module_GetAPIS_, MODULE_NAME)

/** @brief	This is the only function exported by a module under a "by
*			convention" name. Using the exported function, the caller learns
*			the functions for the particular module.
*/
MODULE_EXPORT const MODULE_APIS* Module_GetAPIS(void);

#ifdef __cplusplus
}
#endif

#endif // MODULE_H
