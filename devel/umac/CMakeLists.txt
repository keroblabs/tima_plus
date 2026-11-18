cmake_minimum_required(VERSION 3.16)
set(PROJECT_NAME umac)
project(${PROJECT_NAME} LANGUAGES C)

set(MEMSIZE "1024" CACHE STRING "Emulated RAM size in KiB")
set(DISP_WIDTH "512" CACHE STRING "Display width in pixels")
set(DISP_HEIGHT "342" CACHE STRING "Display height in pixels")

find_package(SDL2 CONFIG REQUIRED)

set(MUSASHI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Musashi")
set(MUSASHI_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/musashi")

# Keep m68kcpu.c beside the generated m68kops.h so its quoted include
# cannot pick up a stale header from a Make build in the Musashi checkout.
configure_file(
    "${MUSASHI_DIR}/m68kcpu.c"
    "${MUSASHI_GENERATED_DIR}/m68kcpu.c"
    COPYONLY
)

add_executable(m68kmake "${MUSASHI_DIR}/m68kmake.c")

add_custom_command(
    OUTPUT
        "${MUSASHI_GENERATED_DIR}/m68kops.c"
        "${MUSASHI_GENERATED_DIR}/m68kops.h"
    COMMAND m68kmake "${MUSASHI_GENERATED_DIR}" "${MUSASHI_DIR}/m68k_in.c"
    DEPENDS m68kmake "${MUSASHI_DIR}/m68k_in.c"
    COMMENT "Generating Musashi opcode handlers"
    VERBATIM
)

add_executable(${PROJECT_NAME}
    src/disc.c
    src/main.c
    src/rom.c
    src/scc.c
    src/unix_main.c
    src/via.c

    "${MUSASHI_GENERATED_DIR}/m68kcpu.c"
    "${MUSASHI_DIR}/m68kdasm.c"
    "${MUSASHI_GENERATED_DIR}/m68kops.c"
    "${MUSASHI_GENERATED_DIR}/m68kops.h"
    "${MUSASHI_DIR}/softfloat/softfloat.c"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${MUSASHI_GENERATED_DIR}"
    "${MUSASHI_DIR}"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    SIM
    ENABLE_DASM=1
    "MUSASHI_CNF=\"${CMAKE_CURRENT_SOURCE_DIR}/include/m68kconf.h\""
    "UMAC_MEMSIZE=${MEMSIZE}"
    "DISP_WIDTH=${DISP_WIDTH}"
    "DISP_HEIGHT=${DISP_HEIGHT}"
    #"$<$<CONFIG:Debug>:DEBUG>"
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic "$<$<CONFIG:Debug>:-Og>")

target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2 m)
