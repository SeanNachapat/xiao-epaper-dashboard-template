#!/usr/bin/env python3
"""
GitHub Actions Cloud Renderer for 7.5" 800x480 E-Paper Dashboard
Renders Page 1 (Daily Brief & Weather) and Page 2 (Systems & Portfolio)
Outputs raw 48,000-byte GxEPD2 binary files (page1.bin, page2.bin) for direct ESP32 streaming.
"""
import os
import sys
import math
import json
from datetime import datetime, timezone
import urllib.request
from PIL import Image, ImageDraw, ImageFont

WIDTH = 800
HEIGHT = 480
BLACK = 0
WHITE = 1

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FONTS_DIR = os.path.join(SCRIPT_DIR, "fonts")

# =============================================================================
# USER CONFIGURATION: Customize all your display data here!
# You can edit this dictionary directly, or edit 'custom_data.json' in the root.
# =============================================================================
USER_CONFIG = {
    # 1. Location & Weather Settings
    "location_name": os.getenv("LOCATION_NAME", "BANGKOK"),
    "latitude": float(os.getenv("WEATHER_LAT", "13.7563")),
    "longitude": float(os.getenv("WEATHER_LON", "100.5018")),
    "timezone": os.getenv("TIMEZONE", "Asia/Bangkok"),

    # 2. Anniversary Tracker
    "anniversary_start": os.getenv("ANNIVERSARY_START_DATE", "2023-08-16"),
    "anniversary_subtitle": "TOGETHER IN LOVE",

    # 3. Page 1: Daily Brief / Agenda Items (up to 2 priority items)
    "agenda_items": [
        {
            "tag": "[1]",
            "title": "Flight UA412 to SFO:",
            "desc1": "Dep 14:30 from Term 3,",
            "desc2": "Gate 71."
        },
        {
            "tag": "[2]",
            "title": "Design Review:",
            "desc1": "Typography guidelines for",
            "desc2": "series EPD-800."
        }
    ],

    # 4. Page 2: Financial & Portfolio Data
    "portfolio": {
        "net_worth": 0.0,
        "day_change": 0.0,
        "day_change_pct": 0.0,
        "high_7d": 0.0,
        "low_7d": 0.0,
        "sparkline": [0, 0, 0, 0, 0, 0, 0],
        "tickers": [
            {"symbol": "BTC", "price": 64230.50, "delta_pct": 3.42},
            {"symbol": "ETH", "price": 3485.20, "delta_pct": -1.15},
            {"symbol": "VOO", "price": 498.60, "delta_pct": 0.85},
        ]
    },

    # 5. Page 2: AI Workspace Telemetry (name, usage label, progress bar ratio 0.0 to 1.0)
    "ai_telemetry": [
        ("ANTIGRAVITY AI", "148K / 200K TOKENS", 0.74),
        ("OPENAI GPT-4O", "84K / 150K TOKENS", 0.56),
        ("CLAUDE 3.5 SONNET", "112K / 150K TOKENS", 0.75),
    ],

    # 6. Page 2: Infrastructure Health Cards (title, subtitle/specs, status_pill)
    "infra_cards": [
        ("PROXMOX VE", "CPU 18% • 64GB RAM", "HEALTHY"),
        ("NAS STORAGE POOL", "14.2TB / 24TB (59%)", "ONLINE"),
        ("KUBERNETES CLUSTER", "3 NODES • 28 PODS", "HEALTHY"),
        ("POSTGRES PRIMARY", "CONN: 42 • QPS: 120", "OPTIMAL"),
    ]
}

# Load external custom_data.json if present
custom_json_path = os.path.join(SCRIPT_DIR, "custom_data.json")
if os.path.exists(custom_json_path):
    try:
        with open(custom_json_path, "r", encoding="utf-8") as f:
            USER_CONFIG.update(json.load(f))
            print(f"[+] Loaded custom config from {custom_json_path}")
    except Exception as e:
        print(f"[WARN] Failed to load custom_data.json: {e}")

LAT = float(USER_CONFIG["latitude"])
LON = float(USER_CONFIG["longitude"])
TZ = USER_CONFIG["timezone"]
LOCATION_NAME = USER_CONFIG["location_name"]
ANNIV_START = USER_CONFIG["anniversary_start"]


