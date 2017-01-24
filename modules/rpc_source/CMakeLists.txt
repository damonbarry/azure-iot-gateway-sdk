#Copyright (c) Microsoft. All rights reserved.
#Licensed under the MIT license. See LICENSE file in the project root for full license information.

cmake_minimum_required(VERSION 2.8.12)

set(rpc_source_sources
    ./src/rpc_source.cpp
)

set(rpc_source_headers
    ./inc/rpc_source.h
)

include_directories(./inc)
include_directories(${GW_INC})

#this builds the rpc_source dynamic library
add_library(rpc_source MODULE ${rpc_source_sources} ${rpc_source_headers})
target_link_libraries(rpc_source gateway)

#this builds the rpc_source static library
add_library(rpc_source_static STATIC ${rpc_source_sources} ${rpc_source_headers})
target_compile_definitions(rpc_source_static PRIVATE BUILD_MODULE_TYPE_STATIC)
target_link_libraries(rpc_source_static gateway)

linkSharedUtil(rpc_source)
linkSharedUtil(rpc_source_static)

add_module_to_solution(rpc_source)

if(install_modules)
    install(TARGETS rpc_source LIBRARY DESTINATION "${LIB_INSTALL_DIR}/modules") 
endif()

