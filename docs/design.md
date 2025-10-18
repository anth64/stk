# stk (Stalwart Toolkit) Design Roadmap

## Vision
`stk` is a lightweight, unopinionated toolkit for building games and engines. It provides a **portable, flexible, and modular foundation** for dynamically loading modules, native or WASM, without imposing design choices. 
`stk` is designed to run **anywhere a C89 compiler exists**. From retro consoles and embedded systems to modern PCs running POSIX or Windows.

---

## Philosophy
- **Lean by design:** only the essentials for modularity and hot-swapping.
- **Predictable and safe:** behaves consistently with no hidden complexity or runtime surprises.
- **Extensible/Optional Features:** WASM modules are add-ons, never mandatory.
- **Non-opinionated:** developers control architecture and game logic; `stk` imposes nothing.
- **Minimal footprint:** core remains small and free of out-of-scope features.
- **Developer tools included:** metadata, logging/tracing, and dependency management.  

---

## Key Principles
1. **Portability:** Runs on hardware from retro consoles to modern PCs.
2. **Simplicity:** Minimal API, easy to integrate into any engine or game.
3. **Flexibility:** Supports multiple languages via optional WASM modules.
4. **Performance:** Lean, predictable, small binaries; no unnecessary runtime.
5. **Extensibility:** Optional enhancements for modern targets.

---

## Design Scope
- Provides **dynamic module loading** and **hot-swapping**.
- Supports **native shared libraries** and **WASM modules**.
- Includes **developer tools**: minimal metadata (name, version, dependencies), lightweight logging/tracing, and **module dependency management**.
- Does **not enforce engine design, architecture, or coding philosophy**.
- Public API is **C89**, ensuring portability and minimal overhead.

---

## Development Phases

### Phase 1: Native Module Support
- Load/unload shared libraries (`.so` / `.dll`).
- Resolve symbols dynamically.
- Hot-swappable at runtime.
- **Developer tools included:** metadata, logging/tracing, and dependency management.
- Platform-agnostic (POSIX, Windows, retro/embedded targets with C89).

### Phase 2: Optional WASM Support
- WASM modules as an alternative backend.
- Multi-language support (Rust, Go, Zig, etc.).
- WASM runtime optional, old or retro hardware can ignore it.
- Sandbox and safety checks for WASM modules.  

---

## High-Level Roadmap Summary
| Phase | Focus | Core vs Optional | Target Hardware |
|-------|-------|----------------|----------------|
| 1 | Native modules + developer tools (metadata, logging, dependencies) | Core | Retro, embedded, POSIX, Windows |
| 2 | WASM modules | Optional | Modern systems only |

---

## Strategic Benefits
- **Retro-ready:** Works on old consoles or embedded systems with minimal footprint.
- **Modern-friendly:** Developers can use modern languages without bloating the core.
- **Modular foundation:** Build engines, games, or experimental projects on top.
- **Scalable:** Hot-swappable modules, metadata, logging, and dependency tracking allow rapid iteration and predictable behavior.

