# Windows-vcpkg

Run `powershell -NoProfile -ExecutionPolicy Bypass -File building\Windows\build.ps1`
from a Windows x64 machine. Jenkins uses the `Windows-vcpkg` stage, controlled
by `RUN_WINDOWS`. The previous Windows scripts remain available for manual use.

Prerequisites: Git, Visual Studio C++ x64 tools and Windows SDK, Ninja, CMake
3.30+, and NSIS 3.03+ (`makensis.exe`) on the build account's PATH.
For a standard NSIS installation, add `C:\Program Files (x86)\NSIS` to PATH.
WiX is no longer needed.

To package an existing staging directory (including one left by a failed WiX
run), use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File building\Windows\package.ps1
```

This regenerates the CPack configuration for NSIS and packages the existing
runtime without compiling the application or dependencies. It requires
`app/VERSION.txt` and the complete `stage` directory under the build root.
Both scripts accept `-BuildRoot`; run only one script per build root at a time.
Failed packaging preserves the stage and prints `NSISOutput.log` for diagnosis.
Successful packaging removes the stage and CPack scratch files to save space.

The existing vcpkg checkout at `C:\src\vcpkg` must be on a tracking branch;
the script refreshes it with `git pull --ff-only` and bootstraps it each run.

The result is `build/windows-vcpkg/assets/NextApp-<version>-windows-x64.exe`.
Version comes from the root CMake build's `VERSION.txt`. It is a per-machine
x64 installation under Program Files/NextApp with a Start Menu shortcut.
Only the UI target and its dependencies are built; AI/MCP remains enabled.
Backend, signup, tests, examples, and unused QCoro modules are excluded.
There is no change to the MCP interface.

## Dependencies and disk use

The manifest pins the requested baseline. Qt default features are disabled;
the explicit feature list supports the client's CMake targets, TLS, SQLite,
SVG icons and QML/Quick Controls. Qt Test is currently linked by
the production client and required by Qt Declarative's port. Qt Charts,
Graphs, Multimedia, WebEngine, database drivers other than SQLite, and the
Qt SDK meta-package are not requested. Transitive build tools required by
these ports are still built by vcpkg.

The `ports/qtgrpc` overlay copies the port and its patch from that baseline
and adds Qt Declarative as a dependency. This makes ProtobufQuick generation
independent of package build order and includes that dependency in the ABI
cache key. It explicitly requires the protobuf/gRPC generators, Qt Core protobuf
types, well-known types and ProtobufQuick; failed feature checks must stop the
package build instead of caching a package without required modules. Review the
overlay when updating the baseline.

The qtgrpc overlay and application configuration use Release for CMake's
configure-time compile/link checks too. Otherwise those checks can use the
debug MSVC runtime and fail against the release-only static dependencies.

Both host and target use `x64-nextapp-static-release`, requesting static
libraries and the static MSVC runtime (`/MT`), with release builds only. The
application uses the same runtime setting. Using one triplet avoids separate
debug builds and duplicate native host tools. `--clean-after-build` asks
vcpkg to remove source/build trees, package staging, and downloaded archives
after each package. Its diagnostic logs and extracted helper tools may remain.

`C:\src\nextapp-windows-cache` holds the installed development dependencies
and compressed binary archives outside Jenkins workspaces. vcpkg checks its
ABI keys (ports, features, triplet, compiler, and dependencies) on every run;
unchanged packages are reused. Deleting a workspace or the installed tree
still allows restoration from the binary cache. Do not routinely delete the
archives. No forced dependency rebuild or cache bypass occurs on a normal run.
Override `-CacheRoot`, `-VcpkgRoot`, `-BuildRoot`, or `-Jobs` if needed.

Switching from the previous dynamic triplet requires a one-time dependency
rebuild; existing dynamic binary archives cannot satisfy static package ABI
keys. Keep the archives: subsequent static builds reuse the new entries. The
script refreshes the application's CMake cache automatically on the triplet
change so cached Qt/DLL package paths cannot leak into the static build.

A file lock in the vcpkg checkout prevents simultaneous invocations of this
script from modifying shared state; a conflicting invocation fails and should
be retried. Other jobs using that checkout must be scheduled separately.
Application objects remain in the workspace for incremental rebuilds. The
deployment tree and CPack staging are removed after success. The installer contains
only deployed runtime files and the application license;
headers, static libraries, debug symbols, tests, and build tools are excluded.

The Windows platform, SQLite, SVG image/icon, OpenSSL TLS and Windows network
reachability plugins are linked into the executable explicitly. Qt's automatic
QML import scan embeds the QML plugins and resources needed by the app, including
the selectable Controls styles. No `windeployqt` copy or external QML tree is needed. Remaining DLLs
from any ports that require dynamic linkage are collected by the dependency
scanner; Windows system DLLs are excluded. The build reports the uncompressed
runtime payload size before NSIS compression. Static linking does not eliminate
the development libraries or binary archives on the build machine.

## Installer identity and updates

`InstallerIds.cmake` saves the stable NSIS registry identity `NextApp-x64`.
Keep it unchanged across versions. The install directory, display name and
Start Menu folder are also independent of the version. CPack's NSIS installer
recognizes a previous installation and offers to uninstall it before installing
the new version. Accept that prompt to remove obsolete DLLs and plugins; silent
installation (`/S`) defaults to accepting it. Same-version reinstall is supported.
This is uninstall/reinstall, without MSI's transactional rollback or automatic
downgrade prevention. Close NextApp before installing an update.

The generated uninstaller removes the recorded installation files and installer
registration, leaving application settings and user databases intact. No custom
user-data cleanup commands from the old Windows build are used.

The historical MSI UUIDs remain reserved in `InstallerIds.cmake` for potential
future migration, but NSIS does not use them. Older legacy NSIS installers and
any MSI installations are not automatically migrated. Back up user data before
removing a legacy installation: its old uninstaller may delete settings/data.
The separate registry identity prevents running that uninstaller automatically.

Before release, test on a Windows VM without Qt/vcpkg on PATH: launch the app,
connect over TLS, exercise SQLite and QML, then install a higher-version installer,
check that one installed product remains and user settings survive, exercise
a same-version reinstall and silent update, and uninstall. Signing is not
configured here.

References: [vcpkg cleanup options](https://learn.microsoft.com/en-us/vcpkg/commands/install#--clean-after-build),
[binary caching](https://learn.microsoft.com/en-us/vcpkg/consume/binary-caching),
[static Qt plugins](https://doc.qt.io/qt-6/qt-import-plugins.html),
[static QML imports](https://doc.qt.io/qt-6/qt-import-qml-plugins.html),
[CPack NSIS configuration](https://cmake.org/cmake/help/latest/cpack_gen/nsis.html).
