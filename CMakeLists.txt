# qpm generates qpm_defines.cmake and extern.cmake during `qpm restore`
include(qpm_defines.cmake)

cmake_minimum_required(VERSION 3.21)
project(${COMPILE_ID})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_compile_options(-frtti -fexceptions -O3)
add_compile_definitions(VERSION=\"${MOD_VERSION}\")
add_compile_definitions(MOD_ID=\"${MOD_ID}\")

string(LENGTH "${CMAKE_CURRENT_LIST_DIR}/" FOLDER_LENGTH)
add_compile_definitions("PAPER_ROOT_FOLDER_LENGTH=${FOLDER_LENGTH}")

# the Archipelago client and its websocket/TLS stack, shared with the host tests
set(RAPIDJSON_INCLUDE_DIR ${EXTERN_DIR}/includes/beatsaber-hook/shared/rapidjson/include)
include(${CMAKE_CURRENT_LIST_DIR}/core/ArchipelagoCore.cmake)

recurse_files(cpp_file_list ${SOURCE_DIR}/*.cpp)
recurse_files(inline_hook_c ${EXTERN_DIR}/includes/beatsaber-hook/shared/inline-hook/*.c)
recurse_files(inline_hook_cpp ${EXTERN_DIR}/includes/beatsaber-hook/shared/inline-hook/*.cpp)

add_library(${COMPILE_ID} SHARED ${cpp_file_list} ${inline_hook_c} ${inline_hook_cpp})

target_include_directories(${COMPILE_ID} PRIVATE ${INCLUDE_DIR})
target_include_directories(${COMPILE_ID} PRIVATE ${RAPIDJSON_INCLUDE_DIR})
target_link_libraries(${COMPILE_ID} PRIVATE -llog archipelago_core)

include(extern.cmake)

add_custom_command(
    TARGET ${COMPILE_ID}
    POST_BUILD
    COMMAND ${CMAKE_STRIP} -d --strip-all "lib${COMPILE_ID}.so" -o "stripped_lib${COMPILE_ID}.so"
    COMMENT "Strip debug symbols from the final binary"
)
add_custom_command(
    TARGET ${COMPILE_ID}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory debug
    COMMENT "Make a folder for the unstripped binary"
)
add_custom_command(
    TARGET ${COMPILE_ID}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rename lib${COMPILE_ID}.so debug/lib${COMPILE_ID}.so
    COMMENT "Keep the unstripped binary for crash symbolication"
)
add_custom_command(
    TARGET ${COMPILE_ID}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rename stripped_lib${COMPILE_ID}.so lib${COMPILE_ID}.so
    COMMENT "Use the stripped binary as the release library"
)
