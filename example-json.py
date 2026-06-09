#!/usr/bin/env python3
"""CLI tool to send JSON-driven updates to the Arduino display API.

Examples:
  python3 example-json.py --json '{"title":"Hallo","content":"_Rood_"}'
  python3 example-json.py --json-file payload.json
  echo '{"title":"CLI","status":""}' | python3 example-json.py --stdin
"""

import argparse
import json
import socket
import sys
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode, urlparse
from urllib.request import Request, urlopen

DEFAULT_URL = "http://192.168.68.86/api/update"
ALLOWED_KEYS = ("title", "content", "status")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send JSON updates to Arduino /api/update")
    parser.add_argument("--url", default=DEFAULT_URL, help="Target API URL")
    parser.add_argument("--json", dest="json_text", help="JSON object as text")
    parser.add_argument("--json-file", help="Path to JSON file")
    parser.add_argument("--stdin", action="store_true", help="Read JSON from stdin")
    parser.add_argument(
        "--wait-response",
        action="store_true",
        help="Wait for HTTP response (default is fire-and-forget)",
    )
    return parser.parse_args()


def read_json_input(args: argparse.Namespace) -> str:
    sources = [bool(args.json_text), bool(args.json_file), bool(args.stdin)]
    if sum(sources) > 1:
        raise ValueError("Use only one input source: --json, --json-file, or --stdin.")

    if args.json_text:
        return args.json_text

    if args.json_file:
        with open(args.json_file, "r", encoding="utf-8") as handle:
            return handle.read()

    if args.stdin:
        return sys.stdin.read()

    return input("JSON input: ").strip()


def build_payload(json_text: str) -> dict[str, str]:
    try:
        data = json.loads(json_text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"Invalid JSON: {exc}") from exc

    if not isinstance(data, dict):
        raise ValueError("JSON root must be an object.")

    payload: dict[str, str] = {}
    for key in ALLOWED_KEYS:
        if key in data:
            value = data[key]
            if value is None:
                payload[key] = ""
            elif isinstance(value, str):
                payload[key] = value
            else:
                payload[key] = str(value)

    if not payload:
        allowed = ", ".join(ALLOWED_KEYS)
        raise ValueError(f"JSON must contain at least one of: {allowed}")

    return payload


def send_fire_and_forget(url: str, payload: dict[str, str], timeout: float = 3.0) -> None:
    parsed = urlparse(url)
    if parsed.scheme != "http":
        raise ValueError("Only http:// URLs are supported for fire-and-forget mode.")

    host = parsed.hostname
    if not host:
        raise ValueError("Invalid URL: missing host.")

    port = parsed.port or 80
    path = parsed.path or "/"
    if parsed.query:
        path += f"?{parsed.query}"

    body = urlencode(payload).encode("utf-8")

    headers = [
        f"POST {path} HTTP/1.1",
        f"Host: {host}",
        "Content-Type: application/x-www-form-urlencoded",
        f"Content-Length: {len(body)}",
        "Connection: close",
        "",
        "",
    ]
    request_bytes = "\r\n".join(headers).encode("utf-8") + body

    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(request_bytes)


def send_wait_response(url: str, payload: dict[str, str], timeout: float = 10.0) -> tuple[int, str]:
    body = urlencode(payload).encode("utf-8")
    request = Request(
        url,
        data=body,
        method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )

    with urlopen(request, timeout=timeout) as response:
        status_code = response.getcode()
        response_body = response.read().decode("utf-8", errors="replace")

    return status_code, response_body


def main() -> int:
    try:
        args = parse_args()
        json_text = read_json_input(args)
        payload = build_payload(json_text)

        if args.wait_response:
            status_code, response_body = send_wait_response(args.url, payload)
            print(f"POST completed (HTTP {status_code})")
            print(response_body)
        else:
            send_fire_and_forget(args.url, payload)
            print("POST sent (fire-and-forget).")

        return 0
    except (ValueError, OSError, HTTPError, URLError) as exc:
        print(f"Error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
