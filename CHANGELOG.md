# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/anth64/stk/compare/v0.0.3...HEAD
[0.0.3]: https://github.com/anth64/stk/releases/tag/v0.0.3
[0.0.2]: https://github.com/anth64/stk/releases/tag/v0.0.2
[0.0.1]: https://github.com/anth64/stk/releases/tag/v0.0.1
