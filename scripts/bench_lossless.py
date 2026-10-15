#!/usr/bin/env python3
import argparse
import filecmp
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def plain(*base):
    return lambda source, target: ([*base, str(source), str(target)], None)


def flac(*base):
    return lambda source, target: (
        ["flac", "-s", "-f", "--keep-foreign-metadata", *base, "-o",
         str(target), str(source)], None)


def piped(*base):
    return lambda source, target: ([*base, str(source)], target)


def wavpack(*base):
    return lambda source, target: (
        [*base, "-q", "-y", str(source), "-o", str(target)], None)


def flac_has_threads():
    if not shutil.which("flac"):
        return False
    usage = subprocess.run(["flac", "--help"], capture_output=True,
                           text=True).stdout
    return "--threads" in usage


def available(program):
    return shutil.which(program) is not None


def tools(enc, dec, threads):
    threaded = flac_has_threads()
    single = ["-j1"] if threaded else []
    settings = [()]
    for flag, values in [("-b", (1024, 2048, 8192, 16384)),
                         ("-l", (0, 8, 24, 32)), ("-q", (8, 10, 15)),
                         ("-p", (0, 6, 8))]:
        settings.extend((flag, str(value)) for value in values)
    settings.append(("-l", "32", "-p", "8"))
    candidates = []
    for options in settings:
        label = " ".join(("wav_lossless", *options, "-j1"))
        candidates.append((label, plain(enc, *options, "-j", "1"),
                           plain(dec, "-j", "1")))
    if threads > 1:
        for options in [(), ("-l", "32", "-p", "8")]:
            label = " ".join(("wav_lossless", *options, f"-j{threads}"))
            candidates.append((label, plain(enc, *options, "-j", str(threads)),
                               plain(dec, "-j", str(threads))))
    for level in ["-0", "-5", "-8"]:
        label = " ".join(("flac", level, *single))
        candidates.append((label, flac(level, *single), flac("-d")))
    if threaded and threads > 1:
        for level in ["-5", "-8"]:
            candidates.append((f"flac {level} -j{threads}",
                               flac(level, f"-j{threads}"), flac("-d")))
    for level in ["-6", "-9e"]:
        candidates.append((f"xz {level} -T1", piped("xz", level, "-T1", "-c"),
                           piped("xz", "-d", "-T1", "-c")))
    for level in ["-3", "-19"]:
        candidates.append((f"zstd {level} -T1", piped("zstd", level, "-T1", "-q", "-c"),
                           piped("zstd", "-d", "-q", "-c")))
    candidates.append(("wavpack", wavpack("wavpack"), wavpack("wvunpack")))
    candidates.append(("wavpack -hh", wavpack("wavpack", "-hh"),
                       wavpack("wvunpack")))
    candidates.append(("bzip2 -9", piped("bzip2", "-9", "-c"),
                       piped("bzip2", "-d", "-c")))
    candidates.append(("gzip -9", piped("gzip", "-9", "-c"),
                       piped("gzip", "-d", "-c")))

    found = []
    for name, encode, decode in candidates:
        missing = [program for program in (encode("in", "out")[0][0],
                                           decode("in", "out")[0][0])
                   if not available(program)]
        if missing:
            print(f"skipping {name}: {', '.join(missing)} not found",
                  file=sys.stderr)
        else:
            found.append((name, encode, decode))
    return found


class ToolError(Exception):
    pass


GNU_TIME = shutil.which("time")


def run(command, output, rss_file):
    program = command[0]
    if GNU_TIME:
        command = [GNU_TIME, "-f", "%M", "-o", str(rss_file), *command]
    with open(output if output else os.devnull, "wb") as sink:
        start = time.perf_counter()
        status = subprocess.run(command, stdin=subprocess.DEVNULL,
                                stdout=sink, stderr=subprocess.PIPE)
        elapsed = time.perf_counter() - start
    if status.returncode != 0:
        diagnostic = status.stderr.decode(errors="replace").strip()
        raise ToolError(f"{program}: exit status {status.returncode}: {diagnostic}")
    if not GNU_TIME:
        return elapsed, None
    return elapsed, int(rss_file.read_text().split()[-1])


def larger(a, b):
    return b if a is None else a if b is None else max(a, b)


def megabytes(kilobytes):
    return "n/a" if kilobytes is None else f"{kilobytes / 1024:.1f}"


