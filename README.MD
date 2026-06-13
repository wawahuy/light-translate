# Light Translate

> **A lightweight, high-performance on-screen text capture & translation tool powered by PaddleOCR.**

Light Translate is a desktop utility that captures text from your screen via region selection, performs optical character recognition (OCR) using PaddleOCR's C++ inference engine, and delivers instant translations — all with minimal CPU overhead and blazing-fast inference.

---

## Quick Start

### Prerequisites

| Dependency | Version | Notes |
|-----------|---------|-------|
| [Paddle Inference](https://paddleinference.paddlepaddle.org.cn/) | 3.0 | C++ prediction library |
| [OpenCV](https://opencv.org/) | 4.7 | Built with `opencv_world` recommended |
| [CMake](https://cmake.org/) | 3.31+ | Build system |
| C++ Compiler | C++11 | MSVC 2022 |

> **Tip**: Enable `WITH_MKL` for Intel CPUs to get the best inference speed. Set `WITH_MKL=OFF` to fall back to OpenBLAS on non-Intel systems.

---

## Configuration

Key CMake options:

| Option | Default | Description |
|--------|---------|-------------|
| `WITH_MKL` | `ON` | Use Intel MKL for math acceleration |
| `WITH_STATIC_LIB` | `ON` | Statically link Paddle Inference |
| `USE_FREETYPE` | `OFF` | Enable OpenCV FreeType for text rendering |

---

## Dependencies

| Library | Purpose |
|---------|---------|
| [Paddle Inference 3.0](https://paddleinference.paddlepaddle.org.cn/) | High-performance inference runtime for PaddleOCR models |
| [OpenCV 4.7](https://opencv.org/) | Image processing, screen capture, and visualization |
| [Abseil](https://abseil.io/) | C++ standard library extensions (`absl::StatusOr`) |
| [Clipper](http://www.angusj.com/delphi/clipper.php) | Polygon clipping for accurate text region masking |
| [nlohmann/json](https://json.nlohmann.me/) | JSON serialization for API communication |
| [Intel MKL](https://www.intel.com/content/www/us/en/developer/tools/oneapi/onemkl.html) *(optional)* | Math kernel library for CPU acceleration |

---

## Contributing

Contributions are welcome! Please follow these guidelines:

1. Fork the repository and create a feature branch from `main`.
2. Keep the coding style consistent (C++11, Google-style naming).
3. Ensure the build passes with both MKL and non-MKL configurations.
4. Submit a pull request with a clear description of the changes.

---

## License

This project is licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.

PaddleOCR is licensed under the Apache License 2.0. Refer to the [PaddleOCR repository](https://github.com/PaddlePaddle/PaddleOCR) for its licensing terms.