file(MAKE_DIRECTORY "${TEST_DIR}")
set(rom "${TEST_DIR}/fixture.rom")
set(floppy "${TEST_DIR}/fixture.dsk")
set(ram "${TEST_DIR}/ram.bin")
file(WRITE "${rom}" "ROM fixture bytes\n")
file(WRITE "${floppy}" "Floppy fixture bytes\n")
file(SHA256 "${rom}" rom_before)
file(SHA256 "${floppy}" floppy_before)

function(expect_rejection floppy_option diagnostic)
    execute_process(
        COMMAND "${UMAC}" -r "${rom}" "${floppy_option}" "${floppy}" ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(result EQUAL 0 OR NOT "${stderr}" MATCHES "${diagnostic}")
        message(FATAL_ERROR "Expected '${diagnostic}': ${stdout}${stderr}")
    endif()
    file(SHA256 "${rom}" rom_after)
    file(SHA256 "${floppy}" floppy_after)
    if(NOT rom_before STREQUAL rom_after OR NOT floppy_before STREQUAL floppy_after)
        message(FATAL_ERROR "Floppy or ROM bytes changed")
    endif()
endfunction()

# The checks must run before startup even with invalid short fixtures.
execute_process(COMMAND "${UMAC}" -w WORKING_DIRECTORY "${TEST_DIR}"
                RESULT_VARIABLE no_floppy ERROR_VARIABLE no_floppy_error)
if(no_floppy EQUAL 0 OR NOT no_floppy_error MATCHES "-w requires a floppy image")
    message(FATAL_ERROR "Expected -w to require a floppy: ${no_floppy_error}")
endif()
execute_process(COMMAND "${UMAC}" -w -i "${floppy}" WORKING_DIRECTORY "${TEST_DIR}"
                RESULT_VARIABLE trace_only ERROR_VARIABLE trace_error)
if(trace_only EQUAL 0 OR NOT trace_error MATCHES "-w requires a floppy image")
    message(FATAL_ERROR "Expected -i to leave the floppy unassigned: ${trace_error}")
endif()
execute_process(COMMAND "${UMAC}" -a WORKING_DIRECTORY "${TEST_DIR}"
                RESULT_VARIABLE missing_path OUTPUT_VARIABLE missing_path_help
                ERROR_VARIABLE missing_path_error)
if(missing_path EQUAL 0 OR NOT missing_path_help MATCHES "-a <floppy path>")
    message(FATAL_ERROR "Expected -a to require a path: ${missing_path_help}${missing_path_error}")
endif()
file(REMOVE "${ram}")
file(CREATE_LINK "${floppy}" "${ram}")
expect_rejection(-d "conflicts with media/output")
expect_rejection(-a "conflicts with media/output")
file(REMOVE "${ram}")
foreach(option IN ITEMS -d -a)
    expect_rejection("${option}" "conflicts with media/output" -W "${rom}")
    expect_rejection("${option}" "conflicts with media/output" -W "${floppy}")
    expect_rejection("${option}" "conflicts with media/output" -w -s "${floppy}")
    expect_rejection("${option}" "conflicts with media/output" -w -P "${floppy}.iwm.lock")
    expect_rejection("${option}" "conflicts with media/output" -w -W "${floppy}.iwm.lock")
endforeach()
expect_rejection(-d "conflicts with media/output" -iw -s "${floppy}")
