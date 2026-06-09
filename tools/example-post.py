#!/usr/bin/env python3
"""Simple fire-and-forget client to update the Arduino display over HTTP POST."""

import socket
from urllib.parse import urlencode, urlparse

ARDUINO_URL = "http://192.168.68.86/api/update"


def send_fire_and_forget(url: str, payload: dict[str, str], timeout: float = 3.0) -> None:
    parsed = urlparse(url)

    if parsed.scheme != "http":
        raise ValueError("Alleen http:// wordt ondersteund door dit voorbeeldscript.")

    host = parsed.hostname
    if not host:
        raise ValueError("Ongeldige URL: host ontbreekt.")

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


def main() -> None:
    print("Arduino POST example (fire-and-forget)")
    print(f"Target: {ARDUINO_URL}")
    print()

    title = input("Titel: ").strip()
    content = input("Inhoud: ").strip()
    status = input("Status (leeg = webwings.nl 2026 + IP): ").strip()

    payload = {
        "title": title,
        "content": content,
        "status": status,
    }

    try:
        send_fire_and_forget(ARDUINO_URL, payload)
        print()
        print("POST verzonden. Script stopt direct zonder op response te wachten.")
    except (OSError, ValueError) as err:
        print()
        print(f"Netwerkfout: {err}")


if __name__ == "__main__":
    main()
