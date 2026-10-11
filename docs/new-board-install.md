# 全新 RK3576 开发板部署教程

适用于公开仓库 **[zhangjunnan8/xrdp-rkmpp](https://github.com/zhangjunnan8/xrdp-rkmpp) 的 `main` 分支**：在 KICKPI K7 / RK3576 上原生编译 XRDP 0.10.6.1 和 xorgxrdp 0.10.5，启用 MPP 硬编码、NEON 转换和流水线优化。本文以 3391 端口、Xfce 桌面、单屏办公为例，无需原开发环境或任何私有仓库。

本文使用以下公开安装约定，后续命令中的名称必须保持一致：

| 项目 | 本教程使用的值 |
|---|---|
| 源码目录 / 分支 | `$HOME/src/xrdp-rkmpp` / GitHub `main` |
| XRDP / Xorg 构建参数 | `xrdp-rkmpp` / `xrdp-rkmpp-xorg` |
| 安装目录 | `/opt/xrdp-rkmpp/<SHA>`、`/opt/xrdp-rkmpp-xorg/<SHA>` |
| 配置 / 日志 / 运行目录 | `/etc/xrdp-rkmpp/xrdp`、`/var/log/xrdp-rkmpp`、`/run/xrdp-rkmpp` |
| systemd 服务 | `xrdp-rkmpp`、`xrdp-rkmpp-sesman` |
| RDP 端口 / Xorg 起始显示号 | `3391` / `:50`，用于与其他实例隔离；安装前检查是否空闲 |
| 桌面账号 / 地址 | 使用新板自己的普通用户、密码和实际地址 |

不要把发布维护者的 `release/rk3576-open-source` 分支名用于从 GitHub 安装；公开仓库的安装入口是 `main`。旧实验辅助脚本不是本教程的安装或升级入口。

## 1. Ubuntu 24.04、22.04、20.04 是否兼容

| 板端系统 | 当前结论 | 使用条件 |
|---|---|---|
| 厂商 Ubuntu 24.04 arm64 | **已在现有 KICKPI K7 实测** | 已验证环境为厂商 6.1.75 内核、MPP 包 1.5.0-1、Xorg 21.1.7；不同镜像仍需重新验收 |
| Ubuntu 22.04 arm64 | **有移植条件，尚未实测** | 必须是能驱动 RK3576 的 BSP 镜像，具备匹配的 MPP 开发包、设备节点和 Xorg SDK；在目标系统重新编译 |
| Ubuntu 20.04 arm64 | **需要额外适配，尚未实测** | 需要适配该系统的 BSP/MPP；不能直接安装现有板子的 MPP 二进制包 |

决定硬编码是否可用的是 **内核驱动 + MPP 用户态库 + 设备权限 + Xorg 模块**，不能只看 Ubuntu 的版本号，也不能把“都是 arm64”当作二进制兼容保证。普通 Ubuntu ARM 镜像不一定包含这块板子的启动和编码驱动。

具体差异：

- 核心实时路径是 C，NEON 路径要求 AArch64；需要在目标板的系统上编译。不要把 24.04 编译的 `/opt` 目录直接复制到 22.04/20.04。
- 现有板子 `librockchip-mpp1 1.5.0-1` 声明依赖 `libc6 >= 2.34`。Ubuntu 20.04 的基础 glibc 为 2.31，直接安装该包不满足依赖。应取得面向 20.04、与其 BSP 匹配的 MPP 包/源码；不要替换系统 glibc 来绕过。
- MPP 还必须提供可缓存缓冲区及 `mpp_buffer_sync_begin/end`、`mpp_buffer_sync_ro_begin/end` 等接口。相同的包版本文字不足以证明接口和驱动匹配，以编译和实际编码为准。
- 本文的内嵌 Python 配置代码按 Python 3.8 语法检查，不调用旧实验安装器，也不要求替换系统 Python；这不代表已完成 Ubuntu 20.04 的运行验证。
- Ubuntu 20.04 已结束标准安全维护；新装办公设备优先选厂商支持的 24.04 镜像。若必须用 20.04，应另外安排其安全维护。

**验证边界：** 本文安装命令按当前源码与现有板的构建产物核对；没有第二块空白板的端到端安装记录，也没有 22.04/20.04 的硬件运行记录。现有 24.04 的验收见 [优化记录](validation/2026-10-10-rk3576-optimization.md)。

## 2. 准备系统并确认目标板

先刷入厂商提供的、支持 KICKPI K7 / RK3576 的 **64 位 Linux 镜像**，建立普通登录用户，确保 SSH 和网络可用。后续在新板的 SSH 终端执行；用户名、主机名和 IP 按自己的环境配置。

下文连续使用同一个 Bash 终端及其变量，遇到失败先停下处理，不继续复制后面的安装命令。编译以普通用户执行，只有安装依赖和系统配置使用 sudo。

```bash
set -euo pipefail
hostname
ip -4 -brief address
cat /etc/os-release
uname -m
uname -r
tr '\0' '\n' < /proc/device-tree/compatible
dpkg --print-architecture
df -h /
ls -l /dev/mpp_service /dev/dma_heap/system
```

必须看到 `aarch64` / `arm64`、`rockchip,rk3576`，以及可用的 MPP 和 DMA 设备。缺少 `/dev/mpp_service` 时先解决 BSP/驱动问题，安装 XRDP 不能补出编码驱动。测试和构建建议预留至少 4 GiB 磁盘空间。

## 3. 安装构建依赖，保留厂商图形组件

先刷新包索引、检查锁定和模拟安装，不要顺手执行全系统升级或 `autoremove`：

```bash
sudo apt-get update
apt-mark showhold
dpkg-query -W xserver-xorg-core
# 开发包在新板上可能尚未安装；先列出现状，再检查安装计划。
dpkg-query -W xserver-xorg-dev librockchip-mpp1 librockchip-mpp-dev || true
apt-cache policy xserver-xorg-core xserver-xorg-dev librockchip-mpp1 librockchip-mpp-dev
XORG_VERSION=$(dpkg-query -W -f='${Version}' xserver-xorg-core)

BUILD_DEPS=(build-essential git ca-certificates curl autoconf automake
  libtool pkg-config libssl-dev libpam0g-dev libx11-dev libxfixes-dev
  libxrandr-dev libjpeg-dev libfuse3-dev libpixman-1-dev libx264-dev
  libopenh264-dev xorg-dev "xserver-xorg-dev=$XORG_VERSION" libepoxy-dev libgbm-dev
  libdrm-dev python3 ffmpeg)
sudo apt-get -s install --no-install-recommends "${BUILD_DEPS[@]}"
```

如果镜像尚未安装 MPP 运行库或开发包，先从**该镜像对应的厂商软件源/SDK**安装匹配的软件包。它们未必能从 Ubuntu 官方仓库取得。厂商使用其他包名时，根据实际文件和 `pkg-config` 检查，不要盲目改用其他平台的 MPP 包。`xserver-xorg-core` 必须已经存在；缺少 Xorg 时先按厂商说明准备匹配的 X11 图形环境。

检查模拟结果：不能移除/替换厂商 MPP、Mali、RGA 或锁定的 Xorg；`xserver-xorg-dev` 应与实际 Xorg 来源、版本匹配。镜像自带 21.1.7 而 Ubuntu 仓库只提供 21.1.12 开发包时，应从厂商取得匹配 SDK，不能解除锁定强行安装。确认计划合适后执行：

```bash
sudo apt-get install --no-install-recommends "${BUILD_DEPS[@]}"
sudo apt-get -s install --no-install-recommends xfce4 xfce4-terminal dbus-x11 xauth fonts-dejavu-core
```

桌面依赖同样确认没有替换厂商组件后安装；如果已经有完整 Xfce，可跳过安装：

```bash
sudo apt-get install --no-install-recommends xfce4 xfce4-terminal dbus-x11 xauth fonts-dejavu-core
pkg-config --modversion rockchip_mpp x264 openh264 fuse3 xorg-server
pkg-config --cflags --libs rockchip_mpp
test -x /usr/lib/xorg/Xorg
test -x /usr/bin/startxfce4
test -x /usr/bin/dbus-run-session
```

上游本次构建要求 OpenH264 >= 2.0、FUSE3 >= 3.1；MPP 头文件应能通过 `<rockchip/rk_mpi.h>` 等路径引用。厂商头文件缺失或接口不齐时，先解决 SDK，不能仅复制 `.so`。

## 4. 获取 GitHub main 与固定的上游发行包

下方使用真实公开仓库，无需 GitHub 账号或 SSH 私钥。源码和下载目录统一使用 `xrdp-rkmpp`；源码目录应尚不存在，已有检出请使用第 10 节的更新步骤。克隆 `main` 后固定到实际取得的提交，构建目录与安装目录均由该提交 SHA 生成，不依赖历史测试报告中的私有提交。

```bash
mkdir -p "$HOME/src" "$HOME/downloads/xrdp-rkmpp"
REPOSITORY_URL=https://github.com/zhangjunnan8/xrdp-rkmpp.git
RELEASE_BRANCH=main
git clone --branch "$RELEASE_BRANCH" --single-branch --no-tags \
  "$REPOSITORY_URL" \
  "$HOME/src/xrdp-rkmpp"
cd "$HOME/src/xrdp-rkmpp"
REPO=$PWD
git status --short
test -z "$(git status --porcelain)"
SHA=$(git rev-parse HEAD)
git switch --detach "$SHA"
test -z "$(git status --porcelain)"
printf 'Build source: %s\n' "$SHA"

DOWNLOADS="$HOME/downloads/xrdp-rkmpp"
curl -fL --retry 3 -o "$DOWNLOADS/xrdp-0.10.6.1.tar.gz" \
  https://github.com/neutrinolabs/xrdp/releases/download/v0.10.6.1/xrdp-0.10.6.1.tar.gz
curl -fL --retry 3 -o "$DOWNLOADS/xorgxrdp-0.10.5.tar.gz" \
  https://github.com/neutrinolabs/xorgxrdp/releases/download/v0.10.5/xorgxrdp-0.10.5.tar.gz
(
  cd "$DOWNLOADS"
  sha256sum -c <<'SUMS'
2f7beb5a3b2529c8d72dc0df9b8cdca31ab0e0c14d1e3421210f5e6ec0ab3b75  xrdp-0.10.6.1.tar.gz
a5d03435f0ef48bf3d5010e63d9264f2334e7063cba3ecd8d4c0a15616a4f712  xorgxrdp-0.10.5.tar.gz
SUMS
)
```

下载的是 Release 附件，不是 GitHub 自动生成的 “Source code (tar.gz)”。构建脚本会再次校验 SHA-256，不接受其他上游版本。

板端无法解析或访问 GitHub 时，在可联网电脑下载上述两个附件，再通过 SCP/SFTP 上传到新板的 `$HOME/downloads/xrdp-rkmpp/`，跳过两条 `curl` 命令，但仍须在板端执行同样的 SHA-256 校验。不要用来源不明或版本不同的包替代。

## 5. 原生编译两部分，不启动服务

```bash
XRDP_BUILD="$HOME/build/xrdp-rkmpp/$SHA"
XORG_BUILD="$HOME/build/xrdp-rkmpp-xorg/$SHA"
sh "$REPO/rk3576/build.sh" \
  "$DOWNLOADS/xrdp-0.10.6.1.tar.gz" "$XRDP_BUILD" xrdp-rkmpp
PREFIX=$(cat "$XRDP_BUILD/install-prefix")

XRDP_CFLAGS="-I$XRDP_BUILD/stage$PREFIX/include" \
  sh "$REPO/rk3576/build-xorg.sh" \
  "$DOWNLOADS/xorgxrdp-0.10.5.tar.gz" "$XORG_BUILD" xrdp-rkmpp-xorg
XPREFIX=$(cat "$XORG_BUILD/install-prefix")
cat "$XRDP_BUILD/pipeline-test.log"
cat "$XORG_BUILD/conversion-test.log" "$XORG_BUILD/conversion-ubsan.log"
```

**首次构建时传入 `XRDP_CFLAGS`。** 它指定本轮 stage 的 XRDP 头文件，空白板不需要先安装旧版 XRDP。构建脚本不再覆盖外部 pkg-config 搜索路径；如果使用已安装的匹配开发包，也可按上游方式设置 pkg-config。

两份源码编译为 `/opt/.../<SHA>` 对应的 stage，尚未写入系统 `/opt`。Xorg 模块的安装目录已经显式重定向，不覆盖 `/usr/lib/xorg/modules`。构建目录必须不存在；失败后先查看 `configure.log` / `build.log`，用新的目录重试，不在旧 stage 上混合构建。

## 6. 首次安装独立 3391 服务

**仅在新板且没有同名实例时执行本节，已有安装不能重跑。** 这里直接使用第 5 节的 stage 产物，不依赖另一套 XRDP、旧配置或原维护者的实验账号。不要用 `install-test.py` 或 `install-lab-release.py` 替代：这些历史工具面向既有实验布局，不支持本教程的 `xrdp-rkmpp` 安装。

下方安装块会检查 RK3576、空闲端口、目录和 unit 冲突，从本次 stage 建立配置，生成**本机独立密钥**，安装账号/权限和两个 systemd unit。它只准备文件；启动放在下一节。不要求固定的主机名或桌面用户名。

先选择普通桌面用户：默认用当前 SSH 用户，不能是 root。不要给 XRDP 服务账号 `xrdp` 设置桌面密码。若需要独立办公账号，先自行建立普通用户，再把下方变量改成该用户名。

```bash
DESKTOP_USER=$(id -un)
test "$DESKTOP_USER" != root
id "$DESKTOP_USER"

sudo bash -s -- "$XRDP_BUILD" "$XORG_BUILD" "$DESKTOP_USER" <<'INSTALL'
set -euo pipefail
b=$1
xb=$2
desktop_user=$3
test "$(uname -m)" = aarch64
grep -aq 'rockchip,rk3576' /proc/device-tree/compatible
test "$desktop_user" != root
id "$desktop_user"
p=$(cat "$b/install-prefix")
xp=$(cat "$xb/install-prefix")
sha=$(cat "$b/source-commit")
test "$sha" = "$(cat "$xb/source-commit")"
[[ "$sha" =~ ^[0-9a-f]{40}$ ]]
test "$p" = "/opt/xrdp-rkmpp/$sha"
test "$xp" = "/opt/xrdp-rkmpp-xorg/$sha"
c=/etc/xrdp-rkmpp/xrdp
logs=/var/log/xrdp-rkmpp
test -x "$b/stage$p/sbin/xrdp"
test -f "$xb/stage$xp/lib/xorg/modules/libxorgxrdp.so"
test -f "$xb/xorgxrdp-0.10.5/xrdpdev/xorg.conf"
test -x /usr/lib/xorg/Xorg
for target in /etc/xrdp-rkmpp "$p" "$xp" "$logs" \
  /etc/systemd/system/xrdp-rkmpp.service \
  /etc/systemd/system/xrdp-rkmpp-sesman.service \
  /etc/systemd/system/xrdp-rkmpp.service.d \
  /etc/systemd/system/xrdp-rkmpp-sesman.service.d \
  /etc/tmpfiles.d/xrdp-rkmpp.conf /usr/local/bin/xrdp-rkmpp-motion-test; do
  if [ -e "$target" ] || [ -L "$target" ]; then
    echo "Already exists; stop and inspect: $target" >&2
    exit 1
  fi
done
test ! -e /tmp/.X11-unix/X50
test ! -e /tmp/.X50-lock
python3 -S - <<'PY'
import socket
with socket.socket() as s:
    s.bind(('0.0.0.0', 3391))
PY
# PAM 的服务名是上游固定的；已存在且不同则先人工核对，不能覆盖。
pam="$b/stage/etc/xrdp-rkmpp/pam.d/xrdp-sesman"
test -f "$pam"
if [ -e /etc/pam.d/xrdp-sesman ]; then
  cmp "$pam" /etc/pam.d/xrdp-sesman
fi
getent group xrdp >/dev/null || groupadd --system xrdp
if ! id -u xrdp >/dev/null 2>&1; then
  useradd --system --gid xrdp --home-dir /nonexistent --no-create-home \
    --shell /usr/sbin/nologin xrdp
fi
getent group video >/dev/null
usermod -aG video xrdp
if getent group render >/dev/null; then usermod -aG render xrdp; fi
install -d -m 0755 "$(dirname "$p")" "$(dirname "$xp")"
cp -a "$b/stage$p" "$p"
cp -a "$xb/stage$xp" "$xp"
install -d -m 0755 "$c"
cp -a "$b/stage/etc/xrdp-rkmpp/xrdp/." "$c/"
chown -R root:root "$p" "$xp" "$c"
install -m 0644 "$xb/xorgxrdp-0.10.5/xrdpdev/xorg.conf" "$c/xorg.conf"
install -m 0644 "$pam" /etc/pam.d/xrdp-sesman
install -d -m 0750 -o xrdp -g xrdp "$logs"

python3 -S - "$c" "$xp" <<'PY'
import pathlib, re, sys
c, xp = pathlib.Path(sys.argv[1]), sys.argv[2]

def section(path, name, values=None, body=None):
    text = path.read_text()
    pattern = r'(?ms)^\[' + re.escape(name) + r'\]\n(.*?)(?=^\[|\Z)'
    match = re.search(pattern, text)
    if match is None:
        raise RuntimeError('Missing section: ' + name)
    if body is None:
        body = match.group(1)
        for key, value in values.items():
            body = re.sub(r'(?m)^' + re.escape(key) + r'\s*=.*\n?', '', body)
            body += key + '=' + value + '\n'
    path.write_text(text[:match.start()] + '[' + name + ']\n' + body + '\n' + text[match.end():])

section(c/'xrdp.ini', 'Globals', {
    'port': '3391', 'runtime_user': 'xrdp', 'runtime_group': 'xrdp',
    'certificate': str(c/'cert.pem'), 'key_file': str(c/'key.pem'),
    'autorun': 'Xorg', 'allow_multimon': 'false'})
section(c/'xrdp.ini', 'Logging', {'LogFile': '/var/log/xrdp-rkmpp/xrdp.log', 'LogLevel': 'INFO'})
section(c/'xrdp.ini', 'Xorg', {'h264_frame_interval': '16'})
section(c/'sesman.ini', 'Globals', {
    'EnableUserWindowManager': 'false', 'DefaultWindowManager': str(c/'startwm.sh'),
    'ReconnectScript': str(c/'reconnectwm.sh')})
section(c/'sesman.ini', 'Security', {'AllowRootLogin': 'false', 'SessionSockdirGroup': 'xrdp'})
section(c/'sesman.ini', 'Sessions', {'X11DisplayOffset': '50', 'MaxSessions': '1'})
section(c/'sesman.ini', 'Logging', {'LogFile': '/var/log/xrdp-rkmpp/sesman.log', 'LogLevel': 'INFO'})
section(c/'sesman.ini', 'Xorg', body='\n'.join([
    'param=/usr/lib/xorg/Xorg', 'param=-config', 'param='+str(c/'xorg.conf'),
    'param=-modulepath', 'param='+xp+'/lib/xorg/modules,/usr/lib/xorg/modules',
    'param=-noreset', 'param=-nolisten', 'param=tcp',
    'param=-logfile', 'param=.xorgxrdp.%s.log'])+'\n')
gfx = c/'gfx.toml'
text = gfx.read_text()
if text.count('h264_encoder = "x264"') != 1:
    raise RuntimeError('Unexpected gfx.toml')
gfx.write_text(text.replace('h264_encoder = "x264"', 'h264_encoder = "MPP"'))
PY
cat > "$c/startwm.sh" <<'WM'
#!/bin/sh
unset SESSION_MANAGER DBUS_SESSION_BUS_ADDRESS
export XDG_CURRENT_DESKTOP=XFCE DESKTOP_SESSION=xfce
export XDG_SESSION_DESKTOP=xfce XDG_SESSION_TYPE=x11
export XDG_CONFIG_DIRS=/etc/xdg
exec /usr/bin/dbus-run-session -- /usr/bin/startxfce4
WM
chmod 0755 "$c/startwm.sh" "$c/reconnectwm.sh"
openssl req -x509 -newkey rsa:2048 -nodes -days 365 \
  -subj "/CN=$(hostname)" -keyout "$c/key.pem" -out "$c/cert.pem"
"$p/bin/xrdp-keygen" xrdp "$c/rsakeys.ini" 2048
chown root:xrdp "$c/key.pem" "$c/cert.pem" "$c/rsakeys.ini"
chmod 0640 "$c/key.pem" "$c/cert.pem" "$c/rsakeys.ini"
install -m 0755 "$b/desktop-motion" /usr/local/bin/xrdp-rkmpp-motion-test
install -m 0644 "$b/source-commit" "$c/source-commit"
cat > /etc/tmpfiles.d/xrdp-rkmpp.conf <<'TMP'
d /run/xrdp-rkmpp 0755 root root -
d /run/xrdp-rkmpp/sockdir 3777 root xrdp -
TMP
cat > /etc/systemd/system/xrdp-rkmpp-sesman.service <<UNIT
[Unit]
Description=RK3576 XRDP session manager
After=network.target
[Service]
Type=exec
ExecStartPre=/usr/bin/systemd-tmpfiles --create /etc/tmpfiles.d/xrdp-rkmpp.conf
ExecStart=$p/sbin/xrdp-sesman --nodaemon
Restart=on-failure
[Install]
WantedBy=multi-user.target
UNIT
cat > /etc/systemd/system/xrdp-rkmpp.service <<UNIT
[Unit]
Description=RK3576 MPP XRDP on 3391
Requires=xrdp-rkmpp-sesman.service
After=network.target xrdp-rkmpp-sesman.service
[Service]
Type=exec
Environment=XRDP_MPP_QP=18
Environment=XRDP_MPP_STRICT=1
Environment=XRDP_MPP_DIRECT=1
Environment=XRDP_MPP_PIPELINE=1
ExecStart=$p/sbin/xrdp --nodaemon
Restart=on-failure
[Install]
WantedBy=multi-user.target
UNIT
systemd-tmpfiles --create /etc/tmpfiles.d/xrdp-rkmpp.conf
systemctl daemon-reload
echo 'Files installed. Services have NOT been started or enabled.'
INSTALL
```

安装块不是可反复执行的升级脚本；中途失败时保留输出、检查已写入文件，不要删除冲突目录强行重跑。它使用新生成的本机密钥，并在 `/etc/xrdp-rkmpp/xrdp/source-commit` 记录安装来源。上述目录名与两个构建脚本的显式参数配套，不要只修改 service 名或某一处路径。

## 7. 验证硬件权限、编码与启动

先核对设备权限。当前已验证镜像为 `/dev/mpp_service` 和 `/dev/dma_heap/system` 的 `root:video 0660`；上文给服务账号追加了 video 组。不同镜像应调整为仅服务需要的组访问，不要使用 `chmod 666`，也不要开放 `/dev/dma_heap/reserved`。

```bash
id xrdp
ls -l /dev/mpp_service /dev/dma_heap/system /dev/dri/*
sudo -u xrdp test -r /dev/mpp_service
sudo -u xrdp test -w /dev/mpp_service
sudo -u xrdp test -r /dev/dma_heap/system
sudo -u xrdp test -w /dev/dma_heap/system
ldd "$PREFIX/sbin/xrdp"

# 使用仓库的真实编码回调和 FFmpeg 独立解码；输出目录必须尚不存在。
sudo env XRDP_MPP_DIRECT=1 XRDP_TEST_POISON_ON_READY=1 \
  python3 -S "$REPO/rk3576/verify.py" "$XRDP_BUILD/test-direct" "$XRDP_BUILD/verify-new-board"
sudo "$XRDP_BUILD/test-lifecycle"
sudo systemd-analyze verify /etc/systemd/system/xrdp-rkmpp*.service
sudo systemctl start xrdp-rkmpp-sesman xrdp-rkmpp
systemctl is-active xrdp-rkmpp-sesman xrdp-rkmpp
sudo ss -ltnp '( sport = :3391 )'
```

`ldd` 不能出现 `not found`；正常可见 `librockchip_mpp` 和 x264。库文件名/SONAME 以该板实际链接结果为准，不手工创建 `.so.0`/`.so.1` 替代链接。独立测试使用 root 只为排除测试账号权限影响，**还必须通过实际 RDP 验证服务账号的硬件访问**。

如果权限检查失败且设备节点存在，先检查厂商 udev 规则。可为确认过的这两个节点增加本机规则（只在确实缺失时操作）：

```text
KERNEL=="mpp_service", GROUP="video", MODE="0660"
SUBSYSTEM=="dma_heap", KERNEL=="system", GROUP="video", MODE="0660"
```

保存为 `/etc/udev/rules.d/70-xrdp-rkmpp.rules` 后，reload 规则并只对对应设备触发或在计划维护时重启，再复查权限。不要只用临时 chmod 作为长期配置。

## 8. Windows 连接与确认硬编码

Windows 运行 `mstsc`，填写 **新板实际 IP:3391**，用普通 Linux 用户及其密码登录，选择 **Xorg**。第一次连接会提示自签证书，核对目标板。也可复制 [RDP 示例](../rk3576/RK3576-optimized.rdp)，把 `full address` 和 `username` 改成新板信息；示例使用不可解析的地址且不包含用户名。

防火墙已启用时，仅向需要的局域网放行 3391/TCP；不要直接关闭防火墙。桌面密码由系统账号管理，教程不会设置固定密码。若同一用户已在 HDMI 本地桌面登录，先退出本地会话或使用独立办公账号，避免桌面会话冲突。

在 **RDP 桌面的终端**执行：

```bash
xrdp-rkmpp-motion-test 60 cycle
```

同时在 SSH 终端看日志和编码器状态：

```bash
sudo grep -E 'selected RK3576|first hardware frame complete|switching to x264|MPP:' \
  /var/log/xrdp-rkmpp/xrdp.log | tail -40
sudo cat /proc/mpp_service/rkvenc-core0/sessions-info
grep -Ei 'rkvenc|venc' /proc/interrupts
```

验收至少包括：

1. 本次新连接出现 **`first hardware frame complete`**，而不只是 `selected ... MPP`；运行期间没有 `switching to x264` 或编码错误。
2. 动画期间能观察到 RKVENC 编码活动/中断变化；`/proc` 诊断路径随 BSP 可能不同。Mali GPU 使用率为零不代表 VPU 没编码。
3. 拖动、变色、窗口缩放和断开后重连正常；确认 Xorg 日志没有模块加载或 ABI 错误。
4. 1080p/1440p、需要时 4K 实测流畅度；动画绘制次数不是客户端实际帧率。严格模式只禁止 MPP 失败后的软件回退，客户端仍须协商 H.264，不能用 RFX 会话证明硬编码。

`run-lan-benchmark.py` 等旧实验工具还要求专用客户端、实验服务和目录布局，不能仅填入新板地址就用于本教程的公开安装。首次验收使用上面的动画、日志和实际 RDP 连接即可；历史压测环境见 [无人值守说明](unattended-benchmark.md)。

## 9. 验收通过后启用日常模式

首次启动采用 `XRDP_MPP_STRICT=1`，硬件失败时报错，避免误把软件编码当成功。确认硬编码后，在自己刚安装的新板上退出测试桌面、保存工作，再执行：

```bash
sudo install -d /etc/systemd/system/xrdp-rkmpp.service.d
sudo tee /etc/systemd/system/xrdp-rkmpp.service.d/10-daily.conf >/dev/null <<'UNIT'
[Service]
Environment=XRDP_MPP_STRICT=0
UNIT
sudo systemctl daemon-reload
sudo systemctl restart xrdp-rkmpp
sudo systemctl enable xrdp-rkmpp-sesman xrdp-rkmpp
systemctl show xrdp-rkmpp -p Environment
```

重启会断开该端口连接；新连接才会使用新的环境设置。日常模式保留明确记录的软件回退。后续维护使用 `xrdp-rkmpp` 服务名、配置和日志路径；旧实验安装器不支持本教程的升级。

## 10. 获取后续公开版本

以下步骤只更新源码检出，不会更新正在运行的服务。使用普通用户执行，保留现有工作区改动；只有工作区干净时才切换提交：

```bash
set -euo pipefail
REPO="$HOME/src/xrdp-rkmpp"
cd "$REPO"
git status --short
test -z "$(git status --porcelain)"
test "$(git remote get-url origin)" = https://github.com/zhangjunnan8/xrdp-rkmpp.git
git fetch --no-tags origin refs/heads/main
NEW_SHA=$(git rev-parse FETCH_HEAD)
git log -1 --oneline "$NEW_SHA"
git diff --stat HEAD "$NEW_SHA"
git switch --detach "$NEW_SHA"
SHA=$(git rev-parse HEAD)
DOWNLOADS="$HOME/downloads/xrdp-rkmpp"
printf 'Source ready for build: %s\n' "$SHA"
```

按第 5 节在新目录中构建两部分。若仍是已经构建的同一 SHA，不要清空已有构建目录重复安装。首次安装器会拒绝覆盖现有实例；目前没有经空白板与升级场景完整验收的一键升级工具。

切换已安装版本前，安排会话中断窗口并备份 `/etc/xrdp-rkmpp/`、两个 unit 及其 drop-in 配置。需要成对部署新 XRDP/Xorg 前缀，更新两个 unit 的 `ExecStart`、`sesman.ini` 中的 Xorg `-modulepath` 和动画程序，保留现有证书、PAM、用户与日常配置；先完成独立测试，再重新执行实际 RDP 验收。保留旧前缀和配置备份供回退，验收完成后更新 `source-commit`。不要用首次安装块或旧实验升级脚本覆盖运行中的实例。

## 11. 排错、停用和后续维护

| 现象 | 优先检查 |
|---|---|
| `rockchip_mpp` 找不到/同步函数未定义 | 厂商 MPP 开发包、头文件与运行库一致性；不要升级系统 glibc 绕过 |
| 找不到 xrdp.pc 或报 XRDP 版本不足 | 第 5 节是否传入本轮 stage 的 `XRDP_CFLAGS` |
| 登录后黑屏或马上退出 | `/var/log/xrdp-rkmpp/sesman.log`、用户 `~/.xorgxrdp.50.log`、Xfce/dbus 是否安装 |
| Xorg 模块 ABI/符号错误 | 是否使用同一板端的 Xorg SDK 构建；模块路径是否指向当前 `$XPREFIX` |
| MPP 初始化失败或回退 x264 | 设备权限、BSP/MPP 匹配、对应新连接日志；严格模式下先修复再继续 |
| 3391 无法访问 | systemd 状态、监听、实际 IP、局域网防火墙；SSH 正常不等于 RDP 端口已放行 |
| `Unknown deployment namespace` 或 Xorg 前缀仍是旧实验路径 | 使用包含公开安装修正的 GitHub `main`；第 5 节两个构建命令的第三个参数分别是 `xrdp-rkmpp`、`xrdp-rkmpp-xorg` |

停用本次服务（会断开本次桌面）：

```bash
sudo systemctl disable --now xrdp-rkmpp xrdp-rkmpp-sesman
```

保留 `/opt` 版本目录、配置和构建日志以便排错，不需要删除系统桌面或驱动。若要临时改软件编码，备份 `gfx.toml` 后把 `h264_encoder = "MPP"` 改成 `"x264"`，退出旧桌面并建立新会话，确认日志；这不等同于已验收硬编码。

后续系统更新应单独审查 Xorg、Mesa、libdrm、MPP、Mali、RGA、内核及开发头文件。不要因为软件包管理器标记“自动安装且无依赖”就删除自编译工程使用的库。本文不部署音频重定向模块，也不承诺多用户、多屏、Wayland、GPU 桌面渲染或所有分辨率 60 FPS。

## 参考与本次文档校验

原版教程在 2026-10-11 完成了 Bash/Python 示例检查、基于历史验收产物的配置生成、unit 校验和 stage 头文件配置验证。那些记录使用的是当时的实验安装名称，不能当作当前公开名称已经在新板部署成功的证明。

本次公开安装修正的检查范围见 [公开安装参数校验](validation/2026-10-11-public-install.md)。构建参数、配置生成与链接在本地校验；未在第二块空白板完成全流程安装，没有重启现有服务，也没有 Ubuntu 22.04/20.04 的硬件运行记录。

- [XRDP 0.10.6.1 官方发行版](https://github.com/neutrinolabs/xrdp/releases/tag/v0.10.6.1)、[xorgxrdp 0.10.5 官方发行版](https://github.com/neutrinolabs/xorgxrdp/releases/tag/v0.10.5)。
- [xorgxrdp 上游构建说明](https://github.com/neutrinolabs/xorgxrdp/tree/v0.10.5)：支持用 `XRDP_CFLAGS` 指定头文件而不预先安装 XRDP。
- [Ubuntu 版本维护周期](https://ubuntu.com/about/release-cycle)；厂商 BSP 的维护周期需要另行向厂商确认。
- MPP 包依赖、设备权限和配置布局取自现有板 2026-10-11 的只读检查；并非对所有厂商镜像的保证。

历史验收报告保留原始提交、路径与服务名，用于解释测试来源。安装时以本文的 GitHub `main`、当前实际 SHA 和 `xrdp-rkmpp` 路径为准，不按历史报告检出或安装私有版本。
