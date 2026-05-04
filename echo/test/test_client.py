# SPDX-License-Identifier: Apache-2.0
"""Integration tests for the echo server/client using siesta_client.

Usage (via run.sh):
    HOST=127.0.0.1 PORT=9910 python3 test/test_client.py
"""

import json
import os
import sys
import time
import traceback


def test_echo_basic(client):
    """GET /echo?message=hello returns {"message":"hello"}."""
    result = client.get__echo("hello")
    assert isinstance(result, dict), f"expected dict, got {type(result)}"
    assert result.get("message") == "hello", f"expected 'hello', got {result}"
    return True


def test_echo_empty(client):
    """GET /echo?message= returns {"message":""}."""
    result = client.get__echo("")
    assert isinstance(result, dict), f"expected dict, got {type(result)}"
    assert result["message"] == "", f"expected empty, got {result['message']!r}"
    return True


def test_echo_special_chars(client):
    """GET /echo?message=hello%20world returns decoded value."""
    result = client.get__echo("hello world")
    assert isinstance(result, dict), f"expected dict, got {type(result)}"
    assert result["message"] == "hello world", f"got {result['message']!r}"
    return True


TESTS = [
    ("basic echo", test_echo_basic),
    ("empty message", test_echo_empty),
    ("special chars", test_echo_special_chars),
]


def main():
    host = os.environ.get("HOST", "127.0.0.1")
    port = int(os.environ.get("PORT", "9910"))

    build_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build")
    sys.path.insert(0, build_dir)

    try:
        from Echo_API import Client  # type: ignore
    except ImportError:
        print(f"FAIL: cannot import Echo_API (Client) from {build_dir}", file=sys.stderr)
        traceback.print_exc()
        return 1

    print(f"connecting to {host}:{port} ...")
    try:
        c = Client(host, port)
    except Exception:
        print(f"FAIL: cannot create client to {host}:{port}", file=sys.stderr)
        traceback.print_exc()
        return 1

    passed = 0
    failed = 0

    for name, test_fn in TESTS:
        ok = False
        for attempt in range(2):
            try:
                ok = test_fn(c)
                break
            except Exception:
                if attempt == 0:
                    time.sleep(0.1)
                else:
                    traceback.print_exc()
        if ok:
            print(f"  PASS: {name}")
            passed += 1
        else:
            print(f"  FAIL: {name}")
            failed += 1

    c.stop()

    print()
    print(f"=== Result: {passed} passed, {failed} failed ===")
    print(f"=== Result: {passed} passed, {failed} failed ===")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
