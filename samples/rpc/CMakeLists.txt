#Copyright (c) Microsoft. All rights reserved.
#Licensed under the MIT license. See LICENSE file in the project root for full license information.

cmake_minimum_required(VERSION 2.8.12)

set(rpc_sample_sources
    ./src/main.c
)
if(WIN32)
    set(rpc_sample_sources 
        ${rpc_sample_sources}
        ./src/rpc_windows.json
    )
    set_source_files_properties(./src/rpc_windows.json PROPERTIES HEADER_FILE_ONLY ON)
else()
    set(rpc_sample_sources 
        ${rpc_sample_sources}
        ./src/rpc_linux.json
    )
    set_source_files_properties(./src/rpc_linux.json PROPERTIES HEADER_FILE_ONLY ON)
endif()

include_directories(./inc ${IOTHUB_CLIENT_INC_FOLDER})
include_directories(${GW_INC})
include_directories(../../modules/common)

add_executable(rpc_sample ${rpc_sample_sources})

add_dependencies(rpc_sample rpc_source rpc_sink)

target_link_libraries(rpc_sample gateway nanomsg)
linkSharedUtil(rpc_sample)
install_broker(rpc_sample ${CMAKE_CURRENT_BINARY_DIR}/$(Configuration) )
copy_gateway_dll(rpc_sample ${CMAKE_CURRENT_BINARY_DIR}/$(Configuration) )

add_sample_to_solution(rpc_sample)