def get_font(filename: str, size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    candidates = [
        os.path.join(FONTS_DIR, filename),
        f"/System/Library/Fonts/Supplemental/{filename}",
        f"/System/Library/Fonts/{filename}",
        "/System/Library/Fonts/Menlo.ttc",
        os.path.join(FONTS_DIR, "Menlo.ttc")
    ]
    for p in candidates:
        if os.path.exists(p):
            try:
                if p.endswith(".ttc"):
                    idx = 1 if bold else 0
                    return ImageFont.truetype(p, size, index=idx)
                return ImageFont.truetype(p, size)
            except Exception:
                continue
    return ImageFont.load_default()

# Fonts
font_serif_hero = get_font("Georgia Bold.ttf", 68, bold=True)
font_serif_am   = get_font("Georgia Bold.ttf", 20, bold=True)
font_serif_num  = get_font("Georgia Bold.ttf", 46, bold=True)
font_temp_num   = get_font("Georgia Bold.ttf", 56, bold=True)

font_menlo_bold_14 = get_font("Menlo.ttc", 14, bold=True)
font_menlo_bold_12 = get_font("Menlo.ttc", 12, bold=True)
font_menlo_bold_11 = get_font("Menlo.ttc", 11, bold=True)
font_menlo_bold_9  = get_font("Menlo.ttc", 9, bold=True)

font_menlo_reg_12  = get_font("Menlo.ttc", 12, bold=False)
font_menlo_reg_11  = get_font("Menlo.ttc", 11, bold=False)
font_menlo_reg_9   = get_font("Menlo.ttc", 9, bold=False)

def draw_black_pill(draw: ImageDraw.ImageDraw, x: int, y: int, text: str, font=font_menlo_bold_11, pad_x=6, pad_y=2) -> int:
    bbox = draw.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    box_w = tw + 2 * pad_x
    box_h = th + 2 * pad_y + 2
    draw.rectangle([x, y, x + box_w, y + box_h], fill=BLACK)
    draw.text((x + pad_x, y + pad_y), text, fill=WHITE, font=font)
    return box_w

def draw_battery(draw: ImageDraw.ImageDraw, x: int, y: int, pct=98):
    draw.rectangle([x + 3, y - 2, x + 7, y], fill=BLACK)
    draw.rectangle([x, y, x + 10, y + 17], outline=BLACK, width=1)
    fill_h = int(pct / 100.0 * 15)
    if fill_h > 0:
        draw.rectangle([x + 1, y + 17 - fill_h, x + 9, y + 16], fill=BLACK)

def map_wmo_code(code: int) -> str:
    if code == 0:
        return "CLEAR SKY"
    elif code in (1, 2):
        return "PARTLY CLOUDY"
    elif code == 3:
        return "OVERCAST"
    elif code in (45, 48):
        return "FOGGY"
    elif code in (51, 53, 55):
        return "DRIZZLE"
    elif code in (61, 63, 65):
        return "RAIN"
    elif code in (71, 73, 75, 77):
        return "SNOW"
    elif code in (80, 81, 82):
        return "RAIN SHOWERS"
    elif code in (95, 96, 99):
        return "THUNDERSTORM"
    return "PARTLY CLOUDY"

def draw_weather_icon_box(draw: ImageDraw.ImageDraw, x: int, y: int, condition: str = "PARTLY CLOUDY", w=50, h=50):
    draw.rectangle([x, y, x + w, y + h], fill=BLACK)
    cx = x + w // 2
    cy = y + h // 2 + 2
    cond_upper = condition.upper()

    if "CLEAR" in cond_upper or "SUN" in cond_upper:
        draw.ellipse([cx - 7, cy - 7, cx + 7, cy + 7], fill=WHITE)
        for dx, dy in [(0, -13), (0, 13), (-13, 0), (13, 0), (-9, -9), (9, -9), (-9, 9), (9, 9)]:
            draw.line([(cx + int(dx*0.6), cy + int(dy*0.6)), (cx + dx, cy + dy)], fill=WHITE, width=2)
        return

    # Radiating dashes around cloud top (for partly cloudy)
    if "PARTLY" in cond_upper:
        draw.rectangle([cx - 2, cy - 18, cx + 2, cy - 14], fill=WHITE)
        draw.rectangle([cx - 13, cy - 14, cx - 9, cy - 10], fill=WHITE)
        draw.rectangle([cx + 9, cy - 14, cx + 13, cy - 10], fill=WHITE)
        draw.rectangle([cx - 18, cy - 4, cx - 14, cy], fill=WHITE)

    # White cloud body
    draw.ellipse([cx - 14, cy - 5, cx - 1, cy + 8], fill=WHITE)
    draw.ellipse([cx - 8, cy - 12, cx + 6, cy + 3], fill=WHITE)
    draw.ellipse([cx - 1, cy - 7, cx + 14, cy + 8], fill=WHITE)
    draw.rectangle([cx - 12, cy + 1, cx + 12, cy + 8], fill=WHITE)

    if "RAIN" in cond_upper or "DRIZZLE" in cond_upper or "SHOWER" in cond_upper:
        draw.line([(cx - 8, cy + 11), (cx - 11, cy + 16)], fill=WHITE, width=1)
        draw.line([(cx, cy + 11), (cx - 3, cy + 16)], fill=WHITE, width=1)
        draw.line([(cx + 8, cy + 11), (cx + 5, cy + 16)], fill=WHITE, width=1)
    elif "THUNDER" in cond_upper:
        draw.polygon([(cx - 1, cy + 9), (cx + 4, cy + 9), (cx, cy + 14), (cx + 5, cy + 14), (cx - 3, cy + 20), (cx, cy + 15), (cx - 4, cy + 15)], fill=WHITE)

def draw_top_nav(draw: ImageDraw.ImageDraw, active_tab: int):
    draw.line([(0, 44), (WIDTH, 44)], fill=BLACK, width=1)
    if active_tab == 1:
        draw.rectangle([18, 12, 128, 36], fill=BLACK)
        draw.text((26, 17), "DAILY BRIEF", fill=WHITE, font=font_menlo_bold_11)
        draw.text((142, 17), "SYSTEMS & PORTFOLIO", fill=BLACK, font=font_menlo_reg_11)
    else:
        draw.text((22, 17), "DAILY BRIEF", fill=BLACK, font=font_menlo_reg_11)
        draw.rectangle([126, 12, 302, 36], fill=BLACK)
        draw.text((134, 17), "SYSTEMS & PORTFOLIO", fill=WHITE, font=font_menlo_bold_11)

    draw.text((664, 17), "SYNCED", fill=BLACK, font=font_menlo_reg_11)
    draw.line([(718, 15), (718, 33)], fill=BLACK, width=1)
    draw.text((728, 17), "98%", fill=BLACK, font=font_menlo_bold_11)
    draw_battery(draw, 766, 16, pct=98)

def draw_footer(draw: ImageDraw.ImageDraw, page_num: int, now: datetime):
    draw.line([(0, 442), (WIDTH, 442)], fill=BLACK, width=1)
    refreshed_str = now.strftime("%I:%M %p").lstrip("0")
    
    # Left footer: ■ REFRESH: 03:00 PM ICT | NEXT CYCLE IN 30M
    draw.rectangle([18, 456, 26, 464], fill=BLACK)
    draw.text((32, 453), f"REFRESH: {refreshed_str} ICT | NEXT CYCLE IN 30M", fill=BLACK, font=font_menlo_bold_11)
    
    # Right footer: E-INK 800×480 | [PAGE 01 / 02]
    draw.text((580, 453), "E-INK 800×480 |", fill=BLACK, font=font_menlo_reg_11)
    draw_black_pill(draw, 694, 450, f"PAGE 0{page_num} / 02", font=font_menlo_bold_11, pad_x=6, pad_y=2)

def get_local_now() -> datetime:
    try:
        import zoneinfo
        tz = zoneinfo.ZoneInfo(TZ)
        return datetime.now(tz)
    except Exception:
        return datetime.now()

# API Fetching
def fetch_weather():
    try:
        url = f"https://api.open-meteo.com/v1/forecast?latitude={LAT}&longitude={LON}&current=temperature_2m,weather_code&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&timezone={TZ}"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=10.0) as res:
            r = json.loads(res.read().decode())
        curr = r.get("current", {})
        daily = r.get("daily", {})
        
        # AQI
        aqi_url = f"https://air-quality-api.open-meteo.com/v1/air-quality?latitude={LAT}&longitude={LON}&current=us_aqi"
        aqi_req = urllib.request.Request(aqi_url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(aqi_req, timeout=10.0) as res:
            aqi_r = json.loads(res.read().decode())
        aqi = int(aqi_r.get("current", {}).get("us_aqi", 25))
        aqi_desc = "GOOD" if aqi <= 50 else ("MODERATE" if aqi <= 100 else "UNHEALTHY")

        forecast = []
        days_str = ["MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"]
        dates = daily.get("time", [])
        highs = daily.get("temperature_2m_max", [])
        lows = daily.get("temperature_2m_min", [])
        rains = daily.get("precipitation_probability_max", [])

        for i in range(min(4, len(dates))):
            dt = datetime.strptime(dates[i], "%Y-%m-%d")
            forecast.append({
                "day": days_str[dt.weekday()],
                "high": int(round(highs[i])),
                "low": int(round(lows[i])),
                "rain_pct": int(rains[i]),
                "high_rain": int(rains[i]) >= 50
            })

        curr_code = curr.get("weather_code", 0)
        cond_text = map_wmo_code(curr_code)

        return {
            "temp": int(round(curr.get("temperature_2m", 30))),
            "condition": cond_text,
            "high": forecast[0]["high"] if forecast else 33,
            "low": forecast[0]["low"] if forecast else 26,
            "aqi": aqi,
            "aqi_desc": aqi_desc,
            "forecast": forecast
        }
    except Exception as e:
        print(f"[WARN] Weather API fallback: {e}")
        return {
            "temp": 30, "condition": "PARTLY CLOUDY", "high": 33, "low": 26,
            "aqi": 35, "aqi_desc": "GOOD",
            "forecast": [
                {"day": "SUN", "high": 33, "low": 26, "rain_pct": 90, "high_rain": True},
                {"day": "MON", "high": 32, "low": 26, "rain_pct": 85, "high_rain": True},
                {"day": "TUE", "high": 32, "low": 25, "rain_pct": 70, "high_rain": True},
                {"day": "WED", "high": 32, "low": 26, "rain_pct": 60, "high_rain": True},
            ]
        }

def fetch_ticker_live(symbol: str) -> dict:
    symbol_u = symbol.upper().strip()
    # 1. Crypto via Binance 24hr API (BTC, ETH, SOL, etc.)
    if symbol_u in ("BTC", "ETH", "SOL", "BNB", "XRP", "DOGE", "ADA", "AVAX", "NEAR", "SUI", "DOT"):
        try:
            url = f"https://api.binance.com/api/v3/ticker/24hr?symbol={symbol_u}USDT"
            req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=4) as res:
                d = json.loads(res.read().decode())
                return {
                    "symbol": symbol_u,
                    "price": float(d["lastPrice"]),
                    "delta_pct": round(float(d["priceChangePercent"]), 2)
                }
        except Exception:
            pass

    # 2. Stocks / ETFs / Indices via Yahoo Finance Chart API (VOO, AAPL, NVDA, TSLA, etc.)
    try:
        url = f"https://query1.finance.yahoo.com/v8/finance/chart/{symbol_u}?interval=1d&range=1d"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"})
        with urllib.request.urlopen(req, timeout=4) as res:
            d = json.loads(res.read().decode())
            meta = d["chart"]["result"][0]["meta"]
            price = float(meta["regularMarketPrice"])
            prev = float(meta.get("chartPreviousClose", meta.get("previousClose", price)))
            pct = round(((price - prev) / prev) * 100, 2)
            return {
                "symbol": symbol_u,
                "price": price,
                "delta_pct": pct
            }
    except Exception:
        pass

    return None

