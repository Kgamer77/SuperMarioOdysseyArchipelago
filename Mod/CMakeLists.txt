cmake_minimum_required(VERSION 3.16)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/sys/cmake/toolchain.cmake")

add_compile_options(-fconstexpr-steps=5000000)
set(DEFAULTDEFINES)


project(${PROJNAME} VERSION 1.0 LANGUAGES ASM C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)


include(config/config.cmake)

set(BUILD_TESTING OFF)
set(BUILD_TESTS OFF)
set(JSON_USE_EXCEPTION OFF)
set(JSON_USE_EXCEPTION OFF CACHE STRING "Disable JSON Exceptions")


option(SMOR_FEATURE_BINGO_ASCEND "Enable the Bingo and Ascend game modes" ON)
option(SMOR_FEATURE_TOADETTE_MEDALS "Enable the Toadette Medal Hunt game mode" ON)
option(SMOR_FEATURE_SUBAREA_KEYS "Enable Subarea Keys (kingdom sub-areas locked by entrance type)" ON)
option(SMOR_FEATURE_NUZLOCKE_MODES "Enable the Nuzlocke and Soft Nuzlocke run modes" ON)
option(SMOR_FEATURE_TIMER_MODE "Enable the Timer run mode (countdown clock, moons buy time)" ON)
option(SMOR_FEATURE_ENEMY_RANDOMIZATION "Enable the enemy/capture, NPC and 2D Chaos Randomization options" ON)
option(SMOR_FEATURE_PROP_RANDOMIZATION "Enable the prop Chaos Randomization options" ON)
option(SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION "Enable the Triple Moon randomization options (Anywhere / Randomized)" ON)
option(SMOR_FEATURE_MULTIPLAYER "Enable Multiplayer (title-menu room join, player sync, MP save files)" ON)
option(SMOR_FEATURE_ARCHIPELAGO "Enable the Archipelago title-menu entry" ON)
option(SMOR_UID_CHECK "UID Check to Play the Mod" OFF)


# Both halves of Chaos Randomization share one placement engine, one generated
# database and one set of runtime hooks. Everything below the option surface is
# gated on this instead of on either half.
if(SMOR_FEATURE_ENEMY_RANDOMIZATION OR SMOR_FEATURE_PROP_RANDOMIZATION)
    set(SMOR_FEATURE_CHAOS_ANY ON)
else()
    set(SMOR_FEATURE_CHAOS_ANY OFF)
endif()

# Dedicated multiplayer server address (see Client::init()) - defaults to the
# real public address. Override for a build that only needs to reach the
# server over LAN/hotspot (e.g. testing with the Switch tethered to the same
# PC hosting the server) to sidestep NAT hairpinning, which a lot of
# routers/hotspots don't support for a device looping back to its own public
# IP through the internet:
#   cmake -B build -DSMOR_MP_SERVER_IP=10.42.0.1
set(SMOR_MP_SERVER_IP "71.9.200.11" CACHE STRING "IP address the mod's Client connects to")
set(SMOR_MP_SERVER_PORT "1027" CACHE STRING "Port the mod's Client connects to")

