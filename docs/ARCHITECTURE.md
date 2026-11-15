# Working on Neko

Neko is a C++20 application with a Qt Widgets frontend. Keep provider and filesystem logic independent of widgets so it can be exercised with temporary data directories.

## Ownership

| Location | Responsibility |
| --- | --- |
| `src/core.hpp` | Engine, HTTP, launch plan and shared data contracts |
| `src/core.cpp` | Common JSON, safe paths, metadata rules and Engine setup |
| `src/core/http.cpp` | Requests, downloads and cancellation |
| `src/core/metadata.cpp` | Version catalogs, profiles and library metadata |
| `src/core/loaders.cpp` | Fabric, Quilt, Forge and NeoForge installation |
| `src/core/runtime.cpp` | Java detection and game runtime environment |
| `src/core/launch.cpp` | Preparing game files and launch arguments |
| `src/core/packs.cpp` | Provider search, import and pending manual downloads |
| `src/core/mods.cpp`, `src/mod_install.hpp` | Injectable provider dependency resolution and staged mod installation |
| `src/core/auth.cpp` | Microsoft, Xbox and Minecraft authentication |
| `src/core/archive.cpp` | Archive extraction and atomic backups |
| `src/mod_storage.*` | Content-addressed immutable mod objects, hard links, cache reuse and cleanup |
| `src/file_ops.*` | Publishing completed files without mutating an old shared inode |
| `src/instance_services.*` | World snapshots, health reports, session ledger and command ranking |
| `src/window.*` | Main window construction, navigation and shared state |
| `src/ui/` | Pages and workflows, named by responsibility |
| `src/ui/launch.cpp` | Async preparation and QProcess lifecycle |
| `src/ui/jobs.cpp` | Worker dispatch, progress and logs |
| `src/ui/storage.cpp` | Storage actions, usage and independent-copy controls |
| `src/ui/mod_browser.cpp` | Instance selection and mod install review |
| `src/ui/instance_tools.cpp` | Health, snapshots, history and command dispatch |
| `src/cards.*`, `src/design.*` | Painted components and navigation |
| `src/ui/appearance.cpp` | Theme application and stylesheets |

The shared Window declaration remains in `window.hpp`; UI modules implement its workflows. These are source boundaries, not independent page controllers. Core operations take explicit roots and values rather than accessing Window state.

## Jobs and processes

Use `Window::task` for filesystem or network work that could block the interface. It owns a worker Engine and cancellation callback. Return data to the completion callback; widgets belong on the GUI thread. Core cancellation uses `Http::checkCancelled`. Snapshot operations accept a cancellation callback as well.

Launch preparation creates a `Launch` value. `startGame` owns process callbacks; only a successful process-start signal begins a session. Exit records use a monotonic duration and wall-clock start time. Stop requests are recorded separately from crashes. Failed starts create no session.

## Persistence and recovery

- `QSettings` holds interface preferences and the selected instance. Secure Microsoft credentials belong in QtKeychain with insecure fallback disabled, managed in `ui/accounts.cpp`.
- `instances/<id>/instance.json` holds configuration and display statistics. Read fresh configuration before writing incremental updates so an unrelated setting is preserved.
- `instances/<id>/history.json` atomically stores lifetime totals and the last 100 sessions. A lock serializes writes; the instance statistics are a display mirror.
- `snapshots/<id>/` contains saves-only ZIPs and JSON records with a SHA-256 digest, version, reason and creation time. Automatic retention never deletes manual or recovery points.

Restore validates the identity, game version and digest before staging extraction. It records current saves as a recovery snapshot before replacing the saves directory, using directory renames with rollback. Mods and settings are outside this operation. Snapshot storage on the same disk does not protect against disk loss.

Use `safePath`, atomic `writeJson`, verified downloads and the archive protections for new import paths. Do not log credentials or store raw launch argument lists in session records.

## Verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The existing Nix workspace uses `build-verified` with its configured compiler and dependencies. Avoid mixing toolchains in that directory.

`core-tests` covers launch metadata, imports and security boundaries. `instance-services-tests` uses temporary fixtures for snapshots, recovery and session accounting. Add cases for observable failures and data preservation rather than duplicating implementation details.

For UI changes, use an isolated directory because diagnostics change settings:

```sh
QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME=/tmp/neko-ui-config XDG_DATA_HOME=/tmp/neko-ui-data ./build/neko-launcher --ui-test
```

This checks responsive pages and font sizes across Catppuccin themes, dialogs, command selection and short-lived process callbacks. Screenshots are saved under `build-verified/ui-previews`. Seed only synthetic instances if testing a populated library. It does not authenticate or launch Minecraft. Optional network and loader integration checks are described in the README.

Mod resolution takes an injected metadata fetch function; installation takes an injected downloader and checkpoint. Offline tests cover provider graphs and filesystem transactions. Keep required dependency traversal, compatibility checks, file staging and UI review separate. Receipts live in `instance.json` under `managedMods`; credentials never belong in receipts.

## Mod storage rules

Only instance `mods/**/*.jar`, disabled JARs and staged `.mods-*` JARs enter shared storage. `safePath` continues rejecting symlinks; hard links need no path-safety exception. Object names are SHA-256 digests. SHA-1/SHA-512 indexes only locate candidate objects, which are verified against both identity and the requested checksum before reuse.

`ModStorage` serializes object publication and cleanup with a lock. Cleanup requires a hard-link count of exactly one (the object path itself). Do not garbage-collect using instance manifests alone: disabled mods, rename operations and references outside those manifests still count.

All writes that can replace a shared file must stage a new sibling and use `publishFile`. Opening a shared destination for truncation or changing its permissions to enable writing can affect every reference. Read-only destinations also cause `QSaveFile(destination)` to refuse opening; stage under a fresh name first. `publishFile` replaces the directory entry, and never opens the destination for writing. Windows uses a rollback rename when replacement rename is unavailable; Linux is the validated platform.

`privateMods` in instance configuration disables new sharing for that instance. Private conversion persists this preference first, then creates independent writable files; cancellation can leave some files still shared until retried. Mutable world, configuration and account files must never enter the object store.
