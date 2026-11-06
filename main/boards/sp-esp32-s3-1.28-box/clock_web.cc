// round-box-clock: see clock_web.h.
#include "clock_web.h"

#include "board.h"
#include "backlight.h"
#include "clock_overlay.h"
#include "clock_sd.h"
#include "clock_settings.h"

#include <dirent.h>
#include <sys/stat.h>
#include <cctype>
#include <cstdlib>

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mdns.h>

#include <cstdio>
#include <cstring>
#include <string>

#define TAG "ClockWeb"

namespace clock_web {
namespace {

// ---------------------------------------------------------------- page
const char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Round Box Clock</title>
<style>
/* Three Oak Woods palette (threeoakwoods-com): forest, deep, paper, ink, gold */
:root{--bg:#f5f1e6;--card:#fffefb;--ink:#2b3a33;--muted:#58665e;--line:#e3d9c3;--accent:#2c654b;--accent-hover:#214d3b;--gold:#8a5a12;--pale:#eaf0e8}
@media (prefers-color-scheme:dark){:root{--bg:#1c2621;--card:#24322c;--ink:#f0ece0;--muted:#a9b5ad;--line:#35473e;--accent:#4f9a76;--accent-hover:#5fae88;--gold:#e6c692;--pale:#2b3d34}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.55 "Segoe UI",system-ui,sans-serif}
main{max-width:560px;margin:0 auto;padding:22px 16px 48px}
h1{font:800 1.75rem/1.18 Nunito,"Segoe UI",system-ui,sans-serif;letter-spacing:-.02em;margin:.2em 0 .1em}h1 small{display:block;font:600 .85rem "Segoe UI",system-ui,sans-serif;color:var(--muted);letter-spacing:0;margin-top:4px}
.eyebrow{color:var(--gold);font-size:11px;letter-spacing:.18em;text-transform:uppercase;font-weight:700;margin:0 0 6px}
section{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px 18px;margin:16px 0}
h2{font-size:11px;margin:0 0 10px;letter-spacing:.16em;text-transform:uppercase;color:var(--gold);font-weight:700}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:8px 0;border-top:1px solid var(--line)}
.row:first-of-type{border-top:0}label{flex:1}
input[type=color]{width:52px;height:36px;border:1px solid var(--line);border-radius:8px;background:none;padding:2px}
select,input[type=range]{font:inherit}select{padding:4px 8px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--ink)}
input[type=range]{width:180px;accent-color:var(--accent)}
.dials{display:flex;gap:18px;justify-content:center;margin:4px 0 8px}
.dial{width:96px;height:96px;border-radius:50%;display:grid;place-items:center;font:700 1.1rem Georgia,serif;letter-spacing:.08em;border:4px solid var(--accent)}
input[type=checkbox]{width:20px;height:20px;accent-color:var(--accent)}
.dial span{font-size:.7rem;display:block;text-align:center;letter-spacing:.04em;opacity:.8}
button{font:inherit;font-weight:600;border:1px solid var(--accent);border-radius:8px;padding:9px 18px;background:var(--accent);color:#fff;cursor:pointer;transition:background .15s}
button:hover{background:var(--accent-hover)}
button.ghost{background:none;color:var(--accent);border-color:var(--line)}button.ghost:hover{background:var(--pale)}
button:focus-visible,input:focus-visible,select:focus-visible{outline:3px solid var(--accent);outline-offset:2px}
.actions{display:flex;gap:10px;justify-content:flex-end;margin-top:12px}
#msg{min-height:1.3em;color:var(--muted);font-size:.9rem;text-align:right}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;margin:0;font-size:.92rem}dt{color:var(--muted)}dd{margin:0;word-break:break-word}
</style></head><body><main>
<p class="eyebrow">Three Oak Woods</p>
<h1>Round Box Clock<small id="sub">roundclock.local</small></h1>

<section><h2>Face</h2>
<div class="dials"><div class="dial" id="pvDay">XII<span>day</span></div><div class="dial" id="pvNight">XII<span>night</span></div></div>
<div class="row"><label for="face">Face colour <small>(becomes the hands at night)</small></label><input type="color" id="face"></div>
<div class="row"><label for="ink">Hands &amp; numerals</label><input type="color" id="ink"></div>
<div class="row"><label for="secs">Second hand</label><input type="checkbox" id="secs"></div>
</section>

<section><h2>Day &amp; night</h2>
<div class="row"><label for="ds">Day face from</label><select id="ds"></select></div>
<div class="row"><label for="ns">Night face from</label><select id="ns"></select></div>
</section>

<section><h2>Display</h2>
<div class="row"><label for="bl">Brightness <b id="blv"></b></label><input type="range" id="bl" min="5" max="100" step="5"></div>
</section>

<div class="actions"><button class="ghost" id="reset">Defaults</button><button id="save">Save</button></div>
<div id="msg"></div>

<section><h2>Pictures on the SD card</h2><div id="imgs"><small>Loading…</small></div></section>

<section><h2>Status</h2><dl id="st"><dt>Loading</dt><dd>…</dd></dl></section>
</main>
<script>
const $=id=>document.getElementById(id);
const hex=n=>'#'+n.toString(16).padStart(6,'0');
const num=s=>parseInt(s.slice(1),16);
const DEF={face:0xCBB07E,ink:0x1C1A18,day_start:7,night_start:19,show_seconds:true,brightness:50};
for(const id of['ds','ns'])for(let h=0;h<24;h++)$(id).add(new Option(String(h).padStart(2,'0')+':00',h));
function preview(){const f=$('face').value,i=$('ink').value;
 Object.assign($('pvDay').style,{background:f,color:i});Object.assign($('pvNight').style,{background:'#000',color:f});
 $('blv').textContent=$('bl').value+'%';}
function fill(s){$('face').value=hex(s.face);$('ink').value=hex(s.ink);$('secs').checked=s.show_seconds;
 $('ds').value=s.day_start;$('ns').value=s.night_start;$('bl').value=s.brightness;preview();}
async function load(){const r=await fetch('/api/settings');fill(await r.json());}
async function save(){const body={face:num($('face').value),ink:num($('ink').value),show_seconds:$('secs').checked,
 day_start:+$('ds').value,night_start:+$('ns').value,brightness:+$('bl').value};
 $('msg').textContent='Saving…';
 const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
 $('msg').textContent=r.ok?'Saved. The clock updates within a second.':'Not saved: '+await r.text();}
const fmtUp=s=>{const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+h+'h '+m+'m';};
async function status(){try{const s=await(await fetch('/api/status')).json();
 const sd=s.sd.mounted?`${s.sd.name} · ${s.sd.size_mb} MB ${s.sd.type}`+(s.sd.fs_free_mb!=null?` · ${s.sd.fs_free_mb} MB free`:''):'not mounted ('+s.sd.error+')';
 const rows=[['Firmware',s.version+' · built '+s.built],['IDF',s.idf],['Uptime',fmtUp(s.uptime_s)],['IP',s.ip],['Wi-Fi',s.rssi+' dBm'],
  ['Battery',s.battery_level<0?'n/a':s.battery_level+'%'+(s.charging?' (charging)':'')],['Memory',`${s.heap_kb} KB internal · ${s.psram_kb} KB PSRAM free`],['SD card',sd]];
 if(s.sd.mounted&&s.sd.root)rows.push(['SD files',s.sd.root.map(e=>e.name+(e.dir?'/':'')).join(', ')||'(empty)']);
 $('st').innerHTML=rows.map(([k,v])=>`<dt>${k}</dt><dd>${String(v).replace(/</g,'&lt;')}</dd>`).join('');}catch(e){}}
['face','ink','bl'].forEach(id=>$(id).addEventListener('input',preview));
$('save').onclick=save;$('reset').onclick=()=>{fill(DEF);$('msg').textContent='Defaults loaded. Press Save to apply.';};
async function images(){try{const r=await(await fetch('/api/sd/images')).json();const shown=r.images.filter(i=>i.name.endsWith('.rgb565'));
 $('imgs').innerHTML=shown.length?shown.map(i=>`<div class="row"><label>${i.name.replace(/</g,'&lt;')}</label><button class="ghost" data-n="${encodeURIComponent(i.name)}">Show 10 s</button></div>`).join(''):'<small>No .rgb565 pictures in /images yet.</small>';
 $('imgs').querySelectorAll('button').forEach(b=>b.onclick=async()=>{const r=await fetch('/api/display/show?secs=10&name='+b.dataset.n,{method:'POST'});$('msg').textContent=r.ok?'Showing on the clock for 10 s.':'Show failed: '+await r.text();});}catch(e){$('imgs').innerHTML='<small>SD card not available.</small>';}}
load();status();images();setInterval(status,5000);
</script></body></html>)HTML";

// ---------------------------------------------------------------- handlers
esp_err_t SendJson(httpd_req_t* req, cJSON* root) {
    char* s = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, s ? s : "{}");
    cJSON_free(s);
    cJSON_Delete(root);
    return err;
}

int CurrentBrightness() {
    Backlight* bl = Board::GetInstance().GetBacklight();
    return bl ? bl->brightness() : -1;
}

esp_err_t IndexHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kIndexHtml, sizeof(kIndexHtml) - 1);
}

