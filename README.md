# Whisper Agent

A C++ desktop application that combines a file browser, code editor, and embedded terminal with voice-to-command capabilities. Record audio, transcribe it with [whisper.cpp](https://github.com/ggerganov/whisper.cpp), and send the transcribed text directly to the integrated terminal running a CLI agent.

## Features

- **Embedded terminal** — a fully functional terminal emulator (powered by [libvterm](https://github.com/neovim/libvterm)) with scrollback, scrollbar, and PTY support
- **File tree** — browse project files with single-click preview
- **Code editor** — syntax-highlighted file viewer using wxStyledTextCtrl with word wrap
- **Voice dictation** — press Record, speak, and the transcribed command is sent to the terminal
- **Live transcription** — partial results stream into an overlay dialog as you speak; press Enter to send immediately or Esc to edit before sending

## Dependencies

All C/C++ dependencies are fetched automatically via CMake `FetchContent`:

| Library | Version | Purpose |
|---------|---------|---------|
| [wxWidgets](https://github.com/wxWidgets/wxWidgets) | 3.2.6 | GUI framework |
| [whisper.cpp](https://github.com/ggerganov/whisper.cpp) | 1.5.5 | Speech-to-text |
| [miniaudio](https://github.com/mackron/miniaudio) | 0.11.21 | Audio capture |
| [libvterm](https://github.com/neovim/libvterm) | 0.3.3 | Terminal emulation |

The Whisper model (`ggml-base.en.bin`, ~140 MB) is downloaded automatically during CMake configure.

### System requirements

- C++17 compiler (GCC or Clang)
- CMake 3.20+
- Perl (for building libvterm encoding tables)
- GTK3 development headers (for wxWidgets on Linux): `sudo apt install libgtk-3-dev`

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Run

```bash
./build/whisper-agent
```

By default the terminal runs `agent` (the Cursor CLI). You can change this at configure time:

```bash
cmake -B build -DWHISPER_AGENT_DEFAULT_COMMAND=bash
```

## Usage

1. Browse files in the left panel; click to preview in the editor
2. Use the terminal at the bottom-right to interact with the agent
3. Press **Record** to start voice dictation
4. Speak your command — partial transcription appears in real time
5. Press **Enter** to send immediately, or **Esc** to stop recording and edit before sending
6. Press **Cancel** to discard

## License

MIT
