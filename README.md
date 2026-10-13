# XRDP RKMPP — Rockchip 硬件编码

[简体中文](README.md) | [English](README.en.md)

面向 **Rockchip RK3576 的 XRDP H.264 硬件编码实现**：直接对接 MPP，使用 NEON 优化桌面颜色转换，在客户端流控范围内重叠抓屏与编码。兼容现成的 RDP 客户端，包括 Windows 自带远程桌面。

**状态：已在一台 KICKPI K7、Ubuntu 24.04 arm64、厂商 Linux 6.1.75 上完成硬件验证。** 本项目是板卡专项适配，不是 XRDP 官方发行版，也不是通用 Rockchip 驱动。Ubuntu 22.04/20.04 及其他开发板尚未完成硬件验证。

## 开发说明

本项目针对 RK3576 的适配与优化工作，大部分由 **GPT-6 Astra** 在维护者指导下完成，包括代码实现、性能优化、测试工具和文档整理。此说明针对本项目新增工作；XRDP、xorgxrdp、Rockchip MPP 等上游项目的署名与许可保持不变。

## 实现路径

```text
X11 桌面 → xorgxrdp 抓屏 → BGRA 转 NV12（AArch64 NEON）
         → XRDP MPP 后端 → Rockchip MPP → RKVENC → H.264 AVC420 → RDP 客户端
```

RKVENC 是专用视频编码单元。Mali GPU 利用率不能用于判断它是否工作；桌面绘制与部分内存传输仍会消耗 CPU。

- 直接接入 XRDP 的 H.264 回调，仅保留这一套优化实现，已移除早期通用插件原型和旧实例脚本。
- 持久化、带缓存同步的 DMA 输入，只更新发生变化的区域。
- 输入所有权转移后允许抓屏与编码重叠，保留真实客户端 ACK 和上游在途帧数限制。
- 动态分辨率、闲置后刷新、IDR 携带 SPS/PPS，以及硬件失败后的明确 x264 回退与画面恢复。
- 严格模式用于验收，MPP 失败时不能静默回退。
- 固定 **XRDP 0.10.6.1 / xorgxrdp 0.10.5** 的 Git 提交和子模块版本，原生构建并使用独立版本目录。

## 性能实测

同一开发板、2560×1440、QP 18、16 ms 抓屏间隔、独立局域网 FreeRDP 客户端。**两组均已使用 MPP 硬编码**；表格比较 NEON、减少复制及抓屏/编码重叠的组合收益。

| 合成桌面负载 | 初始 MPP 路径 | 优化 MPP 路径 | 提升 |
|---|---:|---:|---:|
| 窗口移动 | 19.834 FPS | 35.889 FPS | 81% |
| 快速变色 | 25.111 FPS | 49.445 FPS | 97% |
| 移动加变色 | 20.278 FPS | 36.500 FPS | 80% |

指标是**服务端实际编码速率**，不是显示器呈现帧率或鼠标到画面的端到端延迟。每组为一次同条件测试，没有统计置信区间。另有 1080p 的 x264/MPP 编码回调对照，画质与码率并不相同；完整方法、解释和可机读数据见 [性能对比](docs/performance.md)。

## 构建与部署

从 [全新开发板安装教程](docs/new-board-install.md) 开始；另有 [英文入门说明](docs/getting-started.md)。教程覆盖依赖、原生编译、默认 3389 的单套服务和无头桌面、设备权限与真实 RDP 验收。

