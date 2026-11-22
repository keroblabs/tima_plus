cmake_minimum_required(VERSION 3.16)
set(PROJECT_NAME umac)
project(${PROJECT_NAME} LANGUAGES C)

include(CTest)

set(MEMSIZE "4096" CACHE STRING "Emulated RAM size in KiB")
option(UMAC_DEVELOPMENT_DUART "Build the MC68681 development UART" ON)

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
    src/disk_img.c
    src/iwm.c
    src/main.c
    src/rom.c
    src/rtc.c
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

if(UMAC_DEVELOPMENT_DUART)
    target_sources(umac_core PRIVATE src/m68681_emulator.c)
    target_compile_definitions(umac_core PUBLIC UMAC_DEVELOPMENT_DUART)
endif()

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
    #"$<$<CONFIG:Debug>:DEBUG>"
)

target_compile_options(umac_core PRIVATE -Wall -Wextra -pedantic "$<$<CONFIG:Debug>:-Og>")
target_link_libraries(umac_core PRIVATE m)

add_executable(${PROJECT_NAME} src/unix_main.c)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic "$<$<CONFIG:Debug>:-Og>")
target_link_libraries(${PROJECT_NAME} PRIVATE umac_core SDL2::SDL2)

if(BUILD_TESTING)
    add_executable(umac_iwm_test tests/test_iwm.c)
    target_compile_options(umac_iwm_test PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(umac_iwm_test PRIVATE umac_core)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set(UMAC_TEST_LINK_WRAP ON)
        target_compile_definitions(umac_iwm_test PRIVATE UMAC_TEST_IO_WRAPPERS)
        target_link_options(umac_iwm_test PRIVATE -Wl,--wrap=write,--wrap=fsync,--wrap=rename)
    endif()
    add_test(NAME iwm_controller COMMAND umac_iwm_test controller)
    add_test(NAME iwm_motor_enable COMMAND umac_iwm_test motor)
    add_test(NAME iwm_disk_image COMMAND umac_iwm_test disk)
    add_test(NAME iwm_diskcopy_image COMMAND umac_iwm_test diskcopy)
    add_test(NAME iwm_write_timing COMMAND umac_iwm_test write_timing)
    add_test(NAME iwm_checkpoint COMMAND umac_iwm_test checkpoint)
    add_test(NAME iwm_media_protection
        COMMAND ${CMAKE_COMMAND}
            -DUMAC=$<TARGET_FILE:umac>
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/iwm_media_protection
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_iwm_media.cmake)

    add_executable(umac_scsi_test tests/test_scsi.c)
    target_compile_options(umac_scsi_test PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(umac_scsi_test PRIVATE umac_core)
    add_test(NAME scsi_controller COMMAND umac_scsi_test controller)
    add_test(NAME scsi_disk COMMAND umac_scsi_test disk)

    add_executable(umac_rtc_test tests/test_rtc.c)
    target_compile_options(umac_rtc_test PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(umac_rtc_test PRIVATE umac_core)
    add_test(NAME rtc_core COMMAND umac_rtc_test)

    add_executable(umac_via_rtc_test tests/test_via_rtc.c)
    target_compile_options(umac_via_rtc_test PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(umac_via_rtc_test PRIVATE umac_core)
    add_test(NAME via_rtc COMMAND umac_via_rtc_test)

    add_executable(umac_m68681_test tests/test_m68681.c)
    target_include_directories(umac_m68681_test PRIVATE src)
    target_compile_options(umac_m68681_test PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(umac_m68681_test PRIVATE umac_core)
    if(UMAC_DEVELOPMENT_DUART)
        add_test(NAME m68681_debug_uart COMMAND umac_m68681_test)
    else()
        add_test(NAME m68681_production_map COMMAND umac_m68681_test)
    endif()

    add_test(NAME rtc_pram_safety
        COMMAND ${CMAKE_COMMAND}
            -DUMAC=$<TARGET_FILE:umac>
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/rtc_pram_safety
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pram_safety.cmake)

    set(UMAC_TEST_PLUS_ROM "" CACHE FILEPATH "User-supplied Mac Plus V3 ROM for integration tests")
    set(UMAC_TEST_PLUS_FLOPPY "" CACHE FILEPATH "User-supplied bootable raw or DiskCopy 4.2 800K System Tools floppy")
    set(UMAC_TEST_VASM "" CACHE FILEPATH "vasmm68k_std for assembling the MC68681 boot disk probe")
    if(UMAC_TEST_PLUS_ROM)
        add_test(NAME scsi_plus_rom COMMAND umac_scsi_test rom "${UMAC_TEST_PLUS_ROM}")
        if(UMAC_DEVELOPMENT_DUART AND UMAC_TEST_VASM)
            find_package(Python3 REQUIRED COMPONENTS Interpreter)
            add_test(NAME m68681_plus_rom_disk
                     COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_m68681_disk.py"
                             $<TARGET_FILE:umac> "${UMAC_TEST_PLUS_ROM}" "${UMAC_TEST_VASM}")
            set_tests_properties(m68681_plus_rom_disk PROPERTIES TIMEOUT 25)
        endif()
        if(UMAC_TEST_PLUS_FLOPPY)
            add_executable(umac_iwm_rom_test tests/test_iwm_rom.c)
            target_compile_options(umac_iwm_rom_test PRIVATE -Wall -Wextra -pedantic)
            target_link_libraries(umac_iwm_rom_test PRIVATE umac_core)
            add_test(NAME iwm_plus_rom_boot
                     COMMAND umac_iwm_rom_test "${UMAC_TEST_PLUS_ROM}" "${UMAC_TEST_PLUS_FLOPPY}")
            if(UMAC_TEST_LINK_WRAP)
                target_compile_definitions(umac_iwm_rom_test PRIVATE UMAC_TEST_LINK_WRAP)
                target_link_options(umac_iwm_rom_test PRIVATE -Wl,--wrap=disk_img_sense,--wrap=disk_img_write_bit)
            elseif(EXISTS "${UMAC_TEST_PLUS_FLOPPY}")
                file(SIZE "${UMAC_TEST_PLUS_FLOPPY}" UMAC_TEST_FLOPPY_SIZE)
            endif()
            if(UMAC_TEST_LINK_WRAP OR UMAC_TEST_FLOPPY_SIZE EQUAL 819200 OR UMAC_TEST_FLOPPY_SIZE EQUAL 838484)
                add_test(NAME iwm_plus_rom_write
                         COMMAND umac_iwm_rom_test "${UMAC_TEST_PLUS_ROM}" "${UMAC_TEST_PLUS_FLOPPY}" --write)
                add_test(NAME iwm_plus_rom_format
                         COMMAND umac_iwm_rom_test "${UMAC_TEST_PLUS_ROM}" "${UMAC_TEST_PLUS_FLOPPY}" --format)
            endif()
            if(MEMSIZE EQUAL 4096)
                find_package(Python3 QUIET COMPONENTS Interpreter)
                if(Python3_Interpreter_FOUND)
                    add_test(NAME iwm_desktop_boot
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_iwm_desktop.py"
                                     $<TARGET_FILE:umac> "${UMAC_TEST_PLUS_ROM}" "${UMAC_TEST_PLUS_FLOPPY}")
                    set_tests_properties(iwm_desktop_boot PROPERTIES TIMEOUT 25)
                endif()
            endif()
        endif()
    endif()
endif()
