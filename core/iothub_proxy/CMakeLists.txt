#Copyright (c) Microsoft. All rights reserved.
#Licensed under the MIT license. See LICENSE file in the project root for full license information.

cmake_minimum_required(VERSION 2.8.12)

set(iothub_proxy_sources
    ./src/iothub_proxy.cpp
    ../../modules/rpc_sink/src/devices_comm.cpp
    ../../modules/rpc_sink/src/devices_types.cpp
)

set(rpc_sink_headers
)

include_directories(./inc)
include_directories(../../modules/rpc_sink/src)

find_package(azure_iot_sdks REQUIRED)
include_directories(${IOTHUB_CLIENT_INCLUDES})
message(STATUS ">>>>> IOTHUB_CLIENT_INCLUDES=${IOTHUB_CLIENT_INCLUDES}")
include_directories(${IOTHUB_CLIENT_INC_FOLDER})
message(STATUS ">>>>> IOTHUB_CLIENT_INC_FOLDER=${IOTHUB_CLIENT_INC_FOLDER}")

find_path(BOND_INCLUDES
    NAMES bond/core/bond.h
    PATHS "C:/Program Files/bond/include" #TODO: Don't hard-code!!
    )
find_library(BOND_LIBRARY
    NAMES bond.lib
    PATHS "C:/Program Files/bond/lib/bond" #TODO: Don't hard-code!!
    )
find_library(BOND_APPLY_LIBRARY
    NAMES bond_apply.lib
    PATHS "C:/Program Files/bond/lib/bond" #TODO: Don't hard-code!!
    )

include_directories(${BOND_INCLUDES})

#set(Boost_DEBUG ON)
add_definitions("-DBOOST_LIB_DIAGNOSTIC")
add_definitions("-DBOOST_ALL_NO_LIB")

set(BOOST_LIBRARYDIR "C:/boost_1_63_0/lib64-msvc-14.0")
find_package(Boost REQUIRED COMPONENTS system thread)

include_directories(${Boost_INCLUDE_DIRS})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")

add_library(iothub_proxy STATIC ${iothub_proxy_sources} ${iothub_proxy_headers})
target_compile_definitions(iothub_proxy PRIVATE -D_WIN32_WINNT=0x0600)
target_link_libraries(iothub_proxy iothub_client Boost::system Boost::thread ${BOND_LIBRARY} ${BOND_APPLY_LIBRARY})

add_module_to_solution(iothub_proxy)