公开源码位于 [zhangjunnan8/xrdp-rkmpp](https://github.com/zhangjunnan8/xrdp-rkmpp) 的 **`main`** 分支。教程统一使用 `xrdp-rkmpp` 服务与目录名，不需要原开发环境的分支、旧 XRDP 安装或实验配置。

默认连接配置为 **1920×1080、单屏、3389**。下载 [RDP 连接模板](rk3576/RK3576-optimized.rdp)，填写地址后打开；窗口缩放只缩放显示内容，不自动增大编码分辨率。需要其他分辨率时，编辑连接文件的宽高或客户端显示选项后重连；服务端仍支持分辨率调整。直接在客户端填写 IP 时，分辨率取决于该客户端自己的设置。

需要可用的 RK3576 BSP/MPP、匹配的 Xorg 开发头文件、x264/OpenH264 开发包及 X11 桌面。普通 Ubuntu ARM 镜像不一定具备这些条件。

安装教程中的开发依赖准备好后，在板端执行：

```bash
git clone --branch main --single-branch https://github.com/zhangjunnan8/xrdp-rkmpp.git
cd xrdp-rkmpp
cmake -S . -B build
cmake --build build --parallel 4
```

根目录 CMake 自动浅拉取固定 Git 提交的 XRDP、其子模块和 xorgxrdp，应用补丁并生成两部分产物；等价于在 `build/` 中执行 `cmake ..` 后编译。二进制快捷入口为 `build/bin/xrdp`、`build/bin/xrdp-sesman`，完整安装树位于 `build/xrdp/stage/` 与 `build/xorgxrdp/stage/`。它不安装系统文件或重启服务，后续部署按安装教程执行。详见 [CMake 构建指南](docs/building.md)。**已在 RK3576 上通过原生编译、产物检查、MPP 硬编码与独立解码验证**，见 [板端验收记录](docs/validation/2026-10-13-cmake-native.md)。随后已完成公开路径下的 3389 单套部署、无头登录、动态分辨率和实际硬编码验收，见 [单套服务记录](docs/validation/2026-10-13-single-instance.md)。

部署后在该实例 `gfx.toml` 中选择 `h264_encoder = "MPP"`，并设置 XRDP 服务环境变量：

| 配置 | 含义 |
|---|---|
| `XRDP_MPP_QP=18` | 已测试的局域网固定 QP；支持 10–40 |
| `XRDP_MPP_DIRECT=1` | 直接更新持久化 DMA 输入；源码默认关闭 |
| `XRDP_MPP_PIPELINE=1` | 有界的抓屏/编码重叠；源码默认关闭 |
| `XRDP_MPP_STRICT=1` | 硬件验收；日常可设 `0`，允许有日志的软件回退 |

必须确认**本次连接**出现 `first hardware frame complete`，运行期没有 `switching to x264`。仅配置 MPP 不能证明硬编码，客户端还必须协商 H.264。详见 [实现说明](rk3576/README.md)。

## 使用边界

- H.264 AVC420 / 4:2:0、BT.709 全范围、Baseline、无 B 帧；没有 AVC444。
- 主要验收单屏桌面，多屏与多用户并发尚未验收。
- 没有 RGA 转换、端到端零拷贝或 GPU 桌面渲染加速。
- 固定 QP，没有广域网自适应码率；没有证明 4K60 或所有负载 60 FPS。
- 五分钟分辨率轮换不等于多日稳定性验证。测试板的 39 位虚拟地址内核无法启动 ASan，实际使用 UBSan 和显式内存检查。
- 默认无头运行，不需要 HDMI/DP 显示器或虚拟显示插头；仍需安装 Xorg、xorgxrdp 和 Xfce。

完整支持矩阵见 [已知限制](docs/known-limitations.md)。

## 目录与文档

| 路径 | 用途 |
|---|---|
| `rk3576/` | 当前 MPP 编码器、上游接入、NEON、构建与测试 |
| `docs/performance.md` | 性能、测试方法及解读 |
| `docs/validation/` | 带日期的验收记录与原始构建标识 |
| `docs/unattended-benchmark.md` | 合成负载与独立 LAN 客户端配置 |
| `CMakeLists.txt`、`cmake/` | 默认构建当前 XRDP 集成并管理固定上游依赖 |
| `docs/architecture.md` | 单套服务、会话管理和编码路径说明 |

公开快照不携带私有开发历史。验收文档中的构建 SHA 标识原实测产物，不一定能在公开仓库中检出。对应关系见 [发布来源与隐私检查](docs/public-release.md)。

## 贡献与许可

提交问题或改动前请读 [CONTRIBUTING.md](CONTRIBUTING.md)，提供系统、BSP、MPP、客户端及测试条件，日志须脱敏。不要上传密码、私钥、真实办公桌面截图或现场配置。

项目新增代码采用 **Apache-2.0**，完整文本见 [LICENSE](LICENSE)。上游及依赖保留各自许可与署名；来源和 x264 的独立许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。本仓库不附带 BSP、SDK 二进制、私钥或预编译发行包。
