# 桌面负载验证 / Desktop workload checks

当前只部署一套优化版，默认 TCP **3389**。使用空闲测试板或已约定的维护窗口；不要在办公会话中自动执行负载、重启或更改分辨率。

## 无头真实桌面负载

通过 Windows 远程桌面或启用 H.264 的 FreeRDP 连接后，在该 RDP 桌面的终端运行：

```bash
xrdp-rkmpp-motion-test 60 cycle
```

程序创建真实 X11 窗口，每 20 秒切换移动、变色、移动加变色三种负载。它经过桌面绘制、抓屏、转换、编码、传输和客户端解码；不模拟磁盘 I/O，也不测量物理输入延迟。绘制次数不等于客户端呈现 FPS。

在 SSH 终端检查本次连接与硬件活动：

```bash
sudo ss -tnp '( sport = :3389 )'
sudo grep -E 'selected RK3576|first hardware frame complete|switching to x264|MPP:' \
  /var/log/xrdp-rkmpp/xrdp.log | tail -40
sudo cat /proc/mpp_service/rkvenc-core0/sessions-info
grep -Ei 'rkvenc|venc' /proc/interrupts
```

日志必须属于刚连接的进程，看到首个硬件帧完成，运行期间没有回退。分别检查 1080p/1440p、需要时 4K、重连和客户端动态分辨率调整。独立编码/解码测试见 [安装教程](new-board-install.md)。

## 独立客户端自动化

自动验证需要另一台 Linux 客户端的 Xvfb 和支持 AVC420 的 FreeRDP。使用专用 Xvfb 显示号、普通桌面账号和当前服务的实际地址，验证服务证书并通过 stdin 提供密码。客户端断开只应结束测试自己创建的进程。服务端通过该连接的实际 Xorg 显示号和授权文件启动动画，不能把历史显示号当作所有权证明。

可在客户端编译 `rk3576/test_desktop.c`（依赖 X11）来调整自己的 FreeRDP 窗口。不要仅靠 FreeRDP 进程存活判断桌面正常；还需服务端 Xorg/窗口管理器日志、编码完成和客户端解码检查。

历史固定端口、服务路径和现场配置的自动化包装脚本已从当前树移除；它们及旧通用原型可从 Git 历史查阅。2026-10-10 的 [性能数据](performance.md) 与 [原始验证报告](validation/2026-10-10-rk3576-optimization.md) 保留原始测量条件，删除旧工具不产生新的性能结果。

English: the retained X11 animation exercises the real desktop path on the single default 3389 instance. Run it only in a dedicated session. Hardware completion logs and RKVENC activity prove encoding; draw counts do not prove delivered FPS. Historical multi-instance wrappers are no longer part of this source tree.
