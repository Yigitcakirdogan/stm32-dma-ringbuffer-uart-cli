# Contributing to This Project

Thank you for your interest in contributing! We welcome pull requests from embedded systems engineers, firmware developers, and students worldwide.

## Engineering & Coding Standards

To keep the codebase deterministic, portable, and production-ready, please follow these guidelines:

1. **Zero Dynamic Memory Allocation:** Do not use `malloc()`, `calloc()`, `realloc()`, or `free()` (MISRA C:2012 Rule 21.3). All buffers and state structures must be statically or caller-allocated.
2. **Fixed-Width Types:** Use `<stdint.h>` types (`uint8_t`, `uint16_t`, `uint32_t`, `int32_t`, `size_t`, `bool`) or AUTOSAR standard types (`Std_Types.h`).
3. **Strict Compiler Warnings:** All code must compile cleanly with `-Wall -Wextra -Werror -Wpedantic -Wshadow`.
4. **Hardware Decoupling:** Keep core protocol, DSP, and safety logic decoupled from physical MCU registers via header interfaces so host-based unit tests can verify every change.

## How to Build & Run Unit Tests Locally

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure --verbose
```

## Submitting a Pull Request

1. Check the **[Issues](../../issues)** tab for open tasks labeled `good first issue` or `help wanted`.
2. Fork the repository and create a feature branch (`git checkout -b feat/your-feature`).
3. Add or update unit tests in `tests/` covering your feature or bugfix.
4. Open a Pull Request and ensure the **GitHub Actions CI** workflow passes!
