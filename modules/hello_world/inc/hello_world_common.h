// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef HELLO_WORLD_COMMON_H
#define HELLO_WORLD_COMMON_H

/*the below data structures are used by all versions of Hello World (static/dynamic vanilla/hl)*/
typedef struct HELLO_WORLD_CONFIG_TAG
{
    const char* brokerAddress;
    const char* brokerTopic;
}HELLO_WORLD_CONFIG; /*this needs to be passed to the Module_Create function*/

#endif /*HELLO_WORLD_COMMON_H*/
