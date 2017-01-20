#Copyright (c) Microsoft. All rights reserved.
#Licensed under the MIT license. See LICENSE file in the project root for full license information.

cmake_minimum_required(VERSION 2.8.12)

set(rpc_sink_sources
    ./src/rpc_sink.c
)

set(rpc_sink_headers
    ./inc/rpc_sink.h
)

include_directories(./inc)
include_directories(${GW_INC})

#this builds the rpc_sink dynamic library
add_library(rpc_sink MODULE ${rpc_sink_sources} ${rpc_sink_headers})
target_link_libraries(rpc_sink gateway)

#this builds the rpc_sink static library
add_library(rpc_sink_static STATIC ${rpc_sink_sources} ${rpc_sink_headers})
target_compile_definitions(rpc_sink_static PRIVATE BUILD_MODULE_TYPE_STATIC)
target_link_libraries(rpc_sink_static gateway)

linkSharedUtil(rpc_sink)
linkSharedUtil(rpc_sink_static)

add_module_to_solution(rpc_sink)

if(install_modules)
    install(TARGETS rpc_sink LIBRARY DESTINATION "${LIB_INSTALL_DIR}/modules") 
endif()