def fetch_finance():
    p = USER_CONFIG.get("portfolio", {
        "net_worth": 148920.0,
        "day_change": 2084.10,
        "day_change_pct": 1.42,
        "high_7d": 151200.0,
        "low_7d": 142100.0,
        "sparkline": [142500, 144200, 143800, 147100, 146800, 148200, 148920],
        "tickers": [
            {"symbol": "BTC", "price": 64230.50, "delta_pct": 3.42},
            {"symbol": "ETH", "price": 3485.20, "delta_pct": -1.15},
            {"symbol": "VOO", "price": 498.60, "delta_pct": 0.85},
        ]
    })

    # Fetch real-time market data for each configured ticker
    tickers = p.get("tickers", [])
    updated_tickers = []
    for t in tickers:
        sym = t.get("symbol", "")
        live = fetch_ticker_live(sym)
        if live:
            updated_tickers.append(live)
        else:
            updated_tickers.append(t)
    if updated_tickers:
        p["tickers"] = updated_tickers

    return p

def get_anniversary_info():
    try:
        start = datetime.strptime(ANNIV_START, "%Y-%m-%d")
        now = datetime.now()
        elapsed_days = (now - start).days
        years = elapsed_days // 365
        next_anniv_year = start.year + years + 1
        next_anniv_date = datetime(next_anniv_year, start.month, start.day)
        days_to_next = (next_anniv_date - now).days
        pct = max(0, min(100, int((365 - days_to_next) / 365.0 * 100)))
        return {
            "elapsed_days": elapsed_days,
            "next_target_year": years + 1,
            "days_to_next": days_to_next,
            "progress_pct": pct,
            "date_label": f"SINCE {start.strftime('%B %d, %Y').upper()}"
        }
    except Exception:
        return {
            "elapsed_days": 1248,
            "next_target_year": 4,
            "days_to_next": 42,
            "progress_pct": 88,
            "date_label": "SINCE MAY 24, 2021"
        }

