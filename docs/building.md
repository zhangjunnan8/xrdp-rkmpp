# CMake build / CMake 构建

根目录 CMake 默认构建 RK3576 的 XRDP 集成。第三方完整源码不纳入本仓库：CMake 将固定提交浅拉取至 `build/_deps/`，在独立生成目录应用补丁。MPP、Xorg 和系统开发库使用板端已安装的匹配版本。

## Build / 构建

先完成[新板教程的依赖检查](new-board-install.md#3-安装构建依赖保留厂商图形组件)。要求 Linux ARM64、CMake 3.16+、Python 3.8+、Git、C/C++ 编译器、Autotools、GNU Make，以及匹配 BSP 的 MPP/Xorg 开发包。

```bash
git clone --branch main --single-branch https://github.com/zhangjunnan8/xrdp-rkmpp.git
cd xrdp-rkmpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel 4
```

也可以 `mkdir build && cd build`，再执行 `cmake ..` 和 `cmake --build .`。CMake 自动下载、校验版本、打补丁、编译和暂存，并将 XRDP 头文件传给 xorgxrdp。

Upstream Autotools is orchestrated through [CMake ExternalProject](https://cmake.org/cmake/help/v3.16/module/ExternalProject.html), not reimplemented. Building needs no sudo and changes no system packages or services. `--parallel` controls the outer build; upstream GNU Make uses `XRDP_RKMPP_JOBS` jobs (default 4). Configure with `-DXRDP_RKMPP_JOBS=2` on a memory-constrained board.

## Outputs / 产物

| Path under `build/` | Contents |
|---|---|
| `bin/xrdp`, `bin/xrdp-sesman` | Launchers for staged executables, with their private library search path |
| `bin/xrdp-rkmpp-motion-test` | X11 animation executable |
| `xrdp/stage/`, `xorgxrdp/stage/` | Full isolated install trees, including libraries/configuration and Xorg modules |
| `xrdp/install-prefix`, `xorgxrdp/install-prefix` | `/opt/xrdp-rkmpp/<SHA>` and `/opt/xrdp-rkmpp-xorg/<SHA>` |
| `install-env.sh` | Installation variables, generated after both builds succeed |
| `xrdp/build-info.json`, `xorgxrdp/build-info.json` | Source SHA, dirty flag, upstream commit and prefix |
| `xrdp/test-direct`, `xrdp/test-lifecycle` | Hardware test executables; not run automatically |
| `xrdp/pipeline-test.log` | Parser/ownership checks with UBSan |
| `xorgxrdp/conversion-test.log`, `conversion-ubsan.log` | CPU-only converter/reference checks |
| `_deps/*-ep/src/*-stamp/` | ExternalProject configure/build/install logs |

Check the new executables before installation, without starting a daemon:

```bash
build/bin/xrdp --version
build/bin/xrdp-sesman --version
```

These launchers set `LD_LIBRARY_PATH` for the child process to find this build's staged XRDP libraries. Actual ELF executables remain under `xrdp/stage/opt/xrdp-rkmpp/<SHA>/sbin/`; deployment uses those files, not the launchers. Keep the build directory in place or regenerate the launchers after moving it.

Binaries also link against the board's libraries; they are not portable apps and do not create an RDP service. Use [installation steps 6–9](new-board-install.md#6-首次安装独立-3391-服务), starting with:

```bash
source build/install-env.sh
```

默认构建不运行 MPP 编码、不连接桌面、不启动服务。`cmake --install` 不承担系统部署；当前提供暂存产物和首次安装步骤。部署使用干净 Git 检出；修改状态记录在 `build-info.json` 中。

The CMake-built `test-direct` and `test-lifecycle` use the actual encoder objects and config parser, but redirect the parser's filename to staged `gfx.toml`. This test-only linker wrapper allows x264/fallback checks before installation without writing to `/etc`; `XRDP_TEST_GFX_CONFIG` can explicitly select another test fixture. Server binaries retain their normal configuration path. Run hardware tests separately on an idle board with the required device permissions.

## Dependencies / 固定依赖

Pins are in [UpstreamVersions.cmake](../cmake/UpstreamVersions.cmake):

| Upstream | Release | Commit |
|---|---|---|
| XRDP | 0.10.6.1 | `80c52879fead585d6347df5b3c587718adf8f5a5` |
| xorgxrdp | 0.10.5 | `c2d3cbafebe00be3221f985c06ef6b97f1c63790` |

XRDP's `libpainter`, `librfxcodec` and `ulalaca` use gitlinks in that commit. The helper verifies HEAD, tracked changes and submodule state before patching. It does not track moving tags or branch tips. MPP/kernel/Mali drivers remain vendor-provided; do not substitute arbitrary upstream builds. Legacy archive scripts retain checksum checks for existing workflows.

## Workstation checks / 工作站源码检查

Linux/WSL without an ARM64 board can fetch and check sources and patches:

```bash
cmake -S . -B build-fetch -DXRDP_RKMPP_FETCH_ONLY=ON
cmake --build build-fetch
```

This generates no binary, installation environment file or hardware result. Default compilation rejects non-ARM64 hosts and cross-compilation because converter checks execute target code. Use separate directories for source checks and native builds.

## Rebuilds / 重建

Re-run the same CMake commands after changes. Integration input changes reconstruct marked generated work trees from pristine upstream checkouts; repeated builds reuse downloads. Do not edit generated files. Only marked output inside the build directory may be replaced.

Repeated native builds still stage files and rerun CPU checks. Upstream XRDP installation refreshes staged header timestamps, so xorgxrdp may compile again even when source content is unchanged; dependency downloads and patching are reused.

`XRDP_RKMPP_XRDP_REPOSITORY` and `XRDP_RKMPP_XORG_REPOSITORY` can point to trusted Git mirrors/local repositories; commit pins still apply. Offline mirrors must also provide XRDP's submodules through Git URL mapping/cache configuration. Use a new build directory when changing repository URLs or build modes.

Retry with `cmake --build` after inspecting component `autoreconf.log`, `configure.log`, `install.log` and ExternalProject logs. Never delete unrecognized directories to bypass ownership guards.

The old prototype requires `-DXRDP_RKMPP_BUILD_LEGACY_PROTOTYPE=ON`; see [legacy instructions](legacy-building.md). It does not build the RK3576 integration. Validation scope: [initial workstation checks](validation/2026-10-11-cmake-build.md) and [native RK3576 build/encoding checks](validation/2026-10-13-cmake-native.md).
