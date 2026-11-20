file(MAKE_DIRECTORY "${TEST_DIR}")
set(rom "${TEST_DIR}/fixture.rom")
set(floppy "${TEST_DIR}/fixture.dsk")
set(ram "${TEST_DIR}/ram.bin")
file(WRITE "${rom}" "ROM fixture bytes\n")
file(WRITE "${floppy}" "Floppy fixture bytes\n")
file(SHA256 "${rom}" rom_before)
file(SHA256 "${floppy}" floppy_before)

function(expect_rejection diagnostic)
    execute_process(
        COMMAND "${UMAC}" -r "${rom}" -d "${floppy}" ${ARGN}
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
expect_rejection("read-only IWM" -w)
expect_rejection("conflicts with media/output" -W "${rom}")
expect_rejection("conflicts with media/output" -W "${floppy}")
file(REMOVE "${ram}")
file(CREATE_LINK "${floppy}" "${ram}")
expect_rejection("conflicts with media/output")
file(REMOVE "${ram}")
