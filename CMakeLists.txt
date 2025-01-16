cmake_minimum_required (VERSION 3.10)
project (webclient)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DALLOW_SUDO")

add_definitions (-DLOCAL_DBG)

if (POLICY CMP0091)
    cmake_policy (SET CMP0091 NEW)
endif (POLICY CMP0091)

set (Boost_USE_STATIC_LIBS OFF)
set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_RUNTIME OFF)

set (CMAKE_CXX_FLAGS " -DBOOST_LOG_DYN_LINK")

# find_package (Boost REQUIRED COMPONENTS json url)
set (Boost_DIR /opt/homebrew/opt/boost)
set (Boost_INCLUDE_DIR ${Boost_DIR}/include)
set (Boost_LIB_DIR ${Boost_DIR}/lib)

find_package (OpenSSL REQUIRED COMPONENTS Crypto SSL)
set (OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl)
set (OPENSSL_INCLUDE_DIR ${OPENSSL_ROOT_DIR}/include)
set (OPENSSL_LIB_DIR ${OPENSSL_ROOT_DIR}/lib)

include_directories (${Boost_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR})

link_directories (${Boost_LIB_DIR} ${OPENSSL_LIB_DIR})

add_executable (webclient
        main.cpp
        web-socket.cpp
        Http.cpp
        http-utils.cpp
        Log.cpp
)

# vs MT/MTd
set_property (TARGET webclient PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries (webclient PRIVATE
        boost_url
        boost_json
        boost_log_setup
        boost_log
        boost_filesystem
        boost_thread
        ${OPENSSL_SSL_LIBRARY}
        ${OPENSSL_CRYPTO_LIBRARY}
)
