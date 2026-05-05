#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Concurrent load test for the siesta echo server.

Sends GET /echo?message=... requests over raw HTTP/1.1 sockets using a
thread pool for maximum throughput.  Reports latency percentiles and
requests-per-second.

Usage:
    python3 load_test.py [--host HOST] [--port PORT]
                         [--requests N] [--concurrency C]
                         [--warmup N] [--keepalive N]
"""

import argparse
import socket
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

MESSAGE = "hello_load_test_1234567890"


def make_http_request(msg: str) -> bytes:
    return (
        f"GET /echo?message={msg} HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
    ).encode()


def make_request(host: str, port: int) -> tuple[float, bool]:
    """Open a fresh connection, send one request, measure latency."""
    t0 = time.perf_counter()
    try:
        s = socket.create_connection((host, port), timeout=5.0)
        s.sendall(make_http_request(MESSAGE))
        buf = read_response(s)
        s.close()
        elapsed = time.perf_counter() - t0
        return elapsed, b"200 OK" in buf
    except Exception:
        return time.perf_counter() - t0, False


def make_keepalive_requests(host: str, port: int, count: int) -> list[tuple[float, bool]]:
    """Open one connection, pipeline `count` requests, measure each latency."""
    results = []
    try:
        s = socket.create_connection((host, port), timeout=10.0)
        req = make_http_request(MESSAGE)
        for _ in range(count):
            t0 = time.perf_counter()
            s.sendall(req)
            buf = read_response(s)
            elapsed = time.perf_counter() - t0
            results.append((elapsed, b"200 OK" in buf))
        s.close()
    except Exception:
        results.extend([(0.0, False)] * (count - len(results)))
    return results


def read_response(s: socket.socket) -> bytes:
    """Read until \r\n\r\n or connection close."""
    buf = b""
    while len(buf) < 8192:
        try:
            chunk = s.recv(4096)
        except Exception:
            break
        if not chunk:
            break
        buf += chunk
        if b"\r\n\r\n" in buf:
            break
    return buf


def run_load_test(host: str, port: int, total: int, concurrency: int,
                  warmup: int = 0, keepalive: int = 0) -> dict:
    """Run load test and return aggregated results."""

    latencies: list[float] = []
    ok_count = 0
    fail_count = 0
    lock = threading.Lock()

    print(f"  target: {host}:{port}")
    print(f"  requests: {total}, concurrency: {concurrency}")
    if warmup:
        print(f"  warmup: {warmup}")
    if keepalive:
        print(f"  keep-alive: {keepalive} req/conn")

    if keepalive:
        conns_needed = (total + keepalive - 1) // keepalive

        # Warmup
        if warmup:
            print("  warming up ...", end=" ", flush=True)
            warm_conns = (warmup + keepalive - 1) // keepalive
            with ThreadPoolExecutor(max_workers=min(concurrency, warm_conns)) as pool:
                futures = []
                for i in range(warm_conns):
                    k = min(keepalive, warmup - i * keepalive)
                    if k > 0:
                        futures.append(pool.submit(make_keepalive_requests, host, port, k))
                for fut in as_completed(futures):
                    pass
            print("done")

        print("  running load test ...", end=" ", flush=True)
        t_start = time.perf_counter()

        with ThreadPoolExecutor(max_workers=concurrency) as pool:
            futures = []
            for i in range(conns_needed):
                k = min(keepalive, total - i * keepalive)
                futures.append(pool.submit(make_keepalive_requests, host, port, k))

            for fut in as_completed(futures):
                for elapsed, ok in fut.result():
                    with lock:
                        latencies.append(elapsed)
                        if ok:
                            ok_count += 1
                        else:
                            fail_count += 1

        t_end = time.perf_counter()
        wall_time = t_end - t_start
    else:
        if warmup:
            print("  warming up ...", end=" ", flush=True)
            with ThreadPoolExecutor(max_workers=concurrency) as pool:
                futures = {pool.submit(make_request, host, port): i for i in range(warmup)}
                for _ in as_completed(futures):
                    pass
            print("done")

        print("  running load test ...", end=" ", flush=True)
        t_start = time.perf_counter()

        with ThreadPoolExecutor(max_workers=concurrency) as pool:
            futures = [pool.submit(make_request, host, port) for _ in range(total)]
            for fut in as_completed(futures):
                elapsed, ok = fut.result()
                with lock:
                    latencies.append(elapsed)
                    if ok:
                        ok_count += 1
                    else:
                        fail_count += 1

        t_end = time.perf_counter()
        wall_time = t_end - t_start

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
    p.add_argument("--keepalive", "-k", type=int, default=0,
                   help="Requests per connection (1=new conn each time, >1=reuse)")
    args = p.parse_args()

    results = run_load_test(args.host, args.port, args.requests,
                            args.concurrency, args.warmup, args.keepalive)

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
