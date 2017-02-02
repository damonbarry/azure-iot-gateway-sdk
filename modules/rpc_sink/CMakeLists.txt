#Copyright (c) Microsoft. All rights reserved.
#Licensed under the MIT license. See LICENSE file in the project root for full license information.

cmake_minimum_required(VERSION 2.8.12)

set(rpc_sink_sources
    ./src/rpc_sink.cpp
    ../rpc_sink/src/devices_comm.cpp
    ../rpc_sink/src/devices_types.cpp
)

set(rpc_sink_headers
    ./inc/rpc_sink.h
)

include_directories(./inc)
include_directories(${GW_INC})

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

#this builds the rpc_sink dynamic library
add_library(rpc_sink MODULE ${rpc_sink_sources} ${rpc_sink_headers})
target_compile_definitions(rpc_sink PRIVATE -D_WIN32_WINNT=0x0600)
target_link_libraries(rpc_sink gateway iothub_client Boost::system Boost::thread ${BOND_LIBRARY} ${BOND_APPLY_LIBRARY})
linkUAMQP(rpc_sink)

#this builds the rpc_sink static library
#add_library(rpc_sink_static STATIC ${rpc_sink_sources} ${rpc_sink_headers})
#target_compile_definitions(rpc_sink_static PRIVATE BUILD_MODULE_TYPE_STATIC)
#target_link_libraries(rpc_sink_static gateway)

linkSharedUtil(rpc_sink)
#linkSharedUtil(rpc_sink_static)

add_module_to_solution(rpc_sink)

if(install_modules)
    install(TARGETS rpc_sink LIBRARY DESTINATION "${LIB_INSTALL_DIR}/modules") 
endif()