# Renderers
def render_page1():
    now = get_local_now()
    img = Image.new("1", (WIDTH, HEIGHT), WHITE)
    draw = ImageDraw.Draw(img)
    draw_top_nav(draw, active_tab=1)
    draw_footer(draw, page_num=1, now=now)

    mid_x, mid_y = 466, 230
    draw.line([(mid_x, 44), (mid_x, 442)], fill=BLACK, width=1)
    draw.line([(0, mid_y), (WIDTH, mid_y)], fill=BLACK, width=1)

    # 1. Top-Left: Time
    draw.rectangle([18, 59, 26, 67], fill=BLACK)
    draw.text((32, 56), LOCATION_NAME, fill=BLACK, font=font_menlo_bold_12)
    
    # Date right-aligned to box margin
    date_str = now.strftime("%A, %b %d").upper()
    bbox_d = draw.textbbox((0, 0), date_str, font=font_menlo_reg_11)
    draw.text((mid_x - 18 - (bbox_d[2] - bbox_d[0]), 57), date_str, fill=BLACK, font=font_menlo_reg_11)

    # Big Serif Hero Clock
    time_str = now.strftime("%I:%M")
    draw.text((16, 78), time_str, fill=BLACK, font=font_serif_hero)
    bbox_time = draw.textbbox((16, 78), time_str, font=font_serif_hero)
    ampm_str = now.strftime("%p")
    draw.text((bbox_time[2] + 8, 130), ampm_str, fill=BLACK, font=font_serif_am)

    # Sub-divider line
    draw.line([(18, 196), (mid_x - 18, 196)], fill=BLACK, width=1)
    # Left: Timezone label, Right: LOCAL TIME
    draw.text((18, 204), "ICT • UTC+7", fill=BLACK, font=font_menlo_reg_11)
    bbox_lt = draw.textbbox((0, 0), "LOCAL TIME", font=font_menlo_reg_11)
    draw.text((mid_x - 18 - (bbox_lt[2] - bbox_lt[0]), 204), "LOCAL TIME", fill=BLACK, font=font_menlo_reg_11)

    # 2. Top-Right: Weather
    w = fetch_weather()
    draw.text((482, 52), "OUTDOOR WEATHER", fill=BLACK, font=font_menlo_bold_12)
    draw_black_pill(draw, 482, 69, f"AQI {w['aqi']} • {w['aqi_desc']}", font=font_menlo_bold_11, pad_x=6, pad_y=2)

    draw.text((482, 92), f"{w['temp']}°", fill=BLACK, font=font_temp_num)
    bbox_t = draw.textbbox((482, 92), f"{w['temp']}°", font=font_temp_num)
    cond_x = bbox_t[2] + 16

    draw.text((cond_x, 105), w["condition"], fill=BLACK, font=font_menlo_bold_12)
    draw.text((cond_x, 126), f"H: {w['high']}° / L: {w['low']}°", fill=BLACK, font=font_menlo_reg_11)

    draw_weather_icon_box(draw, 732, 93, condition=w["condition"], w=50, h=50)

    draw.line([(482, 156), (782, 156)], fill=BLACK, width=1)
    fc_x_centers = [518, 592, 668, 744]
    for i, fc in enumerate(w.get("forecast", [])[:4]):
        cx = fc_x_centers[i]
        day = fc["day"]
        rng = f"{fc['high']}/{fc['low']}"
        rain = f"{fc['rain_pct']}% RAIN"

        bbox_d = draw.textbbox((0, 0), day, font=font_menlo_bold_11)
        draw.text((cx - (bbox_d[2] - bbox_d[0]) // 2, 166), day, fill=BLACK, font=font_menlo_bold_11)

        bbox_r = draw.textbbox((0, 0), rng, font=font_menlo_reg_11)
        draw.text((cx - (bbox_r[2] - bbox_r[0]) // 2, 184), rng, fill=BLACK, font=font_menlo_reg_11)

        if fc.get("high_rain", False):
            bbox_rn = draw.textbbox((0, 0), rain, font=font_menlo_bold_9)
            pw = bbox_rn[2] - bbox_rn[0] + 8
            draw_black_pill(draw, cx - pw // 2, 200, rain, font=font_menlo_bold_9, pad_x=4, pad_y=1)
        else:
            bbox_rn = draw.textbbox((0, 0), rain, font=font_menlo_bold_9)
            draw.text((cx - (bbox_rn[2] - bbox_rn[0]) // 2, 202), rain, fill=BLACK, font=font_menlo_bold_9)

    # 3. Bottom-Left: Agenda
    draw_black_pill(draw, 18, 242, "TODAY'S FOCUS // PRIORITY", font=font_menlo_bold_11, pad_x=6, pad_y=3)
    header_right_str = "HIGH PRIORITY"
    bbox_hr = draw.textbbox((0, 0), header_right_str, font=font_menlo_reg_11)
    draw.text((mid_x - 18 - (bbox_hr[2] - bbox_hr[0]), 246), header_right_str, fill=BLACK, font=font_menlo_reg_11)
    draw.line([(18, 268), (mid_x - 18, 268)], fill=BLACK, width=1)

    items = USER_CONFIG.get("agenda_items", [])
    y_starts = [284, 342]
    for i, it in enumerate(items[:2]):
        sy = y_starts[i]
        draw.text((18, sy), it.get("tag", f"[{i+1}]"), fill=BLACK, font=font_menlo_bold_12)
        title_text = it.get("title", "")
        draw.text((46, sy), title_text, fill=BLACK, font=font_menlo_bold_12)
        bbox_title = draw.textbbox((46, sy), title_text, font=font_menlo_bold_12)
        tx = bbox_title[2] + 8
        draw.text((tx, sy), it.get("desc1", ""), fill=BLACK, font=font_menlo_reg_11)
        if it.get("desc2"):
            draw.text((46, sy + 22), it.get("desc2", ""), fill=BLACK, font=font_menlo_reg_11)

    # 4. Bottom-Right: Anniversary
    anniv = get_anniversary_info()
    draw.rectangle([482, 246, 490, 254], fill=BLACK)
    draw.text((496, 244), "ANNIVERSARY TRACKER", fill=BLACK, font=font_menlo_bold_12)
    draw_black_pill(draw, 674, 242, f"TARGET: YEAR {anniv['next_target_year']}", font=font_menlo_bold_11, pad_x=6, pad_y=2)

    for px in range(486, 578, 3):
        draw.point((px, 288), fill=BLACK)
        draw.point((px, 354), fill=BLACK)
    for py in range(288, 354, 3):
        draw.point((486, py), fill=BLACK)
        draw.point((578, py), fill=BLACK)

    # Heart
    draw.ellipse([527, 306, 537, 316], outline=BLACK, width=1)
    draw.ellipse([535, 306, 545, 316], outline=BLACK, width=1)
    draw.line([(527, 313), (536, 323)], fill=BLACK, width=1)
    draw.line([(545, 313), (536, 323)], fill=BLACK, width=1)

    draw.text((493, 328), "[ ART FRAME ]", fill=BLACK, font=font_menlo_reg_9)
    draw.text((502, 360), "120×120 PX", fill=BLACK, font=font_menlo_reg_9)

    draw.text((588, 274), f"{anniv['elapsed_days']:,}", fill=BLACK, font=font_serif_num)
    draw.text((722, 298), "DAYS", fill=BLACK, font=font_menlo_bold_11)
    draw.text((588, 330), USER_CONFIG.get("anniversary_subtitle", "TOGETHER IN LOVE"), fill=BLACK, font=font_menlo_bold_12)
    draw.text((588, 348), anniv["date_label"], fill=BLACK, font=font_menlo_reg_11)

    draw.text((482, 386), f"YEAR {anniv['next_target_year']} IN {anniv['days_to_next']} DAYS", fill=BLACK, font=font_menlo_bold_11)
    draw.text((756, 386), f"{anniv['progress_pct']}%", fill=BLACK, font=font_menlo_bold_11)
    draw.rectangle([482, 404, 782, 416], outline=BLACK, width=1)
    fill_w = int((782 - 482 - 4) * (anniv["progress_pct"] / 100.0))
    if fill_w > 0:
        draw.rectangle([484, 406, 484 + fill_w, 414], fill=BLACK)

    return img

def render_page2():
    now = get_local_now()
    img = Image.new("1", (WIDTH, HEIGHT), WHITE)
    draw = ImageDraw.Draw(img)
    draw_top_nav(draw, active_tab=2)
    draw_footer(draw, page_num=2, now=now)

    mid_x = 420
    draw.line([(mid_x, 44), (mid_x, 442)], fill=BLACK, width=1)
    draw.line([(0, 240), (mid_x, 240)], fill=BLACK, width=1)

    # 1. Top-Left: Portfolio
    fin = fetch_finance()
    draw.rectangle([18, 56, 26, 64], fill=BLACK)
    draw.text((32, 53), "PORTFOLIO // NET WORTH", fill=BLACK, font=font_menlo_bold_12)
    pct_val = fin.get('day_change_pct', 0.0)
    tag_sign = "+" if pct_val > 0 else ""
    draw_black_pill(draw, 330, 52, f"{tag_sign}{pct_val:.2f}% 24H", font=font_menlo_bold_11, pad_x=6, pad_y=2)

    nw_str = f"${int(fin['net_worth']):,}"
    draw.text((16, 76), nw_str, fill=BLACK, font=font_serif_hero)

    draw.line([(18, 142), (mid_x - 18, 142)], fill=BLACK, width=1)
    draw.text((18, 146), "7D TRAJECTORY", fill=BLACK, font=font_menlo_reg_9)
    draw.text((18, 226), f"LOW: ${int(fin['low_7d']):,}", fill=BLACK, font=font_menlo_reg_9)
    draw.text((320, 226), f"HIGH: ${int(fin['high_7d']):,}", fill=BLACK, font=font_menlo_reg_9)

    # Dynamic Sparkline
    spark_vals = fin.get("sparkline", [])
    if spark_vals and len(spark_vals) >= 2 and max(spark_vals) > min(spark_vals):
        min_v, max_v = min(spark_vals), max(spark_vals)
        x_step = (380 - 24) / (len(spark_vals) - 1)
        pts = []
        for idx, val in enumerate(spark_vals):
            px = int(24 + idx * x_step)
            py = int(210 - ((val - min_v) / (max_v - min_v)) * (210 - 155))
            pts.append((px, py))
        for i in range(len(pts) - 1):
            draw.line([pts[i], pts[i+1]], fill=BLACK, width=2)
        draw.rectangle([pts[-1][0] - 3, pts[-1][1] - 3, pts[-1][0] + 3, pts[-1][1] + 3], fill=BLACK)
    else:
        # Flat baseline trajectory for $0 or flat portfolio
        draw.line([(24, 185), (380, 185)], fill=BLACK, width=2)
        draw.rectangle([377, 182, 383, 188], fill=BLACK)

    # 2. Bottom-Left: Tickers
    draw.text((18, 252), "ASSET WATCHLIST", fill=BLACK, font=font_menlo_bold_12)
    draw.text((336, 252), "REALTIME", fill=BLACK, font=font_menlo_reg_11)
    draw.line([(18, 272), (mid_x - 18, 272)], fill=BLACK, width=1)

    tickers = fin.get("tickers", [])
    row_y = 286
    for t in tickers:
        draw.rectangle([18, row_y + 2, 24, row_y + 8], fill=BLACK)
        draw.text((32, row_y), t["symbol"], fill=BLACK, font=font_menlo_bold_12)
        price_str = f"${t['price']:,.2f}" if t["price"] < 10000 else f"${int(t['price']):,}"
        draw.text((140, row_y), price_str, fill=BLACK, font=font_menlo_bold_12)
        pct = t["delta_pct"]
        tag = f"+{pct:.2f}%" if pct >= 0 else f"{pct:.2f}%"
        draw_black_pill(draw, 320, row_y - 2, tag, font=font_menlo_bold_11, pad_x=6, pad_y=2)
        draw.line([(18, row_y + 28), (mid_x - 18, row_y + 28)], fill=BLACK, width=1)
        row_y += 42

    # 3. Top-Right: AI Telemetry
    draw.rectangle([mid_x + 18, 56, mid_x + 26, 64], fill=BLACK)
    draw.text((mid_x + 32, 53), "AI WORKSPACE TELEMETRY", fill=BLACK, font=font_menlo_bold_12)
    draw_black_pill(draw, 680, 52, "DAILY ACTIVE", font=font_menlo_bold_11, pad_x=6, pad_y=2)

    ai_agents = USER_CONFIG.get("ai_telemetry", [
        ("ANTIGRAVITY AI", "148K / 200K TOKENS", 0.74),
        ("OPENAI GPT-4O", "84K / 150K TOKENS", 0.56),
        ("CLAUDE 3.5 SONNET", "112K / 150K TOKENS", 0.75),
    ])
    bar_y = 78
    for name, stat, ratio in ai_agents:
        draw.text((mid_x + 18, bar_y), name, fill=BLACK, font=font_menlo_bold_11)
        draw.text((640, bar_y), stat, fill=BLACK, font=font_menlo_reg_9)
        draw.rectangle([mid_x + 18, bar_y + 16, 782, bar_y + 24], outline=BLACK, width=1)
        fill_w = int((782 - (mid_x + 18) - 4) * ratio)
        if fill_w > 0:
            draw.rectangle([mid_x + 20, bar_y + 18, mid_x + 20 + fill_w, bar_y + 22], fill=BLACK)
        bar_y += 34

    draw.line([(mid_x, 192), (WIDTH, 192)], fill=BLACK, width=1)

    # 4. Bottom-Right: Infrastructure
    draw.rectangle([mid_x + 18, 206, mid_x + 26, 214], fill=BLACK)
    draw.text((mid_x + 32, 203), "INFRASTRUCTURE HEALTH", fill=BLACK, font=font_menlo_bold_12)
    draw.text((708, 203), "ALL SYSTEMS OK", fill=BLACK, font=font_menlo_bold_9)

    raw_cards = USER_CONFIG.get("infra_cards", [
        ("PROXMOX VE", "CPU 18% • 64GB RAM", "HEALTHY"),
        ("NAS STORAGE POOL", "14.2TB / 24TB (59%)", "ONLINE"),
        ("KUBERNETES CLUSTER", "3 NODES • 28 PODS", "HEALTHY"),
        ("POSTGRES PRIMARY", "CONN: 42 • QPS: 120", "OPTIMAL"),
    ])
    positions = [
        (mid_x + 18, 226),
        (mid_x + 18 + 176, 226),
        (mid_x + 18, 326),
        (mid_x + 18 + 176, 326),
    ]
    for i, item in enumerate(raw_cards[:4]):
        name, sub, stat = item[0], item[1], item[2]
        cx, cy = positions[i]
        draw.rectangle([cx, cy, cx + 168, cy + 90], outline=BLACK, width=1)
        draw.text((cx + 8, cy + 8), name, fill=BLACK, font=font_menlo_bold_11)
        draw.text((cx + 8, cy + 34), sub, fill=BLACK, font=font_menlo_reg_9)
        draw_black_pill(draw, cx + 168 - 66, cy + 58, stat, font=font_menlo_bold_9, pad_x=4, pad_y=2)

    return img

def to_gxepd2_bytes(img: Image.Image) -> bytes:
    # GxEPD2 expects bit 1 = Black, bit 0 = White
    # Pillow mode '1': 0=Black, 1=White -> invert bitwise
    return bytes(b ^ 0xFF for b in img.tobytes())

def main():
    print("[+] Rendering Page 1: Daily Brief & Weather...")
    p1 = render_page1()
    p1.save("page1.bmp", "BMP")
    p1.save("page1.png", "PNG")
    with open("page1.bin", "wb") as f:
        f.write(to_gxepd2_bytes(p1))
    print(f"    -> Generated page1.bin ({os.path.getsize('page1.bin'):,} bytes)")

    print("[+] Rendering Page 2: Systems & Portfolio...")
    p2 = render_page2()
    p2.save("page2.bmp", "BMP")
    p2.save("page2.png", "PNG")
    with open("page2.bin", "wb") as f:
        f.write(to_gxepd2_bytes(p2))
    print(f"    -> Generated page2.bin ({os.path.getsize('page2.bin'):,} bytes)")

    print("[✔] Cloud render complete!")

if __name__ == "__main__":
    main()
