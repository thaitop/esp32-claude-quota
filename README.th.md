# จอมอนิเตอร์โควตา Claude — ESP32-2432S028R

<p align="center">
  <a href="README.md">🇬🇧 English</a> &nbsp;·&nbsp; <b>🇹🇭 ภาษาไทย</b>
</p>

> ต้นฉบับคือ [README.md](README.md) หากมีข้อขัดแย้ง ให้ยึดฉบับอังกฤษ

จอตั้งโต๊ะสำหรับหน้าต่างการใช้งานสองช่วงของ Claude Code พร้อมข้อมูลอากาศ
คริปโต และหุ้น รันบน "Cheap Yellow Display" (ESP32-WROOM-32 + จอ 2.8" 240x320
ILI9341 + ทัชสกรีนแบบ resistive XPT2046) วาดด้วย LVGL 9

| หน้าจอ | แสดง | Feed |
|---|---|---|
| **Claude** | Utilization ของ Session และ Weekly พร้อมนับถอยหลังจนรีเซ็ต | Bridge |
| **Weekly Usage** | Utilization รายสัปดาห์ย้อนหลังเจ็ดวัน เป็นกราฟ | Bridge |
| **Weather** | อุณหภูมิ อุณหภูมิที่รู้สึก สภาพอากาศ ความชื้น | Weather |
| **Crypto** | ราคาเหรียญ การเปลี่ยนแปลง 24 ชม. มูลค่าและปริมาณ; BTC/ETH/BNB | Crypto |
| **Stock** | ห้าตัวเป็นลิสต์ พร้อม badge บอกว่าตลาดเปิดหรือปิด | Stock |
| **Setting** | สถานะลิงก์ สุขภาพแต่ละ feed ความเก่าของโควตา heap uptime ความสว่าง | — |

[CONTEXT.md](CONTEXT.md) นิยามคำศัพท์ที่ทั้งสองฝั่งใช้ร่วมกัน ส่วน
[ADR](docs/adr/) บันทึกสี่การตัดสินใจที่กำหนดกรอบของส่วนที่เหลือ

## หน้าจอต่าง ๆ

<table>
  <tr>
    <td align="center"><img src="docs/images/1-usage-page.jpeg" width="220"><br><b>Claude</b></td>
    <td align="center"><img src="docs/images/2-weekly-page.jpeg" width="220"><br><b>Weekly Usage</b></td>
    <td align="center"><img src="docs/images/3-weather-page.jpeg" width="220"><br><b>Weather</b></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/4-crypto-page.jpeg" width="220"><br><b>Crypto</b></td>
    <td align="center"><img src="docs/images/5-stock-page.jpeg" width="220"><br><b>Stock</b></td>
    <td align="center"><img src="docs/images/6-settings-page.jpeg" width="220"><br><b>Setting</b></td>
  </tr>
</table>

## สิ่งที่ต้องมี

