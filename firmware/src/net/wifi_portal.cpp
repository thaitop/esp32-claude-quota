#include "wifi_portal.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstring>

#include "config_store.h"

namespace net {
namespace {

// ---------------------------------------------------------------------------
// The page. One file, inlined CSS and JS, no external requests -- served in AP
// mode to a phone that has no internet at all, so a CDN stylesheet or script
// would simply never load. Two fields and a Save; there is nothing to validate
// on the wire (no uplink), so the only feedback is "saved, rebooting". The list
// of nearby networks is offered as a datalist the /scan endpoint fills, so a
// user who is unsure of the exact SSID spelling can pick it -- but the field
// stays free text, because a hidden SSID will not appear in a scan.
// ---------------------------------------------------------------------------
const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Setup</title>
<style>
:root{color-scheme:dark}
body{font-family:system-ui,sans-serif;margin:0;background:#0f1115;color:#e6e6e6}
main{max-width:420px;margin:0 auto;padding:20px}
h1{font-size:1.3rem}p{color:#9aa4b2;font-size:.9rem}
.row{margin:12px 0}
label{display:block;color:#9aa4b2;font-size:.85rem;margin-bottom:4px}
input{width:100%;box-sizing:border-box;padding:10px;border:1px solid #2a2f3a;
border-radius:8px;background:#161a22;color:#e6e6e6;font-size:1rem}
button{padding:10px 16px;border:0;border-radius:8px;font-size:1rem;
cursor:pointer;margin:12px 6px 0 0}
.pri{background:#4f7cff;color:#fff}.sec{background:#2a2f3a;color:#e6e6e6}
.msg{font-size:.85rem;margin-top:12px;min-height:1em}
.ok{color:#4fd18b}.err{color:#ff6b6b}
</style></head><body><main>
<h1>WiFi Setup</h1>
<p>Enter the network this display should join, then Save. The device reboots
and tries to connect.</p>
<div class="row"><label>Network name (SSID)</label>
<input id="ssid" list="nets" autocomplete="off" placeholder="your WiFi name">
<datalist id="nets"></datalist></div>
<div class="row"><label>Password</label>
<input id="pass" type="password" placeholder="leave blank for open networks"></div>
<button class="pri" onclick="save()">Save &amp; reboot</button>
<button class="sec" onclick="quit()">Exit without saving</button>
<div id="msg" class="msg"></div>
<script>
const $=id=>document.getElementById(id);
async function scan(){try{
const n=await(await fetch("/scan")).json();
$("nets").innerHTML=n.map(s=>`<option value="${s.replace(/"/g,"&quot;")}">`).join("");
}catch(e){}}
async function save(){
const ssid=$("ssid").value.trim();
if(!ssid){$("msg").textContent="Enter a network name.";$("msg").className="msg err";return;}
$("msg").textContent="Saving…";$("msg").className="msg";
await fetch("/save",{method:"POST",
body:new URLSearchParams({ssid:ssid,pass:$("pass").value})});
$("msg").textContent="Saved. Device is rebooting to join \""+ssid+"\" — this page will stop responding.";
$("msg").className="msg ok";}
async function quit(){await fetch("/quit",{method:"POST"});
$("msg").textContent="Rebooting without saving…";$("msg").className="msg";}
scan();
</script></main></body></html>)HTML";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
WebServer server(80);
bool active = false;
bool rebootRequested = false;
char apSsid[24] = {0};
char apPass[9] = {0};
char url[24] = {0};

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------
void handleRoot() { server.send_P(200, "text/html", PAGE); }

// The nearby networks, for the SSID datalist. A scan in AP mode momentarily
// puts the radio into AP+STA; it does not drop the AP the phone is on. Returns
// a plain JSON array of unique names, hidden/blank ones dropped.
void handleScan() {
  const int found = WiFi.scanNetworks();
  String out = "[";
  bool first = true;
  for (int i = 0; i < found; i++) {
    const String name = WiFi.SSID(i);
    if (name.isEmpty()) continue;
    // Skip an SSID already emitted -- scans list every BSSID, so a mesh shows
    // the same name several times.
    bool dup = false;
    for (int j = 0; j < i; j++) {
      if (WiFi.SSID(j) == name) { dup = true; break; }
    }
    if (dup) continue;
    if (!first) out += ',';
    first = false;
    String esc = name;
    esc.replace("\\", "\\\\");
    esc.replace("\"", "\\\"");
    out += '"';
    out += esc;
    out += '"';
  }
  out += ']';
  WiFi.scanDelete();
  server.send(200, "application/json", out);
}

// Persists the typed credentials and arms the reboot. No validation is possible
// here (no uplink) -- WPA2 either accepts them on the next boot or the device
// lands back in this same portal, which is the recovery path, not a failure to
// guard against now.
void handleSave() {
  if (!server.hasArg("ssid") || server.arg("ssid").length() == 0)
    return server.send(400, "application/json", "{\"error\":\"ssid\"}");
  configCommitWifi(server.arg("ssid").c_str(), server.arg("pass").c_str());
  server.send(200, "application/json", "{\"saved\":true}");
  rebootRequested = true;
}

void handleQuit() {
  server.send(200, "application/json", "{\"reboot\":true}");
  rebootRequested = true;
}

}  // namespace

void wifiPortalBegin() {
  if (active) return;

  // A name a phone can recognise as this device, salted with the low MAC bytes
  // so two on one desk do not collide. The MAC is read straight off the chip --
  // no radio association needed, unlike WiFi.macAddress() before begin().
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  snprintf(apSsid, sizeof(apSsid), "ClaudeQuota-%02X%02X", mac[4], mac[5]);

  // An 8-digit WPA2 password from the hardware RNG, shown only on the panel:
  // being able to join the AP at all is the whole gate, so the HTTP server
  // itself needs no PIN -- someone in front of the device is the one setting it
  // up. Eight digits meets WPA2's 8-char minimum.
  snprintf(apPass, sizeof(apPass), "%08u", (unsigned)(esp_random() % 100000000u));

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPass);
  const IPAddress ip = WiFi.softAPIP();  // 192.168.4.1 by default
  snprintf(url, sizeof(url), "http://%s/", ip.toString().c_str());

  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/quit", HTTP_POST, handleQuit);
  server.begin();

  active = true;
  rebootRequested = false;
  Serial.printf("wifi setup AP up: ssid=%s pass=%s %s\n", apSsid, apPass, url);
}

bool wifiPortalActive() { return active; }

void wifiPortalService() { server.handleClient(); }

const char *wifiPortalApSsid() { return active ? apSsid : ""; }
const char *wifiPortalApPassword() { return active ? apPass : ""; }
const char *wifiPortalUrl() { return active ? url : ""; }

bool wifiPortalShouldReboot() { return rebootRequested; }

}  // namespace net