esp_err_t GetSettingsHandler(httpd_req_t* req) {
    const ClockSettings::Values v = ClockSettings::Get().Load();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "face", v.face);
    cJSON_AddNumberToObject(root, "ink", v.ink);
    cJSON_AddNumberToObject(root, "day_start", v.day_start);
    cJSON_AddNumberToObject(root, "night_start", v.night_start);
    cJSON_AddBoolToObject(root, "show_seconds", v.show_seconds);
    cJSON_AddNumberToObject(root, "brightness", CurrentBrightness());
    return SendJson(req, root);
}

esp_err_t PostSettingsHandler(httpd_req_t* req) {
    char body[512];
    if (req->content_len <= 0 || req->content_len >= static_cast<int>(sizeof(body))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large or empty");
        return ESP_FAIL;
    }
    int got = 0;
    while (got < static_cast<int>(req->content_len)) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "read failed");
            return ESP_FAIL;
        }
        got += r;
    }
    body[got] = '\0';

    cJSON* root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }
    ClockSettings::Values v = ClockSettings::Get().Load();
    auto get_int = [&](const char* key, int& out) {
        cJSON* item = cJSON_GetObjectItem(root, key);
        if (cJSON_IsNumber(item)) out = item->valueint;
    };
    int face = static_cast<int>(v.face), ink = static_cast<int>(v.ink), brightness = -1;
    get_int("face", face);
    get_int("ink", ink);
    get_int("day_start", v.day_start);
    get_int("night_start", v.night_start);
    get_int("brightness", brightness);
    cJSON* secs = cJSON_GetObjectItem(root, "show_seconds");
    if (cJSON_IsBool(secs)) v.show_seconds = cJSON_IsTrue(secs);
    cJSON_Delete(root);

    if (face < 0 || ink < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad colour");
        return ESP_FAIL;
    }
    v.face = static_cast<uint32_t>(face);
    v.ink = static_cast<uint32_t>(ink);
    if (!ClockSettings::Get().Save(v)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "out of range (hours 0-23 and different; colours 000000-FFFFFF)");
        return ESP_FAIL;
    }
    if (brightness >= 5 && brightness <= 100) {
        if (Backlight* bl = Board::GetInstance().GetBacklight()) {
            bl->SetBrightness(static_cast<uint8_t>(brightness), true);  // persisted, like the voice tool
        }
    }
    ESP_LOGI(TAG, "settings saved: face %06lx ink %06lx day %d night %d secs %d bl %d",
             static_cast<unsigned long>(v.face), static_cast<unsigned long>(v.ink),
             v.day_start, v.night_start, v.show_seconds, brightness);
    return GetSettingsHandler(req);
}

