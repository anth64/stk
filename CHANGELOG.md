# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.3] - 2026-02-25

### Added
- **Module Metadata**: Optional metadata support for modules
  - `stk_mod_name` - human-readable module name
  - `stk_mod_version` - semantic version string
  - `stk_mod_description` - module description
  - All three are optional, missing symbols are not errors
  - Metadata stored in tight-packed arrays with index mappings
  - Setter functions to override default symbol names before `stk_init()`

## [0.1.2] - 2026-02-15

### Fixed
- **Windows**
    - Module reload now works correctly, idk how it was broken
    - Temp directory creation fixed
    - `.tmp` directory now hidden via FILE_ATTRIBUTE_HIDDEN
    - Test build fixed
        - Force cmd.exe shell in gmake.mk
        - Fix bash syntax errors when running build.bat
- **All Platforms**: platform_mkdir now checks if directory exists before creating

## [0.1.1] - 2026-02-14

### Fixed
- **Logging**: Corrected log level severity order in enum
  - Reversed order so DEBUG (0) < INFO (1) < WARN (2) < ERROR (3)
  - Fixes filtering logic where ERROR/WARN were incorrectly blocked
  - Default INFO level now properly shows INFO, WARN, and ERROR while filtering DEBUG

## [0.1.0] - 2026-02-14

### Fixed
- **C89 Compliance**: Removed stdint.h dependency (C99 feature)
  - Replaced all uint8_t with unsigned char throughout codebase
  - Ensures strict C89 compliance for maximum portability

### Added
- **Flags system**: Centralized bitfield for boolean state and settings
  - Replaces stk_initialized with stk_flags for efficient memory usage
  - Single byte packs all boolean flags (STK_FLAG_INITIALIZED, STK_FLAG_LOGGING_ENABLED)
  - Runtime-changeable logging control via stk_set_logging_enabled()

- **Enhanced logging system**: Complete rewrite with modern features
  - Log levels: ERROR, WARN, INFO, DEBUG with runtime filtering
  - Timestamps: yyyy-mm-dd HH:MM:SS.mmm format (platform-specific implementation)
  - Configurable log output stream (stk_set_log_output)
  - Configurable log prefix (stk_set_log_prefix, defaults to "stk")
  - Configurable minimum log level (stk_set_log_level, defaults to INFO)
  - Platform abstraction for timestamps (GetLocalTime on Windows, gettimeofday on POSIX)

### Changed
- **BREAKING**: stk_log() signature changed from stk_log(FILE *fp, ...) to stk_log(stk_log_level_t level, ...)
- **BREAKING**: All uint8_t types replaced with unsigned char
- All internal STK messages now use appropriate log levels
- Setting log output to NULL disabls logging

### Notes
- This release completes Phase 1 logging improvements
- Dependency management still in progress for Phase 1 completion

## [0.0.4] - 2026-02-11

### Fixed
- **Linux**: Fixed segfault from invalid module indices during extremely rapid file changes
  - Added validation check to skip stale UNLOAD/RELOAD events for already-unloaded modules
  - Prevents is_mod_loaded() returning -1 from being used as array index (SIZE_MAX)
  - Fixed event count mismatch where loops would run more iterations than valid indices populated
  - Completes the Linux hot-reload stability fixes from v0.0.2

## [0.0.3] - 2026-02-10

### Fixed
- **Compilation**: Fixed GCC `-Wrestrict` warning in Linux directory watching code by replacing `strncpy` with `memmove` for overlapping memory operations
  - Ensures clean compilation without warnings while maintaining identical runtime behavior
  - Uses semantically correct function for moving memory within the same buffer

## [0.0.2] - 2026-02-09

### Fixed
- **Linux**: Fixed segfaults during rapid module reloads when file changes are detected in quick succession
  - Enabled file readiness checks on Linux (previously only used on Windows/BSD) to prevent loading partially-written shared libraries
  - Fixed inotify event deduplication to actually remove duplicate events instead of just marking them
  - Reordered reload operations to only unload old module after successfully copying new version, preventing invalid state when copy fails
- **All platforms**: Improved reload safety by deferring module unload until after successful file copy

### Changed
- Made `is_file_ready()` check available on all Unix platforms (was previously excluded on Linux)

## [0.0.1] - 2026-02-01

### Added
- Cross-platform dynamic module loading (Linux, BSD, Windows)
- Hot-reloading of native modules at runtime
- File watching for automatic module reloads (inotify/kqueue/FindFirstFile)
- Basic module metadata support
- Basic logging infrastructure (stdout/stderr)
- Error handling for failed module loads
- Robust filename handling with proper buffer bounds checking
- Install/uninstall targets for Unix systems
- Test suite with cross-platform support

### Notes
- Phase 1 in progress: logging system incomplete (no log levels, verbosity control, or output configurability)
- Dependency management and versioning not yet implemented
- API is unstable and subject to change in future releases

[Unreleased]: https://github.com/anth64/stk/compare/v0.1.2...HEAD
[0.1.2]: https://github.com/anth64/stk/releases/tag/v0.1.2
[0.1.1]: https://github.com/anth64/stk/releases/tag/v0.1.1
[0.1.1]: https://github.com/anth64/stk/releases/tag/v0.1.1
[0.1.0]: https://github.com/anth64/stk/releases/tag/v0.1.0
[0.0.4]: https://github.com/anth64/stk/releases/tag/v0.0.4
[0.0.3]: https://github.com/anth64/stk/releases/tag/v0.0.3
[0.0.2]: https://github.com/anth64/stk/releases/tag/v0.0.2
[0.0.1]: https://github.com/anth64/stk/releases/tag/v0.0.1
