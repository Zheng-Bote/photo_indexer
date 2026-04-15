# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.0] - 2026-04-15

### Added
- SIMD optimization via AVX2 and FMA support.
- xxHash integration (XXH3-64bits) for ultra-fast string fingerprints and index generation.
- Complete `bench.sh` benchmarking script for testing with and without AVX2.
- Doxygen style comments and SPDX license headers to C++ components.

### Changed
- Standardized documentation in `README.md` and architecture endpoints in `docs/`.
- Updated C++ structure with inline utility definitions and fingerprint mappings.

## [0.1.0] - 2026-04-14

### Added
- Initial project structure for `photo_indexer`.
- C++23 based photo metadata extraction using `Exiv2`.
- FlatBuffers schema for metadata and indexes.
- Binary index generation for ISO, Date, Camera, and Tags.
- CMake build system with Conan dependency management.
- Initial project documentation (README, NOTICE, CHANGELOG).