def measure(encode, decode, files, work, runs):
    result = {"size": 0, "encode": 0.0, "decode": 0.0, "encode_rss": None,
              "decode_rss": None, "exact": True, "error": None}
    packed = work / "packed.wv"
    restored = work / "restored.wav"
    for path in files:
        best_encode = float("inf")
        best_decode = float("inf")
        try:
            for _ in range(runs):
                packed.unlink(missing_ok=True)
                restored.unlink(missing_ok=True)
                elapsed, rss = run(*encode(path, packed), work / "rss")
                best_encode = min(best_encode, elapsed)
                result["encode_rss"] = larger(result["encode_rss"], rss)
                elapsed, rss = run(*decode(packed, restored), work / "rss")
                best_decode = min(best_decode, elapsed)
                result["decode_rss"] = larger(result["decode_rss"], rss)
                filecmp.clear_cache()
                if not filecmp.cmp(path, restored, shallow=False):
                    result["exact"] = False
                    raise ToolError("round trip differs")
            result["size"] += packed.stat().st_size
        except (ToolError, OSError) as error:
            result["error"] = f"failed on {path.name} ({error})"
            return result
        result["encode"] += best_encode
        result["decode"] += best_decode
    return result


def ratio(size, original):
    return f"{100 * size / original:.2f}%" if original else "n/a"


def environment(args, candidates):
    print(f"{platform.platform()}; Python {platform.python_version()}")
    cpuinfo = Path("/proc/cpuinfo")
    cpu = platform.processor()
    if cpuinfo.is_file():
        cpu = next((line.split(":", 1)[1].strip()
                    for line in cpuinfo.read_text().splitlines()
                    if line.startswith("model name")), cpu)
    print(f"{cpu}; {os.cpu_count()} logical CPUs; --threads {args.threads}\n")
    programs = {build("INPUT", "OUTPUT")[0][0]
                for _, encode, decode in candidates for build in (encode, decode)}
    programs.difference_update((args.enc, args.dec))
    if GNU_TIME:
        programs.add(GNU_TIME)
    for program in sorted(programs):
        version = subprocess.run([program, "--version"], stdin=subprocess.DEVNULL,
                                 capture_output=True)
        text = (version.stderr or version.stdout).decode(errors="replace")
        lines = text.strip().splitlines()
        print(f"- {program}: {lines[0] if lines else 'version unavailable'}")
    print("\nFLAC uses --keep-foreign-metadata and single-threaded decoding. "
          "xz/zstd use -T1 (zstd also has I/O threads).\n")


def main():
    parser = argparse.ArgumentParser(
        description="Compare wav_lossless with flac and general compressors.")
    parser.add_argument("--enc", default="build-rel/src/wav_lossless_enc")
    parser.add_argument("--dec", default="build-rel/src/wav_lossless_dec")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--threads", type=int, default=min(os.cpu_count() or 1, 64))
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()

    if args.runs < 1:
        parser.error("--runs must be at least 1")
    if not 1 <= args.threads <= 64:
        parser.error("--threads must be between 1 and 64")
    for binary in (args.enc, args.dec):
        if not available(binary):
            parser.error(f"executable not found: {binary}")
    args.files = [path.resolve() for path in args.files]
    for path in args.files:
        if not path.is_file():
            parser.error(f"input file not found: {path}")
    if len(args.files) != len(set(args.files)):
        parser.error("input files must be distinct")
    original = sum(path.stat().st_size for path in args.files)
    candidates = tools(args.enc, args.dec, args.threads)
    environment(args, candidates)
    if not GNU_TIME:
        print("GNU time not found (package 'time'): memory is not measured",
              file=sys.stderr)
    results = []
    with tempfile.TemporaryDirectory(prefix="bench-") as temp:
        for name, encode, decode in candidates:
            print(f"{name}...", file=sys.stderr)
            results.append((name, measure(encode, decode, args.files,
                                          Path(temp), args.runs)))

    print(f"{len(args.files)} files, {original} bytes. Times are the best of "
          f"{args.runs} runs per file, summed over the files, including process "
          "startup and file I/O. Every round trip is checked outside the timer. "
          "RSS is the largest peak over all files and runs, measured by GNU "
          "time (KiB converted to MiB).\n")
    failed = [(name, r) for name, r in results if r["error"]]
    results = [(name, r) for name, r in results if not r["error"]]
    print("| Tool | Size (bytes) | Ratio | Encode (s) | Decode (s) | "
          "Encode RSS (MiB) | Decode RSS (MiB) | Round trip |")
    print("|---|---|---|---|---|---|---|---|")
    for name, r in results:
        print(f"| {name} | {r['size']} | {ratio(r['size'], original)} "
              f"| {r['encode']:.3f} | {r['decode']:.3f} "
              f"| {megabytes(r['encode_rss'])} | {megabytes(r['decode_rss'])} "
              f"| {'exact' if r['exact'] else 'differs'} |")

    for name, r in failed:
        print(f"\n{name}: {r['error']}")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
