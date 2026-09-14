"""Open-Meteo one-liner. No API key. Skip if lat/lon unset."""

from __future__ import annotations

import json
from urllib.request import urlopen

from hub.board import weather_line


def fetch_weather(lat: float, lon: float, timeout_s: float = 8.0) -> str:
    url = (
        "https://api.open-meteo.com/v1/forecast"
        f"?latitude={lat}&longitude={lon}"
        "&current=temperature_2m,weather_code"
        "&daily=temperature_2m_max,temperature_2m_min"
        "&forecast_days=1&timezone=auto"
    )
    with urlopen(url, timeout=timeout_s) as response:
        payload = json.loads(response.read().decode("utf-8"))
    current = payload.get("current") or {}
    daily = payload.get("daily") or {}
    temp = float(current.get("temperature_2m"))
    code = int(current.get("weather_code") or 0)
    highs = daily.get("temperature_2m_max") or []
    lows = daily.get("temperature_2m_min") or []
    high = float(highs[0]) if highs else None
    low = float(lows[0]) if lows else None
    return weather_line(code, temp, high, low)