| รายการ | หมายเหตุ |
|---|---|
| **บอร์ด ESP32-2432S028R** | "CYD" ขนาด 2.8" USB เดี่ยว ราคาราว $10 |
| **สาย USB** | Micro-USB แบบ **ส่งข้อมูล** — สายชาร์จอย่างเดียวจะดูเหมือนบอร์ดเสีย |
| **เครื่องที่รัน Claude Code** | แหล่งของตัวเลขโควตา ต้องเปิดค้างและอยู่ LAN เดียวกับจอ ตัวอย่างข้างล่างเป็น macOS ตัว bridge เป็น Python 3.11+ ล้วน |
| **session key ของ claude.ai** | วางลงไฟล์ตอนตั้งค่า (ขั้น 3) |
| **ไดรเวอร์ CH340 USB serial** | มากับ macOS 12+/Linux อยู่แล้ว Windows ต้องลง [ไดรเวอร์ WCH](https://www.wch-ic.com/downloads/CH341SER_EXE.html) |
| **PlatformIO Core** | `pip install -U platformio` — ติดตั้งที่ `~/.local/bin/pio` |

ไม่ต้องบัดกรี ไม่ต้องมีชิ้นส่วนเพิ่ม

## 1. Clone และตั้งค่า

```bash
git clone https://github.com/thaitop/esp32-claude-quota.git
cd esp32-claude-quota
cp firmware/src/secrets.h.example firmware/src/secrets.h
```

แก้ `firmware/src/secrets.h`:

```c
#define WIFI_SSID     "your-network"        // 2.4GHz เท่านั้น — ESP32 ไม่มีวิทยุ 5GHz
#define WIFI_PASSWORD "your-password"
#define BRIDGE_BASE_URL "http://192.168.1.117:8787"   // IP บน LAN ของเครื่องที่รัน Claude Code
#define WEATHER_LATITUDE  13.75f
#define WEATHER_LONGITUDE 100.50f
#define WEATHER_TZ "Asia/Bangkok"           // ชื่อโซนแบบ IANA — Open-Meteo รับแบบอื่นไม่ได้
#define CLOCK_TZ   "UTC+7"                   // ออฟเซ็ตนาฬิกาบนหัวจอ เช่น "UTC-5", "UTC+5:30", "UTC"
#define FINNHUB_TOKEN "your-finnhub-token"  // คีย์ฟรีจาก finnhub.io/register — ใช้เฉพาะหน้า Stock
```

Finnhub token เป็น credential อ่อน (อ่าน quote สาธารณะเท่านั้น) ถ้าปล่อย
placeholder ไว้ หน้า Stock จะแสดง `--` หา IP ของเครื่องด้วย
`ipconfig getifaddr en0` (macOS) หรือ `hostname -I` (Linux) — ใช้ IP ดีกว่า
`.local` และตั้ง DHCP reservation ให้ จะได้ไม่ต้องแฟลชเฟิร์มแวร์ใหม่เมื่อ IP เปลี่ยน

`secrets.h` อยู่ใน gitignore ที่เหลือ — poll interval, timeout, เหรียญ, หุ้น,
เกณฑ์สี, ค่าคาลิเบรตทัช — อยู่ใน `firmware/src/config.h` ซึ่ง commit ไว้ ทำไม
token กับ TZ ถึงอยู่ใน `secrets.h` ดูที่
[ADR-0004](docs/adr/0004-stock-feed-soft-credential.md)

## 2. แฟลช

```bash
cd firmware
~/.local/bin/pio run --target upload > /tmp/upload.log 2>&1; echo "exit=$?"
grep -E "Hash of data|Hard resetting|SUCCESS|FAILED" /tmp/upload.log
```

เขียนลงไฟล์ก่อน — ถ้า pipe เข้า `tail`/`grep` มันตัดจากปลายผิดด้าน บรรทัด
`SUCCESS` เลยไม่โผล่ ดูเหมือนแฟลชล้มเหลว build แรกดึง toolchain กับไลบรารี
(ไม่กี่นาที) build ถัดไปราว 1 นาที อัปโหลดอีกราว 50 วินาที PlatformIO เลือกพอร์ต
ให้เอง ถ้าต่อสองบอร์ดให้ระบุด้วย `--upload-port /dev/cu.usbserial-XXXX`

จากนั้นดูมันบูต:

```bash
~/.local/bin/pio device monitor
```

Weather กับ Crypto ทำงานทันทีที่ WiFi ขึ้น ส่วนสองหน้า Claude จะค้างที่ `--`
จนกว่า bridge จะรัน

## 3. ตั้งค่าแหล่งโควตา

`bridge/fetch_usage.py` เป็นตัวดึงค่า utilization ป้อน session key ให้มัน:

```bash
mkdir -p ~/.config/claude-quota
pbpaste > ~/.config/claude-quota/session-key   # หรือวางในโปรแกรมแก้ไขข้อความ
chmod 600 ~/.config/claude-quota/session-key
```

ค่านั้นคือคุกกี้ `sessionKey` จากเบราว์เซอร์ที่ล็อกอิน claude.ai อยู่ — ขึ้นต้น
ด้วย `sk-ant-sid01-` การล็อกอินแบบ OAuth ของ Claude Code ใช้ **ไม่ได้** —
endpoint ตัวนี้ยืนยันตัวตนด้วยคุกกี้ สคริปต์จะปฏิเสธไฟล์คีย์ที่ group หรือ world
อ่านได้ ดังนั้น `chmod` ไม่ใช่ทางเลือก

### ขั้นตอนการหา session key

1. **เข้าสู่ระบบ** เปิด [claude.ai](https://claude.ai) ในเบราว์เซอร์แล้วลงชื่อเข้าใช้ให้เรียบร้อย
2. **เปิด Developer Tools**
   - Chrome / Edge / Brave: กด `F12` หรือ `Cmd+Option+I` (macOS) / `Ctrl+Shift+I` (Windows)
   - Firefox: กด `F12` หรือ `Cmd+Option+I` (macOS) / `Ctrl+Shift+I` (Windows)
   - Safari: เปิดใช้งานก่อนที่ Settings → Advanced → *Show features for web developers* แล้วกด `Cmd+Option+I`
3. **ไปที่คุกกี้ (Cookies)**
   - Chrome / Edge / Brave: แท็บ **Application** → ขยาย **Cookies** ทางซ้าย → คลิก `https://claude.ai`
   - Firefox: แท็บ **Storage** → ขยาย **Cookies** → คลิก `https://claude.ai`
   - Safari: แท็บ **Storage** → **Cookies** → คลิก `https://claude.ai`
4. **คัดลอกค่า** หาคุกกี้ชื่อ `sessionKey` แล้วคัดลอก **Value** (ข้อความ `sk-ant-sid01-…`) มาใส่ในไฟล์คีย์ข้างบน

ดูแลค่านี้เหมือนรหัสผ่านของคุณ — มันคือ credential ของทั้งบัญชี ไม่ใช่ API key
แบบจำกัดสิทธิ์ (ดู [หมายเหตุความปลอดภัย](#หมายเหตุความปลอดภัย))

```bash
python3 bridge/fetch_usage.py --check    # ตรวจ path/สิทธิ์ ไม่พิมพ์คีย์ออกมา
python3 bridge/fetch_usage.py            # เขียน bridge/usage-cache หนึ่งครั้ง
python3 bridge/fetch_usage.py --interval # วนทุก 60 วินาที (โหมด production)
```

ใช้ Python ที่มี TLS trust store ใช้งานได้ — ของ Homebrew ไม่ใช่ build จาก
python.org (ซึ่งจะ handshake ล้มด้วย "no CA certificates") ใส่ `--org-id` ถ้า
คุณอยู่หลายองค์กร

ใช้ [Claude Usage Tracker](https://github.com/hamed-elfayome/Claude-Usage-Tracker)
อยู่แล้ว? ชี้ bridge ไปที่ cache ของมันแล้วข้ามขั้นนี้ได้เลย:
`python3 bridge/quota_bridge.py --cache ~/.claude/.statusline-usage-cache`

## 4. สตาร์ต bridge

```bash
/opt/homebrew/bin/python3.13 bridge/quota_bridge.py
# → http://0.0.0.0:8787/quota
```

มันอ่าน cache แล้วเสิร์ฟเป็น JSON บน LAN — ไม่มีการเรียกออกนอก ไม่แตะ
credential ตรวจดู:

```bash
curl -s http://localhost:8787/quota | python3 -m json.tool
curl -s http://<machine-ip>:8787/quota     # จากมือถือหรือแล็ปท็อป
```

Flag: `--port`, `--host`, `--no-tokens` (ข้ามการสแกน jsonl เร็วกว่ามาก),
`--once`, `-v` ถ้า curl ตัวที่สองค้าง แปลว่าไฟร์วอลล์ของเครื่องบล็อกขาเข้า —
อนุญาต Python binary (macOS: System Settings → Network → Firewall)

## 5. ให้ทั้งสองโปรเซสรันค้าง

มีโปรเซสแยกกันสองตัว — fetcher (คุยกับ claude.ai ตามจังหวะช้าๆ) กับ bridge
(เสิร์ฟ LAN) — จึงติดตั้งงาน `launchd` **สองงาน** plist สำเร็จรูปอยู่ใน
`bridge/launchd/` ก็อปทั้งสองไฟล์ไป `~/Library/LaunchAgents/` ตัดนามสกุล
`.example` ออก แล้วตั้ง path ของ repo คุณ:

```bash
cp bridge/launchd/com.local.claude-quota-fetch.plist.example \
   ~/Library/LaunchAgents/com.local.claude-quota-fetch.plist
cp bridge/launchd/com.local.claude-quota-bridge.plist.example \
   ~/Library/LaunchAgents/com.local.claude-quota-bridge.plist

# plist มี path ตัวอย่างฝังอยู่ (/Users/you/esp32-claude-quota)
# บรรทัดนี้เขียนทับด้วย path จริงของ repo ต้องรันจาก root ของ repo ให้ $PWD
# ชี้ที่ checkout — เช็คก่อนด้วย: echo $PWD
sed -i '' "s#/Users/you/esp32-claude-quota#$PWD#" \
  ~/Library/LaunchAgents/com.local.claude-quota-{fetch,bridge}.plist
```

จากนั้นโหลดทั้งคู่แล้วเช็คว่าขึ้น:

```bash
launchctl load ~/Library/LaunchAgents/com.local.claude-quota-fetch.plist
launchctl load ~/Library/LaunchAgents/com.local.claude-quota-bridge.plist
launchctl list | grep claude-quota    # คอลัมน์ที่สองคือ exit code ล่าสุด (0 = โอเค)
```

plist แต่ละตัวชี้ไปที่ wrapper script (`bridge/claude-quota-fetch` /
`bridge/claude-quota-bridge`) ไม่ใช่ `python3.13` ตรงๆ เพื่อให้ Login Items
แสดงชื่ออ่านออกและ stdout ไม่ถูก buffer ไม่มี session key อยู่ใน plist ทั้งคู่ —
มันถูกอ่านจาก `~/.config/claude-quota/session-key` ตอนรัน

หมายเหตุ:

- หลังแก้ plist ให้ `unload` แล้ว `load` ใหม่ — `kickstart -k` ใช้ definition
  ที่ cache ไว้ การแก้เลยเหมือนไม่มีผล
- `KeepAlive` รีสตาร์ตงานที่ตาย ดังนั้นหยุดผ่าน `launchctl unload` ไม่ใช่
  `kill` ไม่งั้นมันเด้งกลับมาทันที
- session key หมดอายุ **ไม่** ทำให้ fetcher ตาย มันวนต่อและ log การถูกปฏิเสธ
  ทุกรอบ วางคีย์ใหม่ลงไฟล์คีย์ จอจะกลับมาภายในหนึ่ง interval ไม่ต้องรีสตาร์ต
- wrapper ตั้ง default เป็น `/opt/homebrew/bin/python3.13` ถ้า Homebrew Python
  ของคุณอยู่ที่อื่น ให้ตั้ง `CLAUDE_QUOTA_PYTHON` ใน plist (dict ชื่อ
  `EnvironmentVariables`) หรือแก้ตัว wrapper
- Linux: ใช้ user systemd unit ที่มี `Restart=always` Windows: Task Scheduler
  ตอน logon

## มันได้ตัวเลขมาอย่างไร

เปอร์เซ็นต์ที่ `/usage` แสดงไม่มีใน public API ตัวไหน เว็บแอปมี endpoint ของ
ตัวเอง ซึ่งเป็นสิ่งที่ตัวนี้อ่าน:

```
GET https://claude.ai/api/organizations/<org-id>/usage
Cookie: sessionKey=<session-key>
```

สองโปรเซส โดยตั้งใจ `fetch_usage.py` ถือ credential เรียก endpoint นั้นทุก 60
วินาที แล้วเขียนไฟล์ KEY=VALUE เล็กๆ:

```
UTILIZATION=89
RESETS_AT=2026-07-20T10:59:59Z
TIMESTAMP=1784557286
WEEKLY_UTILIZATION=31
WEEKLY_RESETS_AT=2026-07-22T17:59:59Z
```

`quota_bridge.py` อ่านไฟล์นั้นแล้วเสิร์ฟเป็น JSON บน LAN การแยกนี้ทำให้จังหวะ
poll ของจอ (20 วินาที) แยกจากการเรียก claude.ai (60 วินาที) จอตัวที่สองจึงไม่
เปลี่ยนอะไรกับสิ่งที่ออกจากเครื่อง และเก็บ credential ของบัญชีไว้นอกโปรเซสที่
หันหน้าเข้า LAN

ถ้า cache หายหรือเก่ากว่า 30 นาที bridge จะรายงาน `"trusted": false` และทุก
ตัวเลขโควตากลายเป็น `--` มันไม่เคยสังเคราะห์เปอร์เซ็นต์จากจำนวน token —
นั่นจะเป็นการเดาที่สวมชุดข้อเท็จจริง
([ADR-0001](docs/adr/0001-never-derive-utilization-from-tokens.md))

Weather คริปโต และหุ้น ถูกดึง **ตรงจากตัวอุปกรณ์** ดังนั้น Mac ที่หลับอยู่จะ
กระทบแค่สองหน้า Claude ([ADR-0002](docs/adr/0002-mixed-feed-topology.md)) มี
คำขอเดียววิ่งอยู่ในเวลาใดก็ตาม — TLS handshake พีคราว 45KB สองอันซ้อนกันไม่พอ
ที่ข้างๆ draw buffer
([ADR-0003](docs/adr/0003-partial-draw-buffer-and-serial-polling.md))

| Feed | รอบ |
|---|---|
| Bridge — quota | 20 วินาที |
| Bridge — history | 10 นาที (ในสล็อตของ Bridge) |
| Weather | 5 นาที |
| Crypto | 60 วินาที |

bridge ยังต่อท้าย Sample ที่เชื่อถือได้ (มากสุดหนึ่งอันต่อช่วง 3 ชม.) ลง
`bridge/history.log` ซึ่งป้อนกราฟ Weekly Usage — 56 Sample ครอบคลุมเจ็ดวัน
เท่าที่ ring ของเฟิร์มแวร์เก็บได้

## การควบคุมด้วยทัช

| ท่า | ผล |
|---|---|
| แตะสล็อตบน navbar | สลับหน้าจอ |
| แตะเหนือ navbar | บังคับรีเฟรชทุก feed ทันที |
| กดค้างราว 1 วินาทีเหนือ navbar | ดับจอ (แตะเพื่อปลุก) |

หน้า Setting เป็นหน้าอ่านอย่างเดียว ไม่ใช่หน้าแก้ไข (ยกเว้น stepper ปรับความ
สว่าง) การเปลี่ยนค่าคอนฟิกต้องแฟลชใหม่

## เกณฑ์สี

| Utilization | สี |
|---|---|
| < 60% | เขียว |
| 60–84% | เหลืองอำพัน |
| ≥ 85% | แดง |

## แก้ปัญหา

- **อัปโหลดต่อไม่ติด** (`No serial data received`) กดปุ่ม BOOT ค้างขณะเริ่ม
  อัปโหลด ถ้าไม่มีพอร์ตขึ้นใน `pio device list` เลย เป็นที่สายหรือไดรเวอร์ CH340
- **อัปโหลดเริ่มแล้วตายกลางคัน** `upload_speed` ตั้งไว้ที่ 230400 แล้ว บางบอร์ด
  อาจต้องลดเป็น 115200 ใน `platformio.ini`
- **จอมืด แต่ serial มีความเคลื่อนไหว** ขา backlight GPIO21 ถูกสำหรับบอร์ดนี้
  CYD รุ่นอื่นใช้ GPIO27 — แก้ `-DTFT_BL=21`
- **สีกลับด้าน** สลับ `-DILI9341_2_DRIVER=1` เป็น `-DILI9341_DRIVER=1`
- **ทัชไม่ตรงจุด** แทนค่าคงที่ `TOUCH_RAW_*` สี่ตัวใน `config.h` ด้วยค่าที่วัดเอง
- **WiFi ไม่ยอมต่อ** 2.4GHz เท่านั้น เครือข่ายแบบ captive portal ใช้ไม่ได้
- **ตัวเลขไหนขึ้น `--`** คือเครื่องหมาย Unknown — แปลว่าค่านั้นเชื่อถือไม่ได้
  ไม่ใช่ศูนย์ หน้า Setting บอกว่า feed ไหนล้มและเก่าแค่ไหน
- **มีแค่สองหน้า Claude ที่ `--` แต่ bridge ปกติ** cache ข้างหลังมันเก่า/หาย
  เกือบทุกครั้งคือ session key หมดอายุ วางคีย์ใหม่ เดี๋ยวมันกลับมารอบถัดไป
  `cat bridge/usage-cache` กับ `python3 bridge/fetch_usage.py` จะโชว์ตัวปัญหา
- **แก้ `include/lv_conf.h` แล้วไม่มีอะไรเปลี่ยน** PlatformIO cache การ build
  LVGL ไว้ — รัน `pio run -t clean` ก่อน
- **บอร์ดรีเซ็ตเมื่อเปิดพอร์ต serial** ปกติ — การ assert DTR คือ power-on reset
  ไม่ใช่การแครช

## หมายเหตุความปลอดภัย

- คุกกี้ `sessionKey` เป็น **credential ระดับบัญชีเต็ม** ไม่ใช่ API key แบบจำกัด
  สิทธิ์ `fetch_usage.py` เป็นตัวเดียวที่เห็นมัน ไม่เคย log หรือส่งผ่านที่ `ps`
  จะเห็น มันอยู่ที่ `~/.config/claude-quota/session-key` นอก repo สิทธิ์ 600
  ดูแลมันเหมือนรหัสผ่าน อย่าเอาใส่ plist หรือ shell profile
- ESP32 เก็บ credential ของ WiFi เป็น plaintext ในแฟลช — บอร์ดที่คุณยกให้คนอื่น
  ให้ถือว่าเป็นบอร์ดที่คุณยกรหัส WiFi ให้ไปด้วย
- bridge เสิร์ฟบน LAN โดยไม่มี auth (เปอร์เซ็นต์และจำนวน token ถ้าเปิด ไม่มี
  credential) ผูกมันไว้กับเครือข่ายที่ไว้ใจได้ อย่า port-forward
- Weather กับคริปโตข้ามการตรวจ certificate โดยตั้งใจ
  ([ADR-0002](docs/adr/0002-mixed-feed-topology.md)) ทั้งคู่ไม่มี credential
- `/api/organizations/<id>/usage` ไม่มีเอกสารและเปลี่ยนได้โดยไม่แจ้ง เมื่อมันพัง
  จอจะแสดง `--` แทนที่จะเป็นตัวเลขเก่า
```
