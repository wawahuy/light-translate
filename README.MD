# Light Translate

[![Build Status](https://github.com/wawahuy/light-translate/actions/workflows/build-and-release.yml/badge.svg)](https://github.com/wawahuy/light-translate/actions)
[![Latest Release](https://img.shields.io/github/v/release/wawahuy/light-translate)](https://github.com/wawahuy/light-translate/releases)
[![Downloads](https://img.shields.io/github/downloads/wawahuy/light-translate/total)](https://github.com/wawahuy/light-translate/releases)
[![License](https://img.shields.io/github/license/wawahuy/light-translate)](https://github.com/wawahuy/light-translate/blob/main/LICENSE)

Light Translate is a lightweight, high-performance on-screen text capture and translation desktop application for Windows. It allows you to select a region of the screen, capture text using built-in OCR engines, and display real-time translations instantly with minimal CPU overhead.

![Demo](demo/demo.gif)

## Features

- OCR Engines:
  - Windows OCR: Uses the built-in Windows 10/11 UWP OCR engine. High performance, works completely offline, and requires no external model downloads.
  - PaddleOCR: Uses PaddleOCR's C++ inference library with the PP-OCRv5 mobile models for high-quality, offline text detection and recognition.

- Translation Providers:
  - Google Translate: Powered by the public Google Translate API. Free to use, works out of the box, and does not require any API keys.
  - DeepSeek Translate: Integrates with the DeepSeek Chat API. Allows customization of the API URL endpoint, API key, and model parameters.

- Display Modes:
  - Overlay: Shows translated text in a dedicated, resizable overlay window.
  - In-Place: Displays translated text boxes directly over the original screen text with solid background options.

- Capture Modes:
  - Auto Capture: Automatically captures and translates the selected region at a configurable millisecond interval (default is 1000ms).
  - Hotkey Capture: Performs capture and translation only when the user presses the designated hotkey.

- Configurations:
  - Screen ROI scaling and multi-monitor indexing.
  - Typography settings: adjust font family, font size, colors, text stroke, and shadow.
  - Idle state detection: automatically pauses translation when no text boxes change in the selected region.
  - Settings persistence: settings are stored locally in a standard INI configuration file.

## Hotkeys

Default global hotkey controls:
- Select Screen Region: Ctrl + M
- Toggle Settings Window: Ctrl + Shift + O
- Pause/Resume Auto Capture: Ctrl + P
- Trigger Manual Capture (Hotkey Mode): Ctrl + N

## Quick Start

### Prerequisites

| Dependency | Version | Notes |
|---|---|---|
| Windows SDK | Windows 10/11 | Required for built-in Windows OCR (C++/WinRT) |
| Paddle Inference | 3.0 | Required for offline PaddleOCR |
| OpenCV | 4.7 | Built with opencv_world recommended |
| CMake | 3.31+ | Build system generator |
| C++ Compiler | C++17 | MSVC 2022 recommended |

Tip: Enable WITH_MKL for Intel CPUs to optimize PaddleOCR inference speed. Set WITH_MKL=OFF to use OpenBLAS on non-Intel systems.

## Configuration Options

Key CMake configuration options:

| Option | Default | Description |
|---|---|---|
| WITH_MKL | ON | Use Intel MKL for math acceleration |
| WITH_STATIC_LIB | ON | Statically link Paddle Inference library |
| USE_FREETYPE | OFF | Enable OpenCV FreeType for text rendering |
| BUILD_SHARED_LIBS | ON | Build project shared libraries |

## Dependencies

- Paddle Inference 3.0 (high-performance inference engine for PaddleOCR)
- OpenCV 4.7 (image preprocessing, capture, and rendering)
- Abseil C++ (utilities and absl::StatusOr helper)
- Clipper (polygon clipping for text region masking)
- nlohmann/json (JSON serialization for translation APIs)
- Intel MKL (optional math library for CPU acceleration)

## Contributing

Contributions are welcome. Please follow these guidelines:
1. Fork the repository and create a feature branch.
2. Keep the coding style consistent (C++17, Google-style naming).
3. Ensure the project builds successfully with both MKL and non-MKL flags.
4. Submit a pull request with a descriptive summary of your changes.

## License

This project is licensed under the Apache License, Version 2.0. See LICENSE for details.

PaddleOCR is licensed under the Apache License 2.0. Refer to the PaddleOCR repository for its licensing terms.