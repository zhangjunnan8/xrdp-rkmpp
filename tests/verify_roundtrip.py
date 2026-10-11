"""Verify actual H.264 output with an independent FFmpeg decoder."""
import json
import math
import pathlib
import re
import subprocess
import sys
import tempfile

encoder, plugin, ffmpeg, ffprobe = sys.argv[1:]


def run(args):
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {args}\n{result.stderr}")
    return result.stdout


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


results = []
with tempfile.TemporaryDirectory(prefix="codecbridge-roundtrip-") as tmp:
    root = pathlib.Path(tmp)
    for index, (width, height, count, padding, color_range, matrix) in enumerate([
        (320, 240, 12, 0, "full", "bt709"),
        (318, 242, 12, 30, "limited", "bt601"),
        (1920, 1080, 8, 16, "full", "bt709"),
    ]):
        stream = root / f"case-{index}.h264"
        reference = root / f"case-{index}.nv12"
        decoded = root / f"case-{index}.yuv"
        metrics = json.loads(run([
            encoder, "encode", "--plugin", plugin, "--output", str(stream),
            "--raw-output", str(reference), "--width", str(width), "--height", str(height),
            "--frames", str(count), "--padding", str(padding), "--qp", "20",
            "--keyframe-every", "5", "--range", color_range, "--matrix", matrix,
        ]))
        require(metrics["frames"] == count and not metrics["hardware"], "wrong encoder identity/count")
        info = json.loads(run([
            ffprobe, "-v", "error", "-show_streams", "-show_frames", "-of", "json",
            "-show_entries", "stream=codec_name,width,height,has_b_frames,color_range,color_space:frame=key_frame,pict_type",
            str(stream),
        ]))
        video = info["streams"][0]
        require(video["codec_name"] == "h264", "not H.264")
        require((video["width"], video["height"]) == (width, height), "wrong coded dimensions")
        require(video["has_b_frames"] == 0, "unexpected reordering")
        require(video["color_range"] == ("pc" if color_range == "full" else "tv"), "wrong color range")
        require(video["color_space"] == ("bt709" if matrix == "bt709" else "smpte170m"), "wrong color matrix")
        pictures = info["frames"]
        require(len(pictures) == count, "lost/extra decoded frames")
        for i, picture in enumerate(pictures):
            require(picture["pict_type"] != "B", "B-frame present")
            if i % 5 == 0:
                require(picture["key_frame"] == 1, "keyframe request not honored")
        headers = set()
        for nal in re.split(b"\x00\x00(?:\x00)?\x01", stream.read_bytes()):
            if not nal:
                continue
            kind = nal[0] & 31
            if kind == 9:
                headers.clear()
            if kind in (7, 8):
                headers.add(kind)
            if kind == 5:
                require(headers == {7, 8}, "IDR is missing repeated SPS/PPS")
        scale_range = "full" if color_range == "full" else "limited"
        run([ffmpeg, "-v", "error", "-nostdin", "-i", str(stream),
             "-vf", f"scale=in_range={scale_range}:out_range={scale_range}",
             "-pix_fmt", "yuv420p", "-f", "rawvideo", str(decoded)])
        original = reference.read_bytes()
        actual = decoded.read_bytes()
        pixels = width * height
        frame_bytes = pixels * 3 // 2
        require(len(original) == len(actual) == frame_bytes * count, "decoded size differs")
        squared_error = 0
        for i in range(count):
            start = i * frame_bytes
            y = original[start:start + pixels]
            uv = original[start + pixels:start + frame_bytes]
            planar = y + uv[0::2] + uv[1::2]
            reconstructed = actual[start:start + frame_bytes]
            squared_error += sum((a - b) ** 2 for a, b in zip(planar, reconstructed))
        mse = squared_error / len(actual)
        psnr = 10 * math.log10(255 ** 2 / mse) if mse else 100.0
        require(psnr >= 32, f"round-trip distortion too high: {psnr:.2f} dB")
        results.append({"width": width, "height": height, "frames": count,
                        "padding": padding, "range": color_range, "matrix": matrix,
                        "psnr_db": round(psnr, 3), "encode_p95_ms": metrics["encode_p95_ms"]})

    bad = subprocess.run([encoder, "probe", str(root / "missing.so")], capture_output=True)
    require(bad.returncode != 0, "missing requested plugin silently fell back")
print(json.dumps({"roundtrip": results}, indent=2))
