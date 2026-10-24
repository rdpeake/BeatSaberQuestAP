# platform-independent Archipelago client, shared by the mod and the host tests
# expects RAPIDJSON_INCLUDE_DIR to point at a rapidjson include directory

include(FetchContent)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v3.6.2
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v11.4.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(mbedtls)
FetchContent_Populate(ixwebsocket)

set(IXWEBSOCKET_SOURCE_NAMES
    IXBench IXCancellationRequest IXConnectionState IXDNSLookup IXExponentialBackoff
    IXGetFreePort IXGzipCodec IXHttp IXHttpClient IXHttpServer IXNetSystem
    IXSelectInterrupt IXSelectInterruptFactory IXSelectInterruptPipe IXSelectInterruptEvent
    IXSetThreadName IXSocket IXSocketConnect IXSocketFactory IXSocketServer IXSocketTLSOptions
    IXStrCaseCompare IXUdpSocket IXUrlParser IXUuid IXUserAgent IXWebSocket
    IXWebSocketCloseConstants IXWebSocketHandshake IXWebSocketHttpHeaders
    IXWebSocketPerMessageDeflate IXWebSocketPerMessageDeflateCodec IXWebSocketPerMessageDeflateOptions
    IXWebSocketProxyServer IXWebSocketServer IXWebSocketTransport IXSocketMbedTLS
)
list(TRANSFORM IXWEBSOCKET_SOURCE_NAMES PREPEND ${ixwebsocket_SOURCE_DIR}/ixwebsocket/)
list(TRANSFORM IXWEBSOCKET_SOURCE_NAMES APPEND .cpp OUTPUT_VARIABLE IXWEBSOCKET_SOURCES)

add_library(ixwebsocket_static STATIC ${IXWEBSOCKET_SOURCES})
target_include_directories(ixwebsocket_static PUBLIC ${ixwebsocket_SOURCE_DIR})
target_compile_definitions(ixwebsocket_static PUBLIC
    IXWEBSOCKET_USE_TLS
    IXWEBSOCKET_USE_MBED_TLS
    IXWEBSOCKET_USE_ZLIB
)
target_compile_definitions(ixwebsocket_static PRIVATE IXWEBSOCKET_USE_MBED_TLS_MIN_VERSION_3)
target_compile_options(ixwebsocket_static PRIVATE -w)
find_package(Threads REQUIRED)
target_link_libraries(ixwebsocket_static PUBLIC mbedtls mbedx509 mbedcrypto z Threads::Threads)

add_library(archipelago_core STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/ApbsArchive.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ArchipelagoClient.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/BeatSaverKeyCache.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/CertificateStore.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/LevelIdentity.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ProgressionRules.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/SlotData.cpp
)
target_include_directories(archipelago_core PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include ${RAPIDJSON_INCLUDE_DIR})
target_compile_features(archipelago_core PUBLIC cxx_std_20)
target_link_libraries(archipelago_core PRIVATE ixwebsocket_static z)
