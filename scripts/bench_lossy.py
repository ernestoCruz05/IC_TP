#!/usr/bin/env python3
"""Compare the project DCT-II codec with FFmpeg's libmp3lame encoder.

Use --target-bitrates to evaluate wav_lossy's target-rate search. Encoding
times in that mode include the repeated trial encodes used to select q.
"""

import argparse
import math
import os
import platform
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path


class BenchError(Exception):
    pass


def read_pcm(path):
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        width = wav.getsampwidth()
        rate = wav.getframerate()
        frames = wav.getnframes()
        if wav.getcomptype() != "NONE" or width not in (1, 2, 3, 4):
            raise BenchError(f"unsupported WAV PCM format: {path}")
        raw = wav.readframes(frames)
    sample_count = len(raw) // width
    if width == 1:
        values = [byte - 128 for byte in raw]
    elif width == 2:
        values = list(struct.unpack("<" + "h" * sample_count, raw))
    elif width == 3:
        values = []
        for offset in range(0, len(raw), 3):
            value = raw[offset] | (raw[offset + 1] << 8) | (raw[offset + 2] << 16)
            values.append(value - (1 << 24) if value & (1 << 23) else value)
    else:
        values = list(struct.unpack("<" + "i" * sample_count, raw))
    scale = float(1 << (width * 8 - 1))
    return channels, rate, frames, [value / scale for value in values]


def accumulate_metrics(source, restored, totals):
    src_ch, src_rate, src_frames, src = read_pcm(source)
    out_ch, out_rate, out_frames, out = read_pcm(restored)
    if (src_ch, src_rate, src_frames) != (out_ch, out_rate, out_frames):
        raise BenchError(
            f"decoded WAV format mismatch for {source.name}: "
            f"source={src_ch}ch/{src_rate}Hz/{src_frames} frames, "
            f"decoded={out_ch}ch/{out_rate}Hz/{out_frames} frames"
        )
    if len(src) != len(out):
        raise BenchError(f"sample count mismatch for {source.name}")
    if totals["channels"] not in (0, src_ch):
        raise BenchError("input files have different channel counts")
    totals["channels"] = src_ch
    if len(totals["signal"]) == 0:
        totals["signal"] = [0.0] * (src_ch + 1)
        totals["error"] = [0.0] * (src_ch + 1)
    for frame in range(src_frames):
        src_sum = 0.0
        out_sum = 0.0
        for channel in range(src_ch):
            index = frame * src_ch + channel
            x, y = src[index], out[index]
            totals["signal"][channel] += x * x
            totals["error"][channel] += (x - y) ** 2
            src_sum += x
            out_sum += y
        x_mid, y_mid = src_sum / src_ch, out_sum / src_ch
        totals["signal"][src_ch] += x_mid * x_mid
        totals["error"][src_ch] += (x_mid - y_mid) ** 2
    totals["frames"] += src_frames
    totals["duration"] += src_frames / src_rate


def snr_db(signal, error):
    if error == 0:
        return "+inf"
    if signal == 0:
        return "-inf"
    return f"{10.0 * math.log10(signal / error):.2f}"


def run_measured(command, output, rss_file, gnu_time):
    wrapped = [*command]
    if gnu_time:
        wrapped = [gnu_time, "-f", "%M", "-o", str(rss_file), *wrapped]
    sink_path = output if output is not None else os.devnull
    with open(sink_path, "wb") as sink:
        start = time.perf_counter()
        completed = subprocess.run(wrapped, stdin=subprocess.DEVNULL,
                                   stdout=sink, stderr=subprocess.PIPE)
        elapsed = time.perf_counter() - start
    if completed.returncode != 0:
        diagnostic = completed.stderr.decode(errors="replace").strip()
        raise BenchError(f"{command[0]} failed ({completed.returncode}): {diagnostic}")
    rss = int(rss_file.read_text().strip()) if gnu_time else None
    return elapsed, rss


def run_candidate(name, files, work, runs, encoder, decoder, gnu_time):
    print(f"Running {name}...", file=sys.stderr, flush=True)
    totals = {"channels": 0, "signal": [], "error": [], "frames": 0,
              "duration": 0.0}
    result = {"name": name, "size": 0, "encode": 0.0, "decode": 0.0,
              "encode_rss": None, "decode_rss": None, "metrics": totals}
    packed = work / "packed.bin"
    restored = work / "restored.wav"
    for source in files:
        best_encode = math.inf
        best_decode = math.inf
        for run in range(runs):
            packed.unlink(missing_ok=True)
            restored.unlink(missing_ok=True)
            elapsed, rss = run_measured(encoder(source, packed), None,
                                       work / f"rss-{run}", gnu_time)
            best_encode = min(best_encode, elapsed)
            if rss is not None:
                result["encode_rss"] = max(result["encode_rss"] or 0, rss)
            elapsed, rss = run_measured(decoder(packed, restored), None,
                                       work / f"rss-{run}", gnu_time)
            best_decode = min(best_decode, elapsed)
            if rss is not None:
                result["decode_rss"] = max(result["decode_rss"] or 0, rss)
            accumulate_metrics(source, restored, totals)
        result["size"] += packed.stat().st_size
        result["encode"] += best_encode
        result["decode"] += best_decode
    return result