esp_err_t StatusHandler(httpd_req_t* req) {
    const esp_app_desc_t* app = esp_app_get_description();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", app->version);
    char built[40];
    snprintf(built, sizeof(built), "%s %s", app->date, app->time);
    cJSON_AddStringToObject(root, "built", built);
    cJSON_AddStringToObject(root, "idf", app->idf_ver);
    cJSON_AddNumberToObject(root, "uptime_s", static_cast<double>(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "heap_kb", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    cJSON_AddNumberToObject(root, "psram_kb", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);

    char ip[16] = "?";
    if (esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(netif, &info) == ESP_OK) {
            snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
        }
    }
    cJSON_AddStringToObject(root, "ip", ip);
    wifi_ap_record_t ap;
    cJSON_AddNumberToObject(root, "rssi", esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0);

    int level = -1;
    bool charging = false, discharging = false;
    if (!Board::GetInstance().GetBatteryLevel(level, charging, discharging)) level = -1;
    cJSON_AddNumberToObject(root, "battery_level", level);
    cJSON_AddBoolToObject(root, "charging", charging);
    cJSON_AddNumberToObject(root, "brightness", CurrentBrightness());

    cJSON* sd = cJSON_Parse(clock_sd::StatusJson().c_str());
    cJSON_AddItemToObject(root, "sd", sd ? sd : cJSON_CreateObject());
    return SendJson(req, root);
}

// ---------------------------------------------------------------- SD images
constexpr const char* kImageDir = "/sdcard/images";
constexpr size_t kMaxUpload = 2 * 1024 * 1024;

bool ValidImageName(const std::string& n) {
    if (n.empty() || n.size() > 40 || n[0] == '.') return false;
    for (char c : n) {
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) return false;
    }
    const auto dot = n.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string ext = n.substr(dot);
    return ext == ".rgb565" || ext == ".png" || ext == ".jpg" || ext == ".bmp";
}

