cmake_minimum_required(VERSION 3.21)
project(beatsaberquestap_host_tests CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(RAPIDJSON_HAS_STDSTRING=1)

include(FetchContent)
FetchContent_Declare(
    rapidjson
    GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
    GIT_TAG 24b5e7a8b27f42fa16b96fc70aade9106cf7102f
)
FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
    FetchContent_Populate(rapidjson)
endif()
set(RAPIDJSON_INCLUDE_DIR ${rapidjson_SOURCE_DIR}/include)

include(${CMAKE_CURRENT_LIST_DIR}/../core/ArchipelagoCore.cmake)

add_executable(progression_tests progression_tests.cpp)
target_link_libraries(progression_tests PRIVATE archipelago_core)

add_executable(client_tests client_tests.cpp)
target_link_libraries(client_tests PRIVATE archipelago_core ixwebsocket_static)

enable_testing()
add_test(NAME progression_tests COMMAND progression_tests)
add_test(NAME client_tests COMMAND client_tests)

add_executable(storage_tests storage_tests.cpp)
target_link_libraries(storage_tests PRIVATE archipelago_core)
target_compile_definitions(storage_tests PRIVATE FIXTURE_DIRECTORY="${CMAKE_CURRENT_LIST_DIR}/fixtures")
add_test(NAME storage_tests COMMAND storage_tests)
