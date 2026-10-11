#!/usr/bin/env python3
"""Independent FFmpeg decoding of actual XRDP callback output; synthetic data."""
import json
import os
import pathlib
import re
import subprocess
import sys

binary = pathlib.Path(sys.argv[1]).resolve()
out = pathlib.Path(sys.argv[2]).resolve()
out.mkdir(exist_ok=False)
results = []
cases = [("partial-small", 320, 240, 20, 18, False),
         ("partial-unaligned", 318, 242, 20, 18, False),
         ("partial-1080", 1920, 1080, 120, 18, False),
         ("full-1080-qp18", 1920, 1080, 120, 18, True),
         ("full-1080-qp20", 1920, 1080, 120, 20, True),
         ("full-1080-qp23", 1920, 1080, 120, 23, True),
         ("partial-1440", 2560, 1440, 60, 18, False),
         ("full-1440", 2560, 1440, 60, 18, True),
         ("partial-4k", 3840, 2160, 20, 18, False)]
for name, w, h, n, qp, full in cases:
    prefix = out / name
    encoded, raw = str(prefix)+".h264", str(prefix)+".nv12"
    env = dict(os.environ, XRDP_MPP_STRICT="1", XRDP_MPP_QP=str(qp))
    if full:
        env["XRDP_TEST_FULL_FRAME"] = "1"
    if name == "partial-small":
        env["XRDP_TEST_IDLE_AT"] = "10"
    p = subprocess.run([str(binary), "mpp", str(w), str(h), str(n), encoded, raw],
                       env=env, text=True, capture_output=True, timeout=90)
    pathlib.Path(str(prefix)+".log").write_text(p.stdout+p.stderr)
    assert p.returncode == 0, p.stdout+p.stderr
    assert "first hardware frame complete" in p.stdout+p.stderr
    assert "switching to x264" not in p.stdout+p.stderr
    meta = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-show_streams",
                      "-show_frames", "-of", "json", encoded], text=True))
    stream, frames = meta["streams"][0], meta["frames"]
    assert stream["width"] == (w+15)&~15 and stream["height"] == (h+15)&~15
    assert stream["has_b_frames"] == 0 and stream["color_range"] == "pc"
    assert stream["color_space"] == "bt709" and len(frames) == n
    assert frames[0]["key_frame"] == 1
    if name == "partial-small":
        assert frames[10]["key_frame"] == 1
    quality = subprocess.run(["ffmpeg", "-hide_banner", "-nostdin", "-xerror", "-i", encoded,
        "-f", "rawvideo", "-pixel_format", "nv12", "-video_size", f"{w}x{h}",
        "-framerate", "60", "-color_range", "pc", "-i", raw, "-filter_complex",
        f"[0:v]crop={w}:{h}:0:0,scale=in_range=full:out_range=full,format=yuv420p[a];"
        "[1:v]scale=in_range=full:out_range=full,format=yuv420p[b];[a][b]psnr",
        "-frames:v", str(n), "-f", "null", "-"],
        capture_output=True, text=True, timeout=90)
    pathlib.Path(str(prefix)+"-decode.log").write_text(quality.stderr)
    assert quality.returncode == 0, quality.stderr
    psnr = float(re.search(r"average:([\d.]+)", quality.stderr)[1])
    assert psnr > 35, (name, psnr)
    result = dict(name=name, width=w, height=h, frames=n, qp=qp, psnr=psnr,
                  metrics=next(line for line in p.stdout.splitlines() if line.startswith("RESULT")))
    results.append(result)
    print(json.dumps(result), flush=True)
    # These raw synthetic reference files are reproducible and large.
    pathlib.Path(raw).unlink()
(out / "results.json").write_text(json.dumps(results, indent=2)+"\n")
