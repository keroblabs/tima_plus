cmake_minimum_required(VERSION 3.16)
set(PROJECT_NAME umac)
project(${PROJECT_NAME} LANGUAGES C)

include(CTest)

set(MEMSIZE "4096" CACHE STRING "Emulated RAM size in KiB")
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

add_library(umac_core STATIC
    src/disc.c
    src/main.c
    src/rom.c
    src/scc.c
    src/scsi5380.c
    src/scsi_disk.c
    src/scsi_image.c
    src/via.c

    "${MUSASHI_GENERATED_DIR}/m68kcpu.c"
    "${MUSASHI_DIR}/m68kdasm.c"
    "${MUSASHI_GENERATED_DIR}/m68kops.c"
    "${MUSASHI_GENERATED_DIR}/m68kops.h"
    "${MUSASHI_DIR}/softfloat/softfloat.c"
)

target_include_directories(umac_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${MUSASHI_DIR}"
    PRIVATE
    "${MUSASHI_GENERATED_DIR}"
)

target_compile_definitions(umac_core PRIVATE
    SIM
    ENABLE_DASM=1
    "MUSASHI_CNF=\"${CMAKE_CURRENT_SOURCE_DIR}/include/m68kconf.h\""
    PUBLIC
    "UMAC_MEMSIZE=${MEMSIZE}"
    "DISP_WIDTH=${DISP_WIDTH}"
    "DISP_HEIGHT=${DISP_HEIGHT}"
    #"$<$<CONFIG:Debug>:DEBUG>"
)

target_compile_options(umac_core PRIVATE -Wall -Wextra -pedantic "$<$<CONFIG:Debug>:-Og>")
target_link_libraries(umac_core PRIVATE m)

add_executable(${PROJECT_NAME} src/unix_main.c)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic "$<$<CONFIG:Debug>:-Og>")
target_link_libraries(${PROJECT_NAME} PRIVATE umac_core SDL2::SDL2)

if(BUILD_TESTING)
    add_executable(umac_scsi_test tests/test_scsi.c)
    target_compile_options(umac_scsi_test PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(umac_scsi_test PRIVATE umac_core)
    add_test(NAME scsi_controller COMMAND umac_scsi_test controller)
    add_test(NAME scsi_disk COMMAND umac_scsi_test disk)
    set(UMAC_TEST_PLUS_ROM "" CACHE FILEPATH "User-supplied Mac Plus V3 ROM for the SCSI Manager test")
    if(UMAC_TEST_PLUS_ROM)
        add_test(NAME scsi_plus_rom COMMAND umac_scsi_test rom "${UMAC_TEST_PLUS_ROM}")
    endif()
endif()
