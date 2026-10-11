# Isolated desktop benchmark / 隔离桌面测试

The synthetic application creates real X11 windows, movement, tables, small text and changing pixels. The default cycle spends 20 seconds each on movement, color changes and both. It exercises composition, capture, conversion, encoding, transport and decoding; it does not simulate disk I/O or measure physical input latency. Animation draw counts are not encoded FPS.

## Session isolation

The current helpers are deliberately restricted to **TCP 3391 / Xorg :50** and one connected benchmark client. The separate decoding host uses its own **Xvfb :97** and FreeRDP. `profile-session.py` checks the actual client IP, XRDP PID, connected Xorg PID and completed hardware frame before drawing. It rejects another display, software fallback or unexpected clients.

3391 必须是隔离测试实例。如果它已用于办公，不要运行这些工具。旧 `rdp-soak.py` 面向 3390 / :30，不适合代替当前测试。改端口需同步审核相关工具，不要只修改一个数字。

## Prepare the independent client

The controller requires Python 3 and Paramiko. Verify both SSH host keys out of band and save them to the controller's standard `known_hosts`; unknown or changed keys are rejected. It logs into the board as root for tracing, and into the decoder host as a normal benchmark account. No SSH or RDP password is stored by these tools.

The decoder host must be Linux x86_64 and already contain this private client layout (replace `CLIENT_BASE` with your own directory):

```text
CLIENT_BASE/
  root/usr/bin/Xvfb
  root/usr/lib/x86_64-linux-gnu/    # dependencies of the private packages
  freerdp-ffmpeg-install/bin/xfreerdp
  freerdp-ffmpeg-install/lib/
  test-desktop                   # compile test_desktop.c on this client host
```

The recorded measurements used **FreeRDP 2.11.5 with H.264/FFmpeg enabled**. Some distribution builds disable AVC support. Check the actual client's `/buildconfig` and negotiated codec. These private packages are not supplied or automatically installed. Build the helper natively on the decoder host:

```sh
cc -O2 -Wall -Wextra -Werror /path/to/rk3576/test_desktop.c -lX11 -o "$CLIENT_BASE/test-desktop"
```

The client uses certificate trust-on-first-use (`/cert:tofu`), not blanket certificate ignore. Establish and verify the test endpoint's certificate before unattended runs; certificate changes may require interactive review. The first-use trust model is not a substitute for checking the intended endpoint.

## Configure and run

Copy [the example JSON](../examples/lab.example.json) to `.local/lab.json` on the controller and replace **every site value**. The example uses RFC 5737 documentation addresses and role names; it cannot target the original development network. Never commit the populated file.

| Key | Meaning |
|---|---|
| `board_host` | Board IPv4 address or DNS name, reachable from controller and client |
| `board_hostname` | Exact `hostname` on the expected RK3576 board |
| `board_repo` | Absolute board source checkout; must contain the current helpers |
| `board_build_root` | Allowed parent of the native XRDP build directory |
| `board_results_root` | Existing parent where new `bench-LABEL` results are written |
| `client_host`, `client_user` | SSH endpoint/account on the independent decoder |
| `client_ip` | Decoder IPv4 address as observed by the board's TCP connection |
| `client_base` | Absolute directory with the private client layout above |
| `rdp_user` | Existing non-root desktop user on the board |

The example directories must be replaced or created deliberately. Record the same source SHA on the controller and board. The CLI validates configuration and board identity before starting its test client. Password input is interactive by default or via stdin with `--password-stdin`, never a command-line password.

```sh
python rk3576/run-lan-benchmark.py /absolute/board/build/SHA run-1440 \
  --config .local/lab.json
python rk3576/run-lan-benchmark.py /absolute/board/build/SHA resize-run \
  --config .local/lab.json --resize-soak --seconds 60
```

The second command keeps one connection through 1440p → 1080p → 4K → 1080p → 1440p. It closes only its own FreeRDP and Xvfb processes. Client lifetime is capped at 30 minutes. Results download to `.local/validation/LABEL/`; board raw output remains under `board_results_root`. Inspect logs before rerunning a failed label; directories are not silently overwritten.

The profiler creates and removes its own tracefs instance. Reports exclude the first 2 seconds of each phase and include encoded FPS, capture/encode durations, ACKs, CPU, RSS and fd counts. Success requires hardware activity, zero encoder return errors, no runtime fallback and no trace loss. Memory/queue observations still require interpretation; finite success is not an unlimited stability guarantee.

## Legacy helpers and service changes

Board-side lab scripts require `XRDP_LAB_HOSTNAME` and (except the first-install helper) `XRDP_LAB_USER`. Profiling also requires `XRDP_LAB_CLIENT`; local loopback tests must explicitly set it to `127.0.0.1`. Legacy on-board client scripts require `XRDP_LAB_CLIENT_TOOLS`, pointing to the prepared private tools root.

These parameters remove site information from source; they do not make old installers universal. `install-test.py` still requires a pre-existing pilot configuration/PAM/modules. `install-lab-release.py` expects existing lab units, a versioned animation symlink and display :50. It can stop that instance; use the [fresh-board recipe](new-board-install.md) for new installations.

本次公开整理仅对参数和保护逻辑做隔离检查，没有为了证明脚本可运行而重新连接正在办公的 RDP 会话。历史性能数据来自原已验证的固定现场配置，移植后的自动化脚本仍需在新隔离环境完成端到端复测。
