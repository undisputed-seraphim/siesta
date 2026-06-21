#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Concurrent load test for the siesta echo server.

Sends GET /echo?message=... requests over persistent HTTP/1.1 connections
using a thread pool. Each worker opens one connection and sends its share
of requests over it (keep-alive). Reports latency percentiles and RPS.

Usage:
    python3 load_test.py [--host HOST] [--port PORT]
                         [--requests N] [--concurrency C]
                         [--warmup N] [--no-keepalive]
"""

import argparse
import socket
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

MESSAGE = "hello_load_test_1234567890"


def make_http_request(msg: str, keepalive: bool = True) -> bytes:
    conn = "keep-alive" if keepalive else "close"
    return (
        f"GET /echo?message={msg} HTTP/1.1\r\n"
        f"Host: localhost\r\n"
        f"Connection: {conn}\r\n"
        "\r\n"
    ).encode()


def read_response(s: socket.socket, buf: bytearray) -> tuple[bool, int]:
    while b"\r\n\r\n" not in buf:
        chunk = s.recv(4096)
        if not chunk:
            return False, 0
        buf.extend(chunk)

    header_end = buf.index(b"\r\n\r\n") + 4
    headers = buf[:header_end].decode("latin-1", errors="replace")

    content_length = 0
    for line in headers.split("\r\n"):
        if line.lower().startswith("content-length:"):
            content_length = int(line.split(":", 1)[1].strip())
            break

    total_needed = header_end + content_length
    while len(buf) < total_needed:
        chunk = s.recv(4096)
        if not chunk:
            return False, 0
        buf.extend(chunk)

    ok = b"200 OK" in buf[:header_end]
    del buf[:total_needed]
    return ok, total_needed


def run_keepalive_worker(host: str, port: int, count: int) -> list[tuple[float, bool]]:
    results = []
    try:
        s = socket.create_connection((host, port), timeout=10.0)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        req = make_http_request(MESSAGE, keepalive=True)
        buf = bytearray()
        for _ in range(count):
            t0 = time.perf_counter()
            s.sendall(req)
            ok, _ = read_response(s, buf)
            elapsed = time.perf_counter() - t0
            results.append((elapsed, ok))
        s.close()
    except Exception:
        results.extend([(0.0, False)] * (count - len(results)))
    return results


def run_oneshot_worker(host: str, port: int) -> tuple[float, bool]:
    t0 = time.perf_counter()
    try:
        s = socket.create_connection((host, port), timeout=5.0)
        s.sendall(make_http_request(MESSAGE, keepalive=False))
        buf = bytearray()
        ok, _ = read_response(s, buf)
        s.close()
        return time.perf_counter() - t0, ok
    except Exception:
        return time.perf_counter() - t0, False


def run_load_test(host: str, port: int, total: int, concurrency: int,
                  warmup: int = 0, no_keepalive: bool = False) -> dict:
    latencies: list[float] = []
    ok_count = 0
    fail_count = 0
    lock = threading.Lock()

    mode = "new-conn" if no_keepalive else "keep-alive"
    print(f"  target: {host}:{port}")
    print(f"  requests: {total}, concurrency: {concurrency}, mode: {mode}")

    if no_keepalive:
        if warmup:
            print(f"  warming up ({warmup}) ...", end=" ", flush=True)
            with ThreadPoolExecutor(max_workers=concurrency) as pool:
                list(pool.map(lambda _: run_oneshot_worker(host, port), range(warmup)))
            print("done")

        print("  running load test ...", end=" ", flush=True)
        t_start = time.perf_counter()
        with ThreadPoolExecutor(max_workers=concurrency) as pool:
            futures = [pool.submit(run_oneshot_worker, host, port) for _ in range(total)]
            for fut in as_completed(futures):
                elapsed, ok = fut.result()
                with lock:
                    latencies.append(elapsed)
                    if ok:
                        ok_count += 1
                    else:
                        fail_count += 1
        wall_time = time.perf_counter() - t_start
    else:
        per_worker = total // concurrency
        remainder = total % concurrency

        if warmup:
            warm_per = max(1, warmup // concurrency)
            print(f"  warming up ({warmup}) ...", end=" ", flush=True)
            with ThreadPoolExecutor(max_workers=concurrency) as pool:
                list(pool.map(lambda _: run_keepalive_worker(host, port, warm_per), range(concurrency)))
            print("done")

        print("  running load test ...", end=" ", flush=True)
        t_start = time.perf_counter()
        with ThreadPoolExecutor(max_workers=concurrency) as pool:
            futures = []
            for i in range(concurrency):
                n = per_worker + (1 if i < remainder else 0)
                futures.append(pool.submit(run_keepalive_worker, host, port, n))
            for fut in as_completed(futures):
                for elapsed, ok in fut.result():
                    with lock:
                        latencies.append(elapsed)
                        if ok:
                            ok_count += 1
                        else:
                            fail_count += 1
        wall_time = time.perf_counter() - t_start

    print(f"done ({wall_time:.2f}s)")

    latencies.sort()
    return {
        "total": total,
        "ok": ok_count,
        "fail": fail_count,
        "wall_time_s": wall_time,
        "req_per_sec": total / wall_time if wall_time > 0 else 0,
        "latency_p50": latencies[len(latencies) // 2] if latencies else 0,
        "latency_p95": latencies[int(len(latencies) * 0.95)] if latencies else 0,
        "latency_p99": latencies[int(len(latencies) * 0.99)] if latencies else 0,
        "latency_min": latencies[0] if latencies else 0,
        "latency_max": latencies[-1] if latencies else 0,
    }


def format_latency(sec: float) -> str:
    if sec < 0.001:   return f"{sec * 1_000_000:.0f} us"
    if sec < 1:       return f"{sec * 1000:.1f} ms"
    return f"{sec:.3f} s"


def main():
    p = argparse.ArgumentParser(description="Siesta echo load test")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=9910)
    p.add_argument("--requests", "-n", type=int, default=10000)
    p.add_argument("--concurrency", "-c", type=int, default=50)
    p.add_argument("--warmup", type=int, default=200)
    p.add_argument("--no-keepalive", action="store_true",
                   help="Open a new connection for every request (tests conn establishment)")
    args = p.parse_args()

    results = run_load_test(args.host, args.port, args.requests,
                            args.concurrency, args.warmup, args.no_keepalive)

    print()
    print("══════════════════════════════════════════")
    print("  Load Test Results")
    print("══════════════════════════════════════════")
    print(f"  Requests:     {results['total']:>8}")
    print(f"  Successful:   {results['ok']:>8}")
    print(f"  Failed:       {results['fail']:>8}")
    print(f"  Wall time:    {results['wall_time_s']:>8.2f} s")
    print(f"  Throughput:   {results['req_per_sec']:>8.0f} req/s")
    print("  ────────────────────────────────────────")
    print(f"  Latency p50:  {format_latency(results['latency_p50']):>8}")
    print(f"  Latency p95:  {format_latency(results['latency_p95']):>8}")
    print(f"  Latency p99:  {format_latency(results['latency_p99']):>8}")
    print(f"  Latency min:  {format_latency(results['latency_min']):>8}")
    print(f"  Latency max:  {format_latency(results['latency_max']):>8}")
    print("══════════════════════════════════════════")

    if results["fail"] > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
