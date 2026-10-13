# 单套无头桌面的架构 / Single headless instance

本项目只维护优化后的 RK3576 直接 MPP 实现。默认安装一套服务，使用标准 RDP 端口 **3389**；不安装对照版软件编码服务或早期 MPP 服务。

```text
RDP 客户端 → TCP 3389 → xrdp-rkmpp
                           ↕ 本地会话管理
                     xrdp-rkmpp-sesman
                           ↓
                 虚拟 Xorg + xorgxrdp + Xfce
                           ↓
           脏区域抓屏 → NEON BGRA→NV12 → MPP → RKVENC
                           ↓
                   H.264 AVC420 → RDP 客户端
```

`xrdp-rkmpp` 负责 RDP 连接、传输与编码；`xrdp-rkmpp-sesman` 负责登录、权限和桌面会话生命周期。两个 systemd unit 组成同一套远程桌面，只有前者监听对外的 3389。Xorg 显示号 `:50` 是内部会话标识，不是另一个 RDP 端口。

无头桌面由 xorgxrdp 的虚拟 Xorg 设备创建，不依赖已连接的 HDMI/DP 显示器或本地显示管理器。仍须安装 X11/Xorg/Xfce 软件；MPP 只加速视频编码，桌面绘制和部分内存传输仍消耗 CPU。

## 唯一维护的编码路径

`xrdp_encoder_mpp.c` 直接实现 XRDP H.264 回调，使用持久化 DMA 输入、局部更新、明确缓存同步和有限等待。`nv12-neon.c` 加速颜色转换；`xrdp_mpp_pipeline.inc` 在输入所有权转移后允许抓屏与编码重叠，保留客户端 ACK 和上游在途帧限制。

公开部署启用 `XRDP_MPP_DIRECT=1` 和 `XRDP_MPP_PIPELINE=1`。这些是同一实现的运行开关，便于故障定位；不生成第二套服务器。安装与头文件版本由根目录 CMake 统一管理。

## 软件回退的范围

MPP 失败时，同一个 XRDP 进程可调用上游 x264 回退，重建完整帧并发送恢复 IDR，日志明确记录切换。`XRDP_MPP_STRICT=1` 在硬件验收时禁止此回退；日常模式为 `0`。x264 是这条故障恢复路径的依赖，不是额外安装的对照服务。RDP 客户端的编码能力仍需协商，严格模式不强迫客户端支持 H.264。

当前源码树已移除通用 CodecBridge ABI、插件加载器、独立 x264 插件和绑定旧实验布局的构建/安装/压测脚本。旧实现可从 Git 历史查阅；性能报告保留当时真实的构建标识与条件。没有新增树莓派、全志或其他 GPU 的兼容层。

English: one optimized RK3576 implementation, one RDP listener on TCP 3389, and two cooperating services (listener and session manager). The virtual Xorg desktop is headless. Upstream x264 remains an in-process failure recovery dependency, not a second server installation.

See [installation](new-board-install.md), [encoder details](../rk3576/README.md) and [limitations](known-limitations.md).
