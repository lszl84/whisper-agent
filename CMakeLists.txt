cmake_minimum_required(VERSION 3.20)
project(whisper-agent LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# ============================================================================
# Dependencies
# ============================================================================

# --- wxWidgets ---
set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(wxWidgets
    GIT_REPOSITORY https://github.com/wxWidgets/wxWidgets.git
    GIT_TAG        v3.2.6
    GIT_SHALLOW    TRUE
)

# --- whisper.cpp ---
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(whisper
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG        v1.5.5
    GIT_SHALLOW    TRUE
)

# --- miniaudio (header-only) ---
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
)

# --- libvterm ---
FetchContent_Declare(libvterm
    GIT_REPOSITORY https://github.com/neovim/libvterm.git
    GIT_TAG        v0.3.3
    GIT_SHALLOW    TRUE
)

message(STATUS "Fetching wxWidgets (this may take a while)...")
FetchContent_MakeAvailable(wxWidgets)

message(STATUS "Fetching whisper.cpp...")
FetchContent_MakeAvailable(whisper)

message(STATUS "Fetching miniaudio...")
FetchContent_Populate(miniaudio)

message(STATUS "Fetching libvterm...")
FetchContent_Populate(libvterm)

# Build libvterm as a static library
include(cmake/BuildLibvterm.cmake)

# ============================================================================
# Whisper model (downloaded at configure time if missing)
# ============================================================================

set(WHISPER_MODEL_DIR "${CMAKE_BINARY_DIR}/models" CACHE PATH "Whisper model directory")
set(WHISPER_MODEL_NAME "ggml-base.en.bin" CACHE STRING "Whisper model filename")
set(WHISPER_MODEL_PATH "${WHISPER_MODEL_DIR}/${WHISPER_MODEL_NAME}")

if(NOT EXISTS "${WHISPER_MODEL_PATH}")
    message(STATUS "Downloading whisper model: ${WHISPER_MODEL_NAME} (~140 MB)...")
    file(MAKE_DIRECTORY "${WHISPER_MODEL_DIR}")
    file(DOWNLOAD
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${WHISPER_MODEL_NAME}"
        "${WHISPER_MODEL_PATH}"
        SHOW_PROGRESS
        STATUS DOWNLOAD_STATUS
    )
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        message(WARNING
            "Failed to download whisper model.\n"
            "Download manually to: ${WHISPER_MODEL_PATH}\n"
            "URL: https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${WHISPER_MODEL_NAME}")
    endif()
endif()

# ============================================================================
# Main executable
# ============================================================================

set(WHISPER_AGENT_DEFAULT_COMMAND "agent" CACHE STRING "Default command to run in the terminal")

add_executable(whisper-agent
    src/main.cpp
    src/main_frame.cpp
    src/terminal_panel.cpp
    src/file_tree_panel.cpp
    src/editor_panel.cpp
    src/transcriber.cpp
)

target_include_directories(whisper-agent PRIVATE
    ${miniaudio_SOURCE_DIR}
    src
)

target_compile_definitions(whisper-agent PRIVATE
    WHISPER_MODEL_PATH="${WHISPER_MODEL_PATH}"
    WHISPER_AGENT_DEFAULT_COMMAND="${WHISPER_AGENT_DEFAULT_COMMAND}"
)

target_link_libraries(whisper-agent PRIVATE
    wx::core
    wx::base
    wx::stc
    vterm
    whisper
    util
    pthread
)
