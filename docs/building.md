# 构建和验证

> Historical generic prototype / 历史通用原型：不用于当前 RK3576 直接硬编码路径。当前实现见 [RK3576 integration](../rk3576/README.md)。

运行目标为 Linux，源码管理可在 Windows 进行。阶段 1 使用 C11、CMake 3.16+；核心只依赖系统动态加载器，x264 插件额外依赖 `pkg-config` 与 x264 开发包。独立码流测试使用 Python 3、FFmpeg 和 ffprobe。

Ubuntu 依赖包为 `gcc make libc6-dev cmake pkg-config libx264-dev`，验证码流额外需要 `python3 ffmpeg`。安装依赖前应检查目标系统的现有包和厂商版本。

## 标准构建

在 Linux 源码目录运行：

```sh
build_dir="$HOME/build/xrdp-codecbridge/$(git rev-parse HEAD)"
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$build_dir" -j4
ctest --test-dir "$build_dir" --output-on-failure -V
```

有 Python、FFmpeg 和 ffprobe 时应看到 `contract`、`x264-lifecycle`、`h264-roundtrip` 三项测试。缺少解码工具时 CMake 会明确报告 round-trip 测试不可用；只通过前两项不能证明码流已被独立解码。

核心可独立构建：

```sh
cmake -S . -B "$build_dir-core" -DCB_BUILD_X264=OFF
cmake --build "$build_dir-core" -j4
ctest --test-dir "$build_dir-core" --output-on-failure
```

关闭 x264 后不查找其依赖，核心和插件契约测试仍可运行。

## 探测与生成码流

```sh
"$build_dir/cb-encode" probe "$build_dir/plugins/cb_backend_x264.so"
artifact_dir=$(mktemp -d)
"$build_dir/cb-encode" encode \
  --plugin "$build_dir/plugins/cb_backend_x264.so" \
  --output "$artifact_dir/sample.h264" \
  --raw-output "$artifact_dir/reference.nv12" \
  --width 1920 --height 1080 --frames 60 --fps 30 \
  --padding 16 --keyframe-every 15 --qp 23
ffmpeg -v error -i "$artifact_dir/sample.h264" -f null -
```

工具生成确定性的移动 NV12 图案，输出 JSON 统计。文件使用独占创建，已有文件会报错。`--raw-output` 可选，保存不含行填充的原始 NV12 图像用于比较。

`encode_mean_ms`、`encode_p50_ms`、`encode_p95_ms` 测量提交到取回码流的时间，包含输入复制与编码，不包含该帧图案生成和文件写入。P95 使用 nearest-rank。`wall_ms` / `cpu_ms` 是整轮生成和编码的时间，`unpaced_fps` 是不等待情况下的测试吞吐量；这些值都不能当作远程桌面帧率或交互延迟。

## 内存与未定义行为检查

```sh
cmake -S . -B "$build_dir-ubsan" \
  -DCMAKE_BUILD_TYPE=Debug -DCB_SANITIZERS=undefined
cmake --build "$build_dir-ubsan" -j4
UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir "$build_dir-ubsan" --output-on-failure
```

支持 AddressSanitizer 的 Linux 环境可使用 `-DCB_SANITIZERS=address,undefined`，并设置 `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`。RK3576 当前厂商内核使用 39 位虚拟地址，该机器 GCC 13 的 ASan 在进入程序前报告 allocator 地址空间初始化失败；应单独记录为环境限制，不能计为测试通过。可在本机 Linux/WSL 补充核心和软件插件的 ASan 检查；它不能代替未来 MPP 后端的板端检查。

## 独立前缀安装

```sh
install_prefix="$HOME/opt/codecbridge"
cmake --install "$build_dir" --prefix "$install_prefix"
"$install_prefix/bin/cb-encode" probe \
  "$install_prefix/lib/codecbridge/cb_backend_x264.so"
```

库目录由 `CMAKE_INSTALL_LIBDIR` 控制；上例使用 `lib`。`cb-encode` 设置相对安装库目录的 RPATH。安装核心、头文件和软件插件，不安装故障注入插件，也不自动修改 XRDP 或启动桌面服务。
