# stk (Stalwart Toolkit)

`stk` is a lightweight, modular toolkit for building games and game engines. It provides a **portable foundation** for dynamically loading modules, native or WASM, without enforcing any architecture or design choices.

It is designed to run on modern systems running POSIX and Windows using C89.

---

## Key Features

- **Dynamic module loading** (native `.so` / `.dll` / `.dylib`)
- **Hot-swapping** of modules at runtime
- **Cross-platform** (Linux, BSD, Windows, macOS)
- **Optional WASM support** for multi-language modules (planned)
- **Developer tools**: lightweight metadata, logging/tracing, and dependency management (in progress)
- **Minimal, portable API**

---

## Philosophy

`stk` is **non-opinionated**: developers control architecture, engine design, and game logic while relying on a predictable, lean foundation.

See [docs/design.md](docs/design.md) for the full design philosophy and roadmap.

---

## Quick Start

### Building

```bash
# Unix (Linux/BSD/macOS)
./build.sh debug release

# Windows
build.bat debug release
```

### Installation

#### Unix (Linux/BSD/macOS)
```bash
sudo ./build.sh install
```

Installs to `/usr/local` by default. Use `PREFIX` to customize:
```bash
sudo ./build.sh PREFIX=$HOME/.local install
```

#### Windows
```
build.bat release
```
* Once finished building, copy `include/stk.h` and `bin/release/stk.dll` to your project directory.

---

## Usage

### Basic Example

```c
#include <stk.h>
#include <stdio.h>

int main(void)
{
    int running = 1;
    size_t events;
    
    /* Initialize stk */
    if (stk_init() != STK_INIT_SUCCESS) {
        fprintf(stderr, "Failed to initialize stk\n");
        return 1;
    }

    /* Main loop - poll for module changes */
    while (running) {
        events = stk_poll();
        if (events > 0) {
            printf("Detected %zu module event(s)\n", events);
        }
        
        /* Your game/application logic here */
    }

    /* Shutdown stk systems*/
    stk_shutdown();
    return 0;
}
```

### Creating a Module

Modules are simple shared libraries with `stk_mod_init` and `stk_mod_shutdown` functions:

```c
/* my_module.c */
#include <stdio.h>

int stk_mod_init(void)
{
    printf("Module loaded!\n");
    return 0;  /* Return 0 on success */
}

void stk_mod_shutdown(void)
{
    printf("Module unloaded!\n");
}
```

Build as a shared library:
```bash
# Linux/BSD
cc -shared -fPIC -o my_module.so my_module.c

# macOS
cc -shared -fPIC -o my_module.dylib my_module.c

# Windows (MSVC)
cl /LD my_module.c /Fe:my_module.dll

# Windows (MinGW)
cc -shared -o my_module.dll my_module.c
```

Place the compiled module in the `mods/` directory (default), and `stk` will automatically load it and watch for changes.

### Configuration

```c
/* Set custom module directory (default: "mods") */
stk_set_mod_dir("custom_mods");

/* Set custom temp directory name (default: ".tmp") */
stk_set_tmp_dir_name(".my_tmp");

/* Set custom init function name (default: "stk_mod_init") */
stk_set_module_init_fn("my_init");

/* Set custom shutdown function name (default: "stk_mod_shutdown") */
stk_set_module_shutdown_fn("my_shutdown");

/*
 * All the above functions must be called before stk_init()
 * if the defaults need to be changed.
 */
stk_init();
```

### API Reference

#### Initialization
- `uint8_t stk_init(void)` - Initialize stk, returns `STK_INIT_SUCCESS` on success
- `void stk_shutdown(void)` - Shutdown and cleanup all modules

#### Runtime
- `size_t stk_poll(void)` - Poll for module changes, returns number of events processed
- `size_t stk_module_count(void)` - Get number of currently loaded modules

#### Configuration
- `void stk_set_mod_dir(const char *path)` - Set module directory
- `void stk_set_tmp_dir_name(const char *name)` - Set temp directory name
- `void stk_set_module_init_fn(const char *name)` - Set module init function name
- `void stk_set_module_shutdown_fn(const char *name)` - Set module shutdown function name

---

## Project Status

**Current Version:** 0.0.1 (Pre-release)

This is an early release proving the core hot-reload foundation. Phase 1 is still in progress.

### What Works
- Cross-platform module loading and hot-reloading
- File watching (inotify/kqueue/FindFirstFile)
- Basic error handling

### In Progress (Phase 1)
- Complete logging system (log levels, verbosity, output configuration)
- Module metadata (name, version, description)
- Dependency management and versioning

See [CHANGELOG.md](CHANGELOG.md) for detailed release notes.

---

## Testing

Run the included test suite:
```bash
./build.sh test    # Unix
build.bat test     # Windows
```

The test will watch the `modules/` directory and report when modules are loaded, reloaded, or unloaded.

---

## License

Mozilla Public License 2.0 (MPL-2.0)

---

## Contributing

Contributions welcome! Please ensure code follows C89 standard and works across all supported platforms.
