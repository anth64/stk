# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0-pre.7] - 2026-03-07

### Fixed
- `stk_module_load()`: added missing dependency failure logging when a module is deferred to the pending queue.

## [1.0.0-pre.6] - 2026-03-07

### Changed
- `stk_poll()`: replaced per-module `stk_pending_add()` calls inside the load holes and `append_modules` loops with batch collection followed by a single `stk_pending_add_batch()` call after both loops complete.

## [1.0.0-pre.5] - 2026-03-07

### Changed
- `stk_init()`: replaced per-module `stk_pending_add()` calls inside the deferred module loop with batch collection followed by a single `stk_pending_add_batch()` call after the loop.

## [1.0.0-pre.4] - 2026-03-07

### Changed
- `stk_pending_retry()`: replaced per-module `realloc` at `attempt_load` with a single bulk allocation upfront (`module_count + stk_pending_count`) before the retry loop.
- `stk_poll()`: replaced per-module `stk_pending_add()` calls inside the unload and cascade loops with batch collection followed by a single `stk_pending_add_batch()` call after each loop.

## [1.0.0-pre.3] - 2026-03-07

### Added
- `STK_MOD_DEP_LOG_BUFFER` (2048) added to `stk.h`

### Changed
- Dependency failure logging now emits a single line per module listing all unmet deps with their reason: `not found` or `requires <constraint>, have <version>`
  - `Deferring 'test_mod_dep': unmet deps: test_mod (not found)`
  - `Unloading 'test_mod_dep': unmet deps: test_mod (not found), renderer (requires ^2.0.0, have 1.3.0)`
- Silent defer at `stk_init` and vague `"unmet dependencies"` cascade log in `stk_poll` replaced with `stk_log_dependency_failures()`
- Kahn topological sort refactored into generic `stk_kahn_sort()` accepting a `has_dep` callback and an `on_cycle` callback, decoupling it from `stk_modules`. `stk_topo_sort()` is now a thin wrapper. `stk_sort_load_order()` uses the same core via `stk_batch_has_dep()`, inspecting tmp files so simultaneous load events are processed dependency-first without a retry cycle
- `stk_module_load()` split into `stk_module_preload()`, `stk_module_activate()`, `stk_module_discard()`, and `stk_validate_dependencies_single()`. Init is not called if deps are unmet
- On UNLOAD events in `stk_poll()`, the unload set is expanded to include all transitively dependent modules via `stk_collect_dependents()`, sorted dependents-first via `stk_sort_unload_order()`. Modules unloaded due to expansion are queued to pending
- On LOAD events with unmet dependencies, the tmp path is added to the pending queue instead of being dropped
- `stk_pending_retry()` now skips already-loaded entries and prunes entries whose file no longer exists
- `free_poll` label moved above `stk_pending_retry()` so retry always runs regardless of exit path

## [1.0.0-pre.2] - 2026-03-06

### Fixed
- Module `init` is no longer called if dependencies are not satisfied. The library handle is unloaded and a dep error is returned. 

## [1.0.0-pre.1] - 2026-03-06

### Added
- **Dependency System**: Full module dependency declaration and resolution
  - Modules declare dependencies via an exported `stk_mod_deps` symbol, no stk headers required in modules, only the memory layout contract must be respected (`{ char[64], char[32] }` sentinel-terminated array)
  - Version constraint operators: `=` (exact), `>=` (minimum, default), `^` (compatible — same major, greater or equal minor/patch)
  - Version defaults to `0.0.0` if not provided or unparseable
  - Dependency validation on `stk_init` and every `stk_poll` cycle
  - Topological sort with cycle detection, load and unload ordering enforced
  - All dependency failures logged before returning
- **Cascade Unload**: When a module is removed, all dependents cascade unload automatically and are added to the pending queue. Cascades repeat until the loaded set is stable
- **Pending Queue**: Modules that fail dependency validation are deferred. When their dependencies become available they are retried and loaded automatically. Entries are removed if their file is deleted

### Changed
- `stk_set_module_dependencies_fn` renamed to `stk_set_module_deps_sym`. Deps are now an exported array symbol, not a function
- Default deps symbol name changed from `stk_mod_dependencies` to `stk_mod_deps`
- `stk_dep_t` added to public header `stk.h`
- `stk_mod_t` field order changed to largest-to-smallest for correct memory alignment with zero padding waste
- Dependency validation failure during `stk_init` no longer fatal, affected modules are deferred to the pending queue
- Poll cycle now logs the full loaded module list as a single summary after any event that changes the loaded set, instead of logging each module individually

### Notes
- This release marks Phase 1 complete: hot-reloading foundation with dependency management
- Memory layout contract for `stk_mod_deps`: sentinel-terminated array of `{ char[64], char[32] }`. Modules do not need to include `stk.h`, define the struct inline or via your engine's own header as long as the layout matches

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

[Unreleased]: https://github.com/anth64/stk/compare/v1.0.0-pre.7...HEAD
[1.0.0-pre.7]: https://github.com/anth64/stk/compare/v1.0.0-pre.6...v1.0.0-pre.7
[1.0.0-pre.6]: https://github.com/anth64/stk/compare/v1.0.0-pre.5...v1.0.0-pre.6
[1.0.0-pre.5]: https://github.com/anth64/stk/compare/v1.0.0-pre.4...v1.0.0-pre.5
[1.0.0-pre.4]: https://github.com/anth64/stk/compare/v1.0.0-pre.3...v1.0.0-pre.4
[1.0.0-pre.3]: https://github.com/anth64/stk/compare/v1.0.0-pre.2...v1.0.0-pre.3
[1.0.0-pre.2]: https://github.com/anth64/stk/compare/v1.0.0-pre.1...v1.0.0-pre.2
[1.0.0-pre.1]: https://github.com/anth64/stk/compare/v0.1.3...v1.0.0-pre.1
[0.1.3]: https://github.com/anth64/stk/releases/tag/v0.1.3
[0.1.2]: https://github.com/anth64/stk/releases/tag/v0.1.2
[0.1.1]: https://github.com/anth64/stk/releases/tag/v0.1.1
[0.1.1]: https://github.com/anth64/stk/releases/tag/v0.1.1
[0.1.0]: https://github.com/anth64/stk/releases/tag/v0.1.0
[0.0.4]: https://github.com/anth64/stk/releases/tag/v0.0.4
[0.0.3]: https://github.com/anth64/stk/releases/tag/v0.0.3
[0.0.2]: https://github.com/anth64/stk/releases/tag/v0.0.2
[0.0.1]: https://github.com/anth64/stk/releases/tag/v0.0.1
