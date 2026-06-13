# 🚪 ESP32 Prank Portal

> A harmless open-WiFi captive portal for the ESP32-S3. People connect expecting free internet, get a fake "connecting…" sequence, then a cheeky **GOTCHA!** with confetti. Meanwhile you watch everyone who got caught from a live, password-protected dashboard.

![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue)
![Framework](https://img.shields.io/badge/framework-Arduino-teal)
![Core](https://img.shields.io/badge/arduino--esp32-3.x-orange)
![License](https://img.shields.io/badge/license-MIT-green)

---

## ⚠️ Responsible use first

This is a **joke tool for fun and security awareness** — nothing more.

- It broadcasts a **single open access point** with no internet. It does **not** impersonate a real network, capture passwords, or sniff anyone's traffic.
- Only run it in spaces you control (your home, your desk, an event you're hosting) and ideally with people who'll enjoy the gag.
- Operating rogue access points in public or workplace environments may be illegal where you live and against venue rules. You are responsible for how you use it.
- The captured "log" is just MAC/IP/timestamps of devices that joined *your* AP, kept in RAM for the dashboard. Don't use it to track people without their knowledge.

If you wouldn't be comfortable explaining it to the person who got pranked, don't run it.

---

## ✨ Features

- **Captive portal** that auto-pops the "sign in" sheet on Android, iOS/macOS, Windows and Firefox.
- **Animated reveal** — fake secure-connection sequence with a shimmer progress bar, then a confetti **GOTCHA!** and a rotating prank message.
- **Live dashboard** (`/dashboard`) — every device that connected: MAC, IP, online status, connection time, last-seen, prank count and last message. Updates every 2 seconds with no page reload or flicker.
- **Real local timestamps with no RTC** — the device only knows its uptime; your browser back-calculates real wall-clock time using its own clock. No clock module, no internet needed.
- **Edit everything from the browser, saved to flash** (NVS) — change the network name, edit the prank messages, and toggle the prank on/off. Survives reboots, no re-flashing.
- **PIN-locked admin** — the dashboard, stats API, settings, CSV export and clear are all behind a login (HTTP Basic Auth). The prank page itself stays open so victims aren't blocked.
- **`prank.local` via mDNS** — reach the dashboard by name instead of memorizing the IP.
- **CSV export** of the victim table, plus a one-click **Clear log**.

---

## 🧰 Hardware

- An **ESP32-S3** board (e.g. ESP32-S3 DevKitC / any "ESP32S3 Dev Module").
- A USB cable. That's it — everything runs on the chip.

> It will also build on other ESP32 variants with minor or no changes, but it's written and tested against the ESP32-S3.

---

## 📦 Dependencies

All bundled with the Arduino-ESP32 core — nothing extra to install:

- `WiFi`
- `DNSServer`
- `WebServer`
- `Preferences`
- `ESPmDNS`

**Requires Arduino-ESP32 core 3.x** (the WiFi AP event names rely on it).

---

## 🚀 Getting started

1. **Install the board support.** In Arduino IDE → *Boards Manager* → install **esp32 by Espressif** (3.x).
2. **Select the board.** *Tools → Board →* **ESP32S3 Dev Module**.
3. **Open** `PrankPortal_ESP32S3.ino`.
4. **Flash** it to your board.
5. Open the **Serial Monitor** at `115200` baud to see the AP name, portal IP and dashboard URLs.

---

## 📱 Using it

1. On your phone or laptop, join the WiFi network **`Free_Public_WiFi`** (the default name).
2. Anyone who joins gets the captive portal and the GOTCHA reveal automatically.
3. To watch the action, open a **normal browser** (not the little captive-portal popup) and go to:

   ```
   http://prank.local/dashboard
   ```
   or, as a fallback (always works):
   ```
   http://4.3.2.1/dashboard
   ```
4. **Log in** — username `admin`, default PIN `1234`.
5. **Change the PIN** in Settings immediately. While you're there you can rename the network, edit the messages, and flip the prank on/off — all saved to flash.

---

## ⚙️ Configuration

Most settings live in the **dashboard Settings panel** (saved to flash). A few constants at the top of the sketch are compile-time only:

| Constant | Default | Meaning |
|---|---|---|
| `AP_CHANNEL` | `1` | WiFi channel (1, 6 or 11 are cleanest) |
| `AP_MAXCONN` | `8` | Max simultaneous clients |
| `ADMIN_PATH` | `/dashboard` | Dashboard URL path |
| `ADMIN_USER` | `admin` | Dashboard login username |
| `MDNS_HOST` | `prank` | mDNS name → `prank.local` |

Editable at runtime (stored in NVS, survive reboots):

| Setting | Default | Notes |
|---|---|---|
| Network name (SSID) | `Free_Public_WiFi` | Changing it restarts the AP — reconnect to the new name |
| Prank messages | 6 built-in lines | Up to 12, one per line. `&#128521;`-style HTML entities render as emoji |
| Prank enabled | `on` | When off, `/` serves a plain "no internet" page and stops counting |
| Dashboard PIN | `1234` | **Change this.** Blank field in Settings = keep current |

---

## 🌐 HTTP endpoints

| Path | Auth | Purpose |
|---|---|---|
| `/` | open | The prank captive page (or a plain page when disabled) |
| `/dashboard` | 🔒 | Live admin dashboard |
| `/api/stats` | 🔒 | JSON stats, polled every 2s by the dashboard |
| `/save` (POST) | 🔒 | Save settings to flash |
| `/export.csv` | 🔒 | Download the victim table as CSV |
| `/clear` | 🔒 | Wipe the in-memory log |
| OS probe URLs | open | `/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`, etc. → redirect to portal |

---

## 🖼️ Screenshots

> Add your own captures here.

| Captive reveal | Live dashboard |
|---|---|
| `docs/gotcha.png` | `docs/dashboard.png` |

---

## 🔍 How it works

- The board runs as a **softAP** at `4.3.2.1` with a wildcard **DNS server** that points every lookup back to itself — that's what triggers the OS captive-portal sheet.
- The portal page is streamed from `PROGMEM` in chunks (low heap, no big allocations), with the rotating message injected between a fixed head and tail.
- A `WiFi.onEvent` handler tracks **connect / disconnect / IP-assigned** events to build the device table; page views are attributed by IP.
- The dashboard is a single static shell; a 2-second `fetch('/api/stats')` rewrites only the table body and stat numbers, so nothing flickers.
- Config persists through the **Preferences** (NVS) library.

---

## ⚠️ Limitations & notes

- **The victim log is RAM-only** — it clears on reboot or power loss. Export the CSV if you want to keep a session. (Config *does* persist.)
- **HTTP Basic Auth is not encrypted** — the PIN is base64-encoded over plain HTTP. It's enough to keep curious victims out of your dashboard; it is *not* bank-grade. Don't reuse a real password as the PIN.
- **`prank.local` resolution varies** — reliable on iOS/macOS and Windows (with Bonjour); flaky on some Android versions. The IP always works.
- Device tracking holds up to **24 devices**; the stalest offline entry is evicted when full.

---

## 🗺️ Ideas / roadmap

Not built, but natural next steps:

- OLED scoreboard (live count + last-MAC ticker)
- Persist the victim log to flash/SD (pairs well with a dual AP+STA NTP clock for real timestamps)
- WebSocket push instead of polling
- Reaction buttons / mini guestbook on the gotcha page
- RSSI proximity column, dwell-time stats, message-effectiveness leaderboard
- Auto-disable timer, per-device prank cap, SSID rotation
- ESP-NOW aggregation across multiple boards into one dashboard
- OTA updates, installable PWA dashboard

---

## 📄 License

Released under the **MIT License**. See [`LICENSE`](LICENSE).

## 🙌 Credits

Built on the Arduino-ESP32 core. Made for fun and a little security awareness.
