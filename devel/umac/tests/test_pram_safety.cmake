if(NOT DEFINED UMAC OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "UMAC and TEST_DIR are required")
endif()

file(MAKE_DIRECTORY "${TEST_DIR}")

# Passing the ROM itself as -P must fail before the front-end opens or
# changes the file, even when it is not a valid emulator ROM fixture.
set(rom_path "${TEST_DIR}/fixture.rom")
file(WRITE "${rom_path}" "ROM fixture bytes\n")
file(SHA256 "${rom_path}" rom_before)
execute_process(
    COMMAND "${UMAC}" -r "${rom_path}" -P "${rom_path}"
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE rom_result
    OUTPUT_VARIABLE rom_stdout
    ERROR_VARIABLE rom_stderr)
if(rom_result EQUAL 0)
    message(FATAL_ERROR "uMac accepted its ROM path as the PRAM destination")
endif()
string(FIND "${rom_stdout}${rom_stderr}" "conflicts with media/output" conflict_pos)
if(conflict_pos EQUAL -1)
    message(FATAL_ERROR "ROM path rejection was not actionable: ${rom_stdout}${rom_stderr}")
endif()
file(SHA256 "${rom_path}" rom_after)
if(NOT rom_before STREQUAL rom_after)
    message(FATAL_ERROR "ROM fixture changed after PRAM path rejection")
endif()

# A hard-link alias has a different pathname but still names the ROM inode.
set(rom_hardlink "${TEST_DIR}/rom-hardlink.bin")
file(CREATE_LINK "${rom_path}" "${rom_hardlink}" RESULT hardlink_result)
if(NOT hardlink_result STREQUAL "0")
    message(FATAL_ERROR "Could not create ROM hard-link fixture: ${hardlink_result}")
endif()
execute_process(
    COMMAND "${UMAC}" -r "${rom_path}" -P "${rom_hardlink}"
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE hardlink_result
    OUTPUT_VARIABLE hardlink_stdout
    ERROR_VARIABLE hardlink_stderr)
if(hardlink_result EQUAL 0)
    message(FATAL_ERROR "uMac accepted a hard-link alias of the ROM as PRAM")
endif()
string(FIND "${hardlink_stdout}${hardlink_stderr}" "conflicts with media/output" hardlink_conflict_pos)
if(hardlink_conflict_pos EQUAL -1)
    message(FATAL_ERROR "ROM hard-link rejection was not actionable: ${hardlink_stdout}${hardlink_stderr}")
endif()
file(SHA256 "${rom_path}" rom_after_hardlink)
if(NOT rom_before STREQUAL rom_after_hardlink)
    message(FATAL_ERROR "ROM fixture changed after hard-link PRAM rejection")
endif()

# The default RAM output is protected too, including an existing file that
# would otherwise be truncated during startup.
set(ram_path "${TEST_DIR}/ram.bin")
file(WRITE "${ram_path}" "RAM fixture bytes\n")
file(SHA256 "${ram_path}" ram_before)
execute_process(
    COMMAND "${UMAC}" -P "${ram_path}"
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE ram_result
    OUTPUT_VARIABLE ram_stdout
    ERROR_VARIABLE ram_stderr)
if(ram_result EQUAL 0)
    message(FATAL_ERROR "uMac accepted its RAM output path as the PRAM destination")
endif()
string(FIND "${ram_stdout}${ram_stderr}" "conflicts with media/output" ram_conflict_pos)
if(ram_conflict_pos EQUAL -1)
    message(FATAL_ERROR "RAM path rejection was not actionable: ${ram_stdout}${ram_stderr}")
endif()
file(SHA256 "${ram_path}" ram_after)
if(NOT ram_before STREQUAL ram_after)
    message(FATAL_ERROR "RAM fixture changed after PRAM path rejection")
endif()

# Existing malformed state must be rejected before emulator startup and
# must not be replaced by a default PRAM file on exit.
set(bad_pram "${TEST_DIR}/malformed.pram")
file(WRITE "${bad_pram}" "malformed PRAM fixture\n")
file(SHA256 "${bad_pram}" pram_before)
execute_process(
    COMMAND "${UMAC}" -P "${bad_pram}"
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE pram_result
    OUTPUT_VARIABLE pram_stdout
    ERROR_VARIABLE pram_stderr)
if(pram_result EQUAL 0)
    message(FATAL_ERROR "uMac accepted an existing malformed PRAM file")
endif()
string(FIND "${pram_stdout}${pram_stderr}" "invalid or inaccessible" invalid_pos)
if(invalid_pos EQUAL -1)
    message(FATAL_ERROR "Malformed PRAM rejection was not actionable: ${pram_stdout}${pram_stderr}")
endif()
file(SHA256 "${bad_pram}" pram_after)
if(NOT pram_before STREQUAL pram_after)
    message(FATAL_ERROR "Malformed PRAM fixture changed after rejection")
endif()