def mib(kib):
    return "n/a" if kib is None else f"{kib / 1024.0:.1f}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--enc", default="build-rel/src/wav_lossy_enc")
    parser.add_argument("--dec", default="build-rel/src/wav_lossy_dec")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--n", type=int, default=1024)
    parser.add_argument("--q", type=int, default=64)
    parser.add_argument("--target-bitrates", default="",
                        help="comma-separated wav_lossy target rates in kbps")
    parser.add_argument("--bitrates", default="256k,320k")
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()
    if args.runs < 1 or args.n < 256 or args.n > 4096 or args.n & (args.n - 1):
        parser.error("runs must be positive and n must be a power of two from 256 to 4096")
    if not 0 <= args.q <= 240:
        parser.error("q must be between 0 and 240")
    for binary in (args.enc, args.dec, args.ffmpeg):
        if not (Path(binary).is_file() if "/" in binary else shutil.which(binary)):
            parser.error(f"executable not found: {binary}")
    files = [path.resolve() for path in args.files]
    if any(not path.is_file() for path in files) or len(files) != len(set(files)):
        parser.error("all input files must exist and be distinct")
    bitrates = [item.strip() for item in args.bitrates.split(",") if item.strip()]
    if not bitrates:
        parser.error("provide at least one MP3 bitrate")
    try:
        target_bitrates = [int(item.strip()) for item in
                           args.target_bitrates.split(",") if item.strip()]
    except ValueError:
        parser.error("wav_lossy target bitrates must be comma-separated integers")
    if any(rate < 8 or rate > 100000 for rate in target_bitrates):
        parser.error("wav_lossy target bitrates must be between 8 and 100000 kbps")
    original_size = sum(path.stat().st_size for path in files)
    duration = 0.0
    for path in files:
        _, sample_rate, frames, _ = read_pcm(path)
        duration += frames / sample_rate
    gnu_time = shutil.which("time")
    ffmpeg_version = subprocess.run([args.ffmpeg, "-version"], capture_output=True,
                                    text=True, check=True).stdout.splitlines()[0]
    encoders = subprocess.run([args.ffmpeg, "-hide_banner", "-encoders"],
                              capture_output=True, text=True, check=True).stdout
    if "libmp3lame" not in encoders:
        parser.error("this FFmpeg build does not provide libmp3lame")

    print(f"{platform.platform()}; Python {platform.python_version()}")
    cpu = platform.processor()
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.is_file():
        cpu = next((line.split(":", 1)[1].strip()
                    for line in cpuinfo.read_text().splitlines()
                    if line.startswith("model name")), cpu)
    print(f"{cpu}; {os.cpu_count()} logical CPUs; {ffmpeg_version}")
    print(f"{len(files)} files, {original_size} source bytes, {duration:.2f} seconds")
    print("MP3 uses libmp3lame CBR; ffmpeg's Xing/LAME gapless metadata is retained.")
    if not gnu_time:
        print("GNU time not found; memory will be n/a", file=sys.stderr)

    results = []
    with tempfile.TemporaryDirectory(prefix="bench-lossy-") as temp:
        work = Path(temp)
        results.append(run_candidate(
            f"wav_lossy DCT-II N={args.n} q={args.q}", files, work, args.runs,
            lambda source, target: [args.enc, "-n", str(args.n),
                                    "-q", str(args.q), str(source), str(target)],
            lambda source, target: [args.dec, str(source), str(target)], gnu_time))
        for target_bitrate in target_bitrates:
            results.append(run_candidate(
                f"wav_lossy DCT-II N={args.n} target={target_bitrate} kbps",
                files, work, args.runs,
                lambda source, target, rate=target_bitrate: [
                    args.enc, "-n", str(args.n), "-b", str(rate),
                    str(source), str(target)],
                lambda source, target: [args.dec, str(source), str(target)], gnu_time))
        for bitrate in bitrates:
            results.append(run_candidate(
                f"MP3 CBR {bitrate}", files, work, args.runs,
                lambda source, target, br=bitrate: [
                    args.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error",
                    "-y", "-i", str(source), "-map_metadata", "-1", "-threads", "1",
                    "-c:a", "libmp3lame", "-b:a", br, "-f", "mp3", str(target)],
                lambda source, target: [
                    args.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error",
                    "-y", "-i", str(source), "-threads", "1", "-c:a", "pcm_s16le",
                    str(target)], gnu_time))

    labels = ["SNR L (dB)", "SNR R (dB)", "SNR MID (dB)"]
    print(f"\nBest of {args.runs} runs per file; times include process startup and file I/O.")
    print("RSS is the maximum GNU time peak across files/runs; SNR is aggregated by energy.")
    print("| Codec | Bytes | Ratio | Rate (kbps) | Encode (s) | Decode (s) | Encode RSS (MiB) | Decode RSS (MiB) | "
          + " | ".join(labels) + " |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for result in results:
        metrics = result["metrics"]
        snrs = [snr_db(metrics["signal"][i], metrics["error"][i])
                for i in range(metrics["channels"] + 1)]
        rate = result["size"] * 8.0 / duration / 1000.0 if duration else 0.0
        print(f"| {result['name']} | {result['size']} | "
              f"{100.0 * result['size'] / original_size:.2f}% | {rate:.1f} | "
              f"{result['encode']:.3f} | {result['decode']:.3f} | "
              f"{mib(result['encode_rss'])} | {mib(result['decode_rss'])} | "
              + " | ".join(snrs) + " |")
    print(f"\nFFmpeg: {ffmpeg_version}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (BenchError, OSError, subprocess.SubprocessError, wave.Error) as error:
        print(f"bench_lossy: {error}", file=sys.stderr)
        sys.exit(1)