set(LOGIC_DATA_HEADER ${PROJECT_SOURCE_DIR}/include/Randomizer/LogicData.h)
set(LOGIC_DATA_INPUTS
    ${PROJECT_SOURCE_DIR}/logic/templates/logic_tokens.csv
    ${PROJECT_SOURCE_DIR}/logic/templates/rooms.csv
    ${PROJECT_SOURCE_DIR}/logic/templates/logic_nodes.csv
    ${PROJECT_SOURCE_DIR}/logic/templates/route_edges.csv
    ${PROJECT_SOURCE_DIR}/logic/templates/moon_access.csv
    ${PROJECT_SOURCE_DIR}/logic/templates/entrance_links.csv
    ${PROJECT_SOURCE_DIR}/logic/templates/progression_events.csv
    ${PROJECT_SOURCE_DIR}/logic/templates/painting_links.csv
    ${PROJECT_SOURCE_DIR}/logic/randomizer_logic.py
    ${PROJECT_SOURCE_DIR}/logic/generate_runtime_logic.py
)
add_custom_command(
    OUTPUT ${LOGIC_DATA_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/logic/generate_runtime_logic.py
        --input ${PROJECT_SOURCE_DIR}/logic/templates
        --output ${LOGIC_DATA_HEADER}
    DEPENDS ${LOGIC_DATA_INPUTS}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Generating Switch runtime logic tables"
)
add_custom_target(generate_logic_data DEPENDS ${LOGIC_DATA_HEADER})

if(SMOR_FEATURE_CHAOS_ANY)
    set(ENEMY_SWAP_DATA_HEADER ${PROJECT_SOURCE_DIR}/include/Randomizer/EnemySwapDB.h)
    set(ENEMY_SWAP_DATA_INPUTS
        ${PROJECT_SOURCE_DIR}/config/enemy_swap_entities.csv
        ${PROJECT_SOURCE_DIR}/config/enemy_swap_costs.csv
        ${PROJECT_SOURCE_DIR}/config/enemy_swap_spatial.csv
        ${PROJECT_SOURCE_DIR}/config/enemy_swap_stage_budgets.csv
        ${PROJECT_SOURCE_DIR}/config/enemy_spawn_restrictions.csv
        ${PROJECT_SOURCE_DIR}/config/enemy_capture_map.csv
        ${PROJECT_SOURCE_DIR}/config/enemy_swap_rules.csv
        ${PROJECT_SOURCE_DIR}/logic/templates/capture_gates.csv
        ${PROJECT_SOURCE_DIR}/logic/templates/capture_gate_objects.csv
        ${PROJECT_SOURCE_DIR}/logic/templates/logic_tokens.csv
        ${PROJECT_SOURCE_DIR}/logic/templates/logic_nodes.csv
        ${PROJECT_SOURCE_DIR}/logic/templates/route_edges.csv
        ${PROJECT_SOURCE_DIR}/logic/templates/moon_access.csv
        ${PROJECT_SOURCE_DIR}/logic/enemy_swap.py
        ${PROJECT_SOURCE_DIR}/tools/generate_enemy_swap_costs.py
        ${PROJECT_SOURCE_DIR}/tools/generate_enemy_swap_db.py
    )
    add_custom_command(
        OUTPUT ${ENEMY_SWAP_DATA_HEADER}
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/generate_enemy_swap_db.py
            --strict
            --output ${ENEMY_SWAP_DATA_HEADER}
        DEPENDS ${ENEMY_SWAP_DATA_INPUTS}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Generating enemy/capture placement and gate tables"
    )
    add_custom_target(generate_enemy_swap_data DEPENDS ${ENEMY_SWAP_DATA_HEADER})
endif()

file(GLOB_RECURSE SOURCES_ASM ${PROJECT_SOURCE_DIR}/src/*.S)
file(GLOB_RECURSE SOURCES_C ${PROJECT_SOURCE_DIR}/src/*.c)
file(GLOB_RECURSE SOURCES_CXX ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE SOURCES_APCPP ${PROJECT_SOURCE_DIR}/lib/APCppNX/*.cpp)
file(GLOB_RECURSE SOURCES_CJSON ${PROJECT_SOURCE_DIR}/lib/cJSON/*.c)
# file(GLOB_RECURSE SOURCES_ZLIB ${PROJECT_SOURCE_DIR}/lib/APCpp/zlib/*.c)
set(SOURCES_ZLIB 
${PROJECT_SOURCE_DIR}/lib/APCppNX/zlib/inflate.c
${PROJECT_SOURCE_DIR}/lib/APCppNX/zlib/compress.c
${PROJECT_SOURCE_DIR}/lib/APCppNX/zlib/deflate.c
${PROJECT_SOURCE_DIR}/lib/APCppNX/zlib/zutil.c
${PROJECT_SOURCE_DIR}/lib/APCppNX/zlib/crc32.c
${PROJECT_SOURCE_DIR}/lib/APCppNX/zlib/trees.c
${PROJECT_SOURCE_DIR}/lib/APCppNX/zlib/adler32.c
)

list(FILTER SOURCES_APCPP EXCLUDE REGEX "/.*(test|mock|example|ws).*/")
list(FILTER SOURCES_APCPP EXCLUDE REGEX "/(test|main|httpd|linenoise|zfstream|zstream|msgpack11|CLI11).*")
# list(FILTER SOURCES_ZLIB EXCLUDE REGEX "*/contrib/*")
# list(FILTER SOURCES_ZLIB EXCLUDE REGEX "/(gz)*.")
# list(FILTER SOURCES_APCPP EXCLUDE REGEX "/IX*(SelectInterruptPipe|Agent|Uuid)*.*")

set(stringstream NintendoSDK-NEX/OnlineCore/Platform/Core/StringStream.h)

# Vendored mbedTLS for the Firestore live-tracking HTTPS calls - not under
# src/, so it needs its own glob rather than being picked up by SOURCES_C above.
file(GLOB SOURCES_MBEDTLS ${PROJECT_SOURCE_DIR}/lib/mbedtls/library/*.c)
set(SOURCES ${SOURCES_ASM} ${SOURCES_C} ${SOURCES_CXX} ${SOURCES_MBEDTLS} ${SOURCES_CJSON} ${SOURCES_ZLIB} ${SOURCES_APCPP})
if(NOT SMOR_FEATURE_CHAOS_ANY)
    # The headers provide no-op API stubs for MoonRando/Randomizer, so the
    # feature implementation and its placement hooks need not be compiled.
    list(REMOVE_ITEM SOURCES
        ${PROJECT_SOURCE_DIR}/src/Randomizer/EnemySwapRando.cpp
        ${PROJECT_SOURCE_DIR}/src/Randomizer/EnemySwapHooks.cpp
        ${PROJECT_SOURCE_DIR}/src/Randomizer/SpawnTuning.cpp
    )
endif()

# set(MBEDTLS_CONFIG_FILE "${PROJECT_SOURCE_DIR}/lib/mbedtls/library/mbedtls_config.h" CACHE STRING "mbedtls Config File")
set(ZLIB_BUILD_TESTING OFF CACHE STRING "Enable Zlib examples and tests")

add_executable(${PROJECT_NAME} ${SOURCES})
# target_link_libraries(${PROJECT_NAME} PRIVATE APCpp)

target_compile_options(${PROJECT_NAME} PRIVATE -w)
add_dependencies(${PROJECT_NAME} generate_logic_data)
if(SMOR_FEATURE_CHAOS_ANY)
    add_dependencies(${PROJECT_NAME} generate_enemy_swap_data)
endif()

message(STATUS "The current source directory is: ${CMAKE_CURRENT_SOURCE_DIR}")

include_directories(lib)

include_directories(lib/imgui)
include_directories(lib/mbedtls/include)

include_directories(lib/custom)
include_directories(lib/custom/al)
include_directories(lib/custom/game)
include_directories(lib/custom/nn)
include_directories(lib/custom/rs)

include_directories(lib/OdysseyHeaders)
include_directories(lib/OdysseyHeaders/sead)
include_directories(lib/OdysseyHeaders/al)
include_directories(lib/OdysseyHeaders/agl)
include_directories(lib/OdysseyHeaders/game)
include_directories(lib/OdysseyHeaders/NintendoSDK)

include_directories(lib/APCppNX)

include_directories(lib/cJSON)
include_directories(lib/APCppNX/zlib)

include_directories(include)

include(sys/cmake/module.cmake)
include(sys/cmake/deploy.cmake)
include(sys/cmake/sail.cmake)
usesail(${PROJECT_NAME})

# sys/cmake/deploy.cmake's deploy.py only copies exefs (main.npdm/subsdk) to
# build/sd - it never copies romfs. The makefile's `release`/file_structure
# target does copy romfs, but to package/<name>-Switch/atmosphere/, a
# different folder meant for zipped release packaging, not the build/sd
# output `make debug` actually deploys and real-hardware testing reads from.
# Without this, layout actors that load a byml out of romfs (e.g.
# Speedboot's SpeedbootLoad) get a null Resource and crash at boot on any
# environment relying on `make debug` alone - confirmed via a boot-time
# Data Abort crash log resolving straight into that constructor.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/romfs
        ${CMAKE_CURRENT_BINARY_DIR}/sd/atmosphere/contents/0100000000010000/romfs
    COMMENT "Copying romfs to SD folder"
)

# SMOVER ?= 100
# BUILDVER ?= 101
# BUILDVERSTR ?= $(shell date +%Y.%m.%d) - recomputed every `make` invocation
# BUILDDATESTR ?= $(shell date +%H:%M) - same, shown alongside BUILDVERSTR
# DEBUGLOG ?= 0 # defaults to disable debug logger
# SERVERIP ?= 0.0.0.0 # put debug logger server IP here
target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUGLOG=${DEBUGLOG})
target_compile_definitions(${PROJECT_NAME} PRIVATE SERVERIP=${SERVERIP})
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_MP_SERVER_IP="${SMOR_MP_SERVER_IP}")
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_MP_SERVER_PORT=${SMOR_MP_SERVER_PORT})
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOVER=${SMOVER})
target_compile_definitions(${PROJECT_NAME} PRIVATE BUILDVER=${BUILDVER})
target_compile_definitions(${PROJECT_NAME} PRIVATE BUILDVERSTR=${BUILDVERSTR})
target_compile_definitions(${PROJECT_NAME} PRIVATE BUILDDATESTR=${BUILDDATESTR})
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_FEATURE_BINGO_ASCEND=$<BOOL:${SMOR_FEATURE_BINGO_ASCEND}>)
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_FEATURE_TOADETTE_MEDALS=$<BOOL:${SMOR_FEATURE_TOADETTE_MEDALS}>)
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_FEATURE_SUBAREA_KEYS=$<BOOL:${SMOR_FEATURE_SUBAREA_KEYS}>)
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_FEATURE_NUZLOCKE_MODES=$<BOOL:${SMOR_FEATURE_NUZLOCKE_MODES}>)
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_FEATURE_TIMER_MODE=$<BOOL:${SMOR_FEATURE_TIMER_MODE}>)
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_FEATURE_ENEMY_RANDOMIZATION=$<BOOL:${SMOR_FEATURE_ENEMY_RANDOMIZATION}>)
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_FEATURE_PROP_RANDOMIZATION=$<BOOL:${SMOR_FEATURE_PROP_RANDOMIZATION}>)
# Defined here as well as derived in FeatureFlags.h, so translation units
# which never include that header still agree on the umbrella.
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_FEATURE_CHAOS_ANY=$<BOOL:${SMOR_FEATURE_CHAOS_ANY}>)
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION=$<BOOL:${SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION}>)
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_FEATURE_MULTIPLAYER=$<BOOL:${SMOR_FEATURE_MULTIPLAYER}>)
target_compile_definitions(${PROJECT_NAME} PRIVATE SMOR_FEATURE_ARCHIPELAGO=$<BOOL:${SMOR_FEATURE_ARCHIPELAGO}>)

if(DEBUG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SMOOHKDBG)
endif()