std::string QueryParam(httpd_req_t* req, const char* key) {
    char query[128];
    char val[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return "";
    if (httpd_query_key_value(query, key, val, sizeof(val)) != ESP_OK) return "";
    return val;
}

esp_err_t Fail(httpd_req_t* req, httpd_err_code_t code, const char* msg) {
    httpd_resp_send_err(req, code, msg);
    return ESP_FAIL;
}

esp_err_t UploadHandler(httpd_req_t* req) {
    if (!clock_sd::IsMounted()) return Fail(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
    const std::string name = QueryParam(req, "name");
    if (!ValidImageName(name)) return Fail(req, HTTPD_400_BAD_REQUEST, "name: [A-Za-z0-9._-], max 40, .rgb565/.png/.jpg/.bmp");
    if (req->content_len == 0 || req->content_len > kMaxUpload) return Fail(req, HTTPD_400_BAD_REQUEST, "empty or larger than 2 MB");

    mkdir(kImageDir, 0775);  // fine if it exists
    const std::string path = std::string(kImageDir) + "/" + name;
    const std::string tmp = path + ".part";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return Fail(req, HTTPD_500_INTERNAL_SERVER_ERROR, "cannot create file");
    char* buf = static_cast<char*>(malloc(4096));
    size_t left = req->content_len;
    bool ok = buf != nullptr;
    while (ok && left > 0) {
        int r = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0 || fwrite(buf, 1, r, f) != static_cast<size_t>(r)) ok = false;
        else left -= r;
    }
    free(buf);
    fclose(f);
    if (!ok) {
        remove(tmp.c_str());
        return Fail(req, HTTPD_500_INTERNAL_SERVER_ERROR, "upload interrupted or write failed");
    }
    remove(path.c_str());
    rename(tmp.c_str(), path.c_str());
    ESP_LOGI(TAG, "uploaded %s (%u bytes)", path.c_str(), static_cast<unsigned>(req->content_len));
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "saved", path.c_str());
    cJSON_AddNumberToObject(root, "bytes", req->content_len);
    return SendJson(req, root);
}

