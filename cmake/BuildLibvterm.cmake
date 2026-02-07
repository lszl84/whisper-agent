# Build libvterm from FetchContent-populated sources.
# Requires Perl to generate encoding tables from .tbl files.

find_program(PERL_EXECUTABLE perl)
if(NOT PERL_EXECUTABLE)
    message(FATAL_ERROR
        "Perl is required to build libvterm encoding tables.\n"
        "Install with: sudo apt install perl")
endif()

set(VTERM_SRC_DIR "${libvterm_SOURCE_DIR}/src")
set(VTERM_INC_DIR "${libvterm_SOURCE_DIR}/include")
set(VTERM_ENC_DIR "${VTERM_SRC_DIR}/encoding")

# Generate encoding lookup tables from .tbl files
foreach(ENC DECdrawing uk)
    if(NOT EXISTS "${VTERM_ENC_DIR}/${ENC}.inc")
        message(STATUS "Generating libvterm encoding table: ${ENC}")
        execute_process(
            COMMAND ${PERL_EXECUTABLE} -CSDA
                "${libvterm_SOURCE_DIR}/tbl2inc_c.pl"
                "${VTERM_ENC_DIR}/${ENC}.tbl"
            OUTPUT_FILE "${VTERM_ENC_DIR}/${ENC}.inc"
            RESULT_VARIABLE GEN_RESULT
            WORKING_DIRECTORY "${libvterm_SOURCE_DIR}"
        )
        if(NOT GEN_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to generate ${ENC}.inc encoding table")
        endif()
    endif()
endforeach()

# Collect all C sources
file(GLOB VTERM_SOURCES "${VTERM_SRC_DIR}/*.c")

add_library(vterm STATIC ${VTERM_SOURCES})

target_include_directories(vterm PUBLIC  "${VTERM_INC_DIR}")
target_include_directories(vterm PRIVATE "${VTERM_SRC_DIR}")

# Suppress warnings in third-party code
target_compile_options(vterm PRIVATE -w)
