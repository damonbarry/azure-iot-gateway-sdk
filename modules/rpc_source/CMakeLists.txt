#Copyright (c) Microsoft. All rights reserved.
#Licensed under the MIT license. See LICENSE file in the project root for full license information.

cmake_minimum_required(VERSION 2.8.12)

set(rpc_source_sources
    ./src/rpc_source.cpp
    ../rpc_sink/src/devices_comm.cpp
    ../rpc_sink/src/devices_types.cpp
)

set(rpc_source_headers
    ./inc/rpc_source.h
)

include_directories(./inc)
include_directories(../rpc_sink/src)
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

#this builds the rpc_source dynamic library
add_library(rpc_source MODULE ${rpc_source_sources} ${rpc_source_headers})
#target_link_libraries(rpc_source gateway)
#target_link_libraries(rpc_source gateway ${Boost_LIBRARIES})
target_link_libraries(rpc_source gateway Boost::system Boost::thread ${BOND_LIBRARY} ${BOND_APPLY_LIBRARY})
target_compile_definitions(rpc_source PRIVATE -D_WIN32_WINNT=0x0600)

#this builds the rpc_source static library
#add_library(rpc_source_static STATIC ${rpc_source_sources} ${rpc_source_headers})
#target_compile_definitions(rpc_source_static PRIVATE BUILD_MODULE_TYPE_STATIC)
#target_link_libraries(rpc_source_static gateway)

linkSharedUtil(rpc_source)
#linkSharedUtil(rpc_source_static)

add_module_to_solution(rpc_source)

if(install_modules)
    install(TARGETS rpc_source LIBRARY DESTINATION "${LIB_INSTALL_DIR}/modules") 
endif()

