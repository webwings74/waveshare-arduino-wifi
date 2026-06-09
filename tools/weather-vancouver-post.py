#!/usr/bin/env python3
"""Fetch the Vancouver weather forecast and send it as form-urlencoded to the Arduino API."""

from __future__ import annotations

import argparse
import json
import socket
from datetime import datetime
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode, urlparse
from urllib.request import Request, urlopen

ARDUINO_URL_DEFAULT = "http://192.168.68.86/api/update"
VANCOUVER_LAT = 49.2827
VANCOUVER_LON = -123.1207
WEATHER_URL_TEMPLATE = (
    "https://api.open-meteo.com/v1/forecast?"
    "latitude={lat}&longitude={lon}&daily=weather_code,temperature_2m_max,"
    "temperature_2m_min,precipitation_probability_max&timezone=America%2FVancouver&forecast_days=1"
)

WMO_CODE_LABELS: dict[int, str] = {
    0: "clear sky",
    1: "mainly clear",
    2: "partly cloudy",
    3: "overcast",
    45: "fog",
    48: "depositing rime fog",
    51: "light drizzle",
    53: "moderate drizzle",
    55: "dense drizzle",
    56: "light freezing drizzle",
    57: "dense freezing drizzle",
    61: "slight rain",
    63: "moderate rain",
    65: "heavy rain",
    66: "light freezing rain",
    67: "heavy freezing rain",
    71: "slight snowfall",
    73: "moderate snowfall",
    75: "heavy snowfall",
    77: "snow grains",
    80: "slight rain showers",
    81: "moderate rain showers",
    82: "violent rain showers",
    85: "slight snow showers",
    86: "heavy snow showers",
    95: "thunderstorm",
    96: "thunderstorm with slight hail",
    99: "thunderstorm with heavy hail",
}

WEEKDAY_EN = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
MONTH_EN = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send Vancouver weather forecast to Arduino as form-urlencoded")
    parser.add_argument("--url", default=ARDUINO_URL_DEFAULT, help="Arduino API URL (for example http://<ip>/api/update)")
    parser.add_argument("--timeout", type=float, default=45.0, help="Request timeout in seconds")
    parser.add_argument(
        "--wait-response",
        action="store_true",
        help="Wait for the Arduino HTTP response (can take a while on e-paper refresh)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Only print the payload, do not send to Arduino",
    )
    return parser.parse_args()


def fetch_weather_payload(timeout: float) -> dict[str, str]:
    weather_url = WEATHER_URL_TEMPLATE.format(lat=VANCOUVER_LAT, lon=VANCOUVER_LON)
    with urlopen(weather_url, timeout=timeout) as response:
        raw = response.read().decode("utf-8", errors="replace")
    data = json.loads(raw)

    daily = data.get("daily")
    if not isinstance(daily, dict):
        raise ValueError("Weather API response missing 'daily' block.")

    dates = daily.get("time")
    weather_codes = daily.get("weather_code")
    temp_max = daily.get("temperature_2m_max")
    temp_min = daily.get("temperature_2m_min")
    precip_max = daily.get("precipitation_probability_max")

    required_lists = [dates, weather_codes, temp_max, temp_min, precip_max]
    if not all(isinstance(item, list) and len(item) > 0 for item in required_lists):
        raise ValueError("Weather API response does not contain expected forecast arrays.")

    day_iso = str(dates[0])
    code = int(weather_codes[0])
    max_c = round(float(temp_max[0]))
    min_c = round(float(temp_min[0]))
    rain_prob = int(round(float(precip_max[0])))

    try:
        parsed_date = datetime.strptime(day_iso, "%Y-%m-%d")
        day_txt = f"{WEEKDAY_EN[parsed_date.weekday()]} {parsed_date.day:02d} {MONTH_EN[parsed_date.month - 1]}"
    except ValueError:
        day_txt = day_iso

    condition = WMO_CODE_LABELS.get(code, f"weather code {code}")
    content = f"{day_txt}: {condition}, min {min_c}C max {max_c}C, rain chance {rain_prob}%."

    if len(content) > 256:
        content = content[:253] + "..."

    return {
        "title": "Weather Vancouver",
        "content": content,
        "status": "",
    }


def send_form_fire_and_forget(url: str, payload: dict[str, str], timeout: float) -> None:
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


def send_form_wait_response(url: str, payload: dict[str, str], timeout: float) -> tuple[int, str]:
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
        payload = fetch_weather_payload(timeout=args.timeout)

        print("Payload:")
        print(json.dumps(payload, indent=2, ensure_ascii=False))

        if args.dry_run:
            print("Dry run enabled: not sending to Arduino.")
            return 0

        if args.wait_response:
            status_code, response_body = send_form_wait_response(args.url, payload, timeout=args.timeout)
            print(f"POST completed (HTTP {status_code})")
            print(response_body)
        else:
            send_form_fire_and_forget(args.url, payload, timeout=args.timeout)
            print("Form POST sent (fire-and-forget).")

        return 0
    except (ValueError, OSError, HTTPError, URLError, json.JSONDecodeError) as exc:
        print(f"Error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())