esp_err_t ListImagesHandler(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_AddArrayToObject(root, "images");
    if (DIR* dir = opendir(kImageDir)) {
        while (dirent* e = readdir(dir)) {
            if (e->d_type == DT_DIR || !ValidImageName(e->d_name)) continue;
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", e->d_name);
            struct stat st;
            const std::string p = std::string(kImageDir) + "/" + e->d_name;
            if (stat(p.c_str(), &st) == 0) cJSON_AddNumberToObject(item, "bytes", static_cast<double>(st.st_size));
            cJSON_AddItemToArray(arr, item);
        }
        closedir(dir);
    }
    return SendJson(req, root);
}

esp_err_t ShowHandler(httpd_req_t* req) {
    const std::string name = QueryParam(req, "name");
    if (!ValidImageName(name)) return Fail(req, HTTPD_400_BAD_REQUEST, "bad name");
    const std::string secs_s = QueryParam(req, "secs");
    const int secs = secs_s.empty() ? 10 : atoi(secs_s.c_str());
    const std::string err = clock_overlay::ShowRgb565File(std::string(kImageDir) + "/" + name, secs);
    if (!err.empty()) return Fail(req, HTTPD_400_BAD_REQUEST, err.c_str());
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "showing", name.c_str());
    cJSON_AddNumberToObject(root, "secs", secs);
    return SendJson(req, root);
}

// ---------------------------------------------------------------- startup
void StartServices() {
    if (esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        esp_netif_set_hostname(netif, kHostname);  // DHCP hostname; takes effect on the next lease
    }

    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        mdns_hostname_set(kHostname);
        mdns_instance_name_set("Round Box Clock");
        mdns_service_add("Round Box Clock", "_http", "_tcp", 80, nullptr, 0);
        ESP_LOGI(TAG, "mDNS: %s.local", kHostname);
    } else {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32769;      // keep clear of any other httpd instance's default ctrl port
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 12;
    config.recv_wait_timeout = 15;  // uploads
    httpd_handle_t server = nullptr;
    err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return;
    }
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = IndexHandler, .user_ctx = nullptr},
        {.uri = "/api/settings", .method = HTTP_GET, .handler = GetSettingsHandler, .user_ctx = nullptr},
        {.uri = "/api/settings", .method = HTTP_POST, .handler = PostSettingsHandler, .user_ctx = nullptr},
        {.uri = "/api/status", .method = HTTP_GET, .handler = StatusHandler, .user_ctx = nullptr},
        {.uri = "/api/sd/upload", .method = HTTP_POST, .handler = UploadHandler, .user_ctx = nullptr},
        {.uri = "/api/sd/images", .method = HTTP_GET, .handler = ListImagesHandler, .user_ctx = nullptr},
        {.uri = "/api/display/show", .method = HTTP_POST, .handler = ShowHandler, .user_ctx = nullptr},
    };
    for (const auto& u : uris) {
        httpd_register_uri_handler(server, &u);
    }
    ESP_LOGI(TAG, "web settings at http://%s.local/", kHostname);
}

void WaitTask(void*) {
    for (;;) {
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t info = {};
        if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    StartServices();
    vTaskDelete(nullptr);
}

}  // namespace

void StartWhenNetworkReady() {
    static bool started = false;
    if (started) return;
    started = true;
    xTaskCreate(WaitTask, "clock_web_wait", 4096, nullptr, 3, nullptr);
}

}  // namespace clock_web
