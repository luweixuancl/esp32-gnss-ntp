#include "web_portal.h"
#include "app_ipc.h"
#include "config.h"
#include "gps_service.h"
#include "ntp_server.h"
#include "ota_service.h"
#include <ArduinoJson.h>
#include <esp_system.h>
#include <math.h>
#include <time.h>

namespace {

String jsonSafeSsid(const String& ssid) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(ssid.c_str());
  const size_t n = ssid.length();
  bool utf8 = true;
  for (size_t i = 0; i < n;) {
    const uint8_t c = p[i];
    if (c < 0x80) {
      ++i;
      continue;
    }
    size_t need = 0;
    if ((c & 0xE0) == 0xC0) {
      need = 2;
    } else if ((c & 0xF0) == 0xE0) {
      need = 3;
    } else if ((c & 0xF8) == 0xF0) {
      need = 4;
    } else {
      utf8 = false;
      break;
    }
    if (i + need > n) {
      utf8 = false;
      break;
    }
    for (size_t k = 1; k < need; ++k) {
      if ((p[i + k] & 0xC0) != 0x80) {
        utf8 = false;
        break;
      }
    }
    if (!utf8) {
      break;
    }
    i += need;
  }
  if (utf8) {
    return ssid;
  }
  String hex;
  hex.reserve(n * 2 + 4);
  hex = "hex:";
  for (size_t i = 0; i < n; ++i) {
    char b[3];
    snprintf(b, sizeof(b), "%02X", p[i]);
    hex += b;
  }
  return hex;
}

}  // namespace

void WebPortal::begin(WifiManager* wifi, GpsService* gps, NtpServer* ntp) {
  wifi_ = wifi;
  gps_ = gps;
  ntp_ = ntp;
  if (started_) {
    return;
  }

  const char* hdrs[] = {"Cookie", "Content-Length"};
  server_.collectHeaders(hdrs, 2);

  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/setup", HTTP_GET, [this]() { handleSetupEntry(); });
  server_.on("/setup/", HTTP_GET, [this]() { handleSetupEntry(); });
  server_.on("/cfg", HTTP_GET, [this]() { handleSetup(); });
  server_.on("/cfg/", HTTP_GET, [this]() { handleSetup(); });
  server_.on("/login", HTTP_GET, [this]() { sendLoginPage(""); });
  server_.on("/login", HTTP_POST, [this]() { handleLogin(); });
  server_.on("/logout", HTTP_GET, [this]() { handleLogout(); });
  server_.on("/scan", HTTP_GET, [this]() { handleScan(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  // Multipart firmware upload (login session). Final handler runs after body.
  server_.on(
      "/ota", HTTP_POST, [this]() { handleOtaDone(); }, [this]() { handleOtaUpload(); });
  server_.on("/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/metrics", HTTP_GET, [this]() { handleMetrics(); });
  server_.onNotFound([this]() {
    sendNoCache();
    const WifiLinkSnapshot link = wifi_ ? wifi_->linkSnapshot() : WifiLinkSnapshot{};
    // Captive probes must not land on settings HTML. SoftAP → login; STA → status.
    if (link.apUp && !link.staUp) {
      server_.sendHeader("Location", "/login", true);
      server_.send(302, "text/plain", "");
      return;
    }
    server_.sendHeader("Location", "/", true);
    server_.send(302, "text/plain", "");
  });
  server_.begin();
  started_ = true;
  Serial.println("HTTP on :80  (/ /status|/status?view=ui /metrics open; /cfg+/ota need login)");
}

void WebPortal::loop() {
  if (started_) {
    server_.handleClient();
  }
}

bool WebPortal::consumeConnectRequest(String& ssid, String& pass) {
  if (!pendingConnect_) {
    return false;
  }
  pendingConnect_ = false;
  ssid = pendingSsid_;
  pass = pendingPass_;
  return true;
}

String WebPortal::writePassword() const {
  String expect;
  AppSettings s;
  if (settingsCopy(pdMS_TO_TICKS(100), &s)) {
    expect = effectiveWebWritePassword(s);
  } else {
    expect = derivedSoftApPassword();
  }
  if (expect.isEmpty()) {
    expect = derivedSoftApPassword();
  }
  return expect;
}

void WebPortal::sendNoCache() {
  server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server_.sendHeader("Pragma", "no-cache");
}

void WebPortal::issueSession() {
  char tok[17];
  snprintf(tok, sizeof(tok), "%08x%08x", static_cast<unsigned>(esp_random()),
           static_cast<unsigned>(esp_random()));
  sessionToken_ = tok;
  sessionUntilMs_ = millis() + 30UL * 60UL * 1000UL;
  String cookie = String("ntp_sess=") + sessionToken_ +
                  "; Path=/; Max-Age=1800; HttpOnly; SameSite=Lax";
  server_.sendHeader("Set-Cookie", cookie);
}

bool WebPortal::sessionCookieOk() {
  if (sessionToken_.isEmpty() || sessionUntilMs_ == 0) {
    return false;
  }
  if (static_cast<int32_t>(millis() - sessionUntilMs_) >= 0) {
    return false;
  }
  const String cookie = server_.header("Cookie");
  const String needle = String("ntp_sess=") + sessionToken_;
  const int pos = cookie.indexOf(needle);
  if (pos < 0) {
    return false;
  }
  if (pos > 0) {
    const char prev = cookie[pos - 1];
    if (prev != ' ' && prev != ';') {
      return false;
    }
  }
  const int end = pos + needle.length();
  if (end < static_cast<int>(cookie.length())) {
    const char next = cookie[end];
    if (next != ';' && next != ' ') {
      return false;
    }
  }
  return true;
}

bool WebPortal::requireSession(bool htmlLogin) {
  if (sessionCookieOk()) {
    return true;
  }
  if (htmlLogin && server_.method() == HTTP_GET) {
    sendLoginPage("");
    return false;
  }
  sendNoCache();
  server_.send(401, "text/plain", "Unauthorized");
  return false;
}

void WebPortal::sendLoginPage(const char* err) {
  sendNoCache();
  String body;
  body += F("<h1>NTP 设置登录</h1>"
            "<div class='card'>"
            "<p>输入配置口令后才能进入设置。口令默认与 SoftAP 相同"
            "（串口 <code>SoftAP default pass=</code> 或 OLED）。"
            "登录后设置页不再要求填写写口令。</p>");
  if (err && err[0]) {
    body += F("<p class='bad'>");
    body += err;
    body += F("</p>");
  }
  body += F("<form method='POST' action='/login'>"
            "<label>配置口令</label>"
            "<input name='password' type='password' autocomplete='current-password' autofocus>"
            "<button type='submit'>进入设置</button>"
            "</form></div>"
            "<p><a href='/'>返回状态</a></p>");
  server_.send(200, "text/html", buildPage("NTP 登录", body));
}

void WebPortal::handleLogin() {
  const String pass = server_.arg("password");
  const String expect = writePassword();
  if (!pass.isEmpty() && !expect.isEmpty() && pass == expect) {
    issueSession();
    sendNoCache();
    server_.sendHeader("Location", "/cfg", true);
    server_.send(302, "text/plain", "");
    return;
  }
  sendLoginPage("口令错误");
}

void WebPortal::handleLogout() {
  sessionToken_ = "";
  sessionUntilMs_ = 0;
  sendNoCache();
  server_.sendHeader("Set-Cookie", "ntp_sess=; Path=/; Max-Age=0");
  server_.sendHeader("Location", "/login", true);
  server_.send(302, "text/plain", "");
}

void WebPortal::handleSetupEntry() {
  sendNoCache();
  if (sessionCookieOk()) {
    server_.sendHeader("Location", "/cfg", true);
    server_.send(302, "text/plain", "");
    return;
  }
  sendLoginPage("");
}

String WebPortal::buildPage(const String& title, const String& body, bool refresh) const {
  String html;
  html.reserve(body.length() + 1400);
  html += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<meta http-equiv='Cache-Control' content='no-store'>");
  if (refresh) {
    html += F("<meta http-equiv='refresh' content='2'>");
  }
  html += F("<title>");
  html += title;
  html += F("</title><style>"
            "body{font-family:system-ui,-apple-system,'Segoe UI',sans-serif;max-width:480px;"
            "margin:0 auto;padding:16px 12px 28px;background:#0f172a;color:#e2e8f0}"
            "h1{font-size:1.25rem}button,input,select{font-size:1rem;padding:8px;margin:6px 0;"
            "width:100%;box-sizing:border-box}"
            "button{background:#38bdf8;border:0;border-radius:8px;color:#0f172a;font-weight:700}"
            "a{color:#38bdf8}.card{background:#1e293b;padding:16px;border-radius:12px;margin:12px 0}"
            ".row{display:flex;justify-content:space-between;gap:12px;padding:6px 0;"
            "border-bottom:1px solid #334155}"
            ".row:last-child{border-bottom:0}.k{color:#94a3b8}.v{font-weight:700;text-align:right}"
            ".ok{color:#4ade80}.warn{color:#fbbf24}.bad{color:#f87171}"
            ".obar{display:none;height:10px;background:#334155;border-radius:6px;overflow:hidden;margin:8px 0}"
            ".ofill{height:100%;width:0;background:#38bdf8;transition:width .15s linear}"
            "button:disabled{opacity:.55}"
            ".hero{text-align:center;padding:12px 4px 8px}"
            ".brand{font-size:1.85rem;font-weight:800;letter-spacing:.06em;margin:0;line-height:1.15}"
            ".badge{display:inline-block;margin:14px 0 6px;padding:7px 16px;border-radius:8px;"
            "font-weight:700;background:#334155;color:#e2e8f0}"
            ".badge.ok{background:#14532d;color:#86efac}"
            ".badge.warn{background:#713f12;color:#fde68a}"
            ".badge.bad{background:#7f1d1d;color:#fca5a5}"
            ".timeLocal{font-size:2.85rem;font-weight:800;font-variant-numeric:tabular-nums;"
            "letter-spacing:.03em;margin:10px 0 4px;line-height:1.05}"
            ".timeUtc{color:#94a3b8;font-size:.92rem;font-variant-numeric:tabular-nums}"
            ".ident{color:#cbd5e1;margin:16px 0 10px;font-size:.95rem;line-height:1.45;"
            "word-break:break-word}"
            "a.btn{display:inline-block;background:#38bdf8;color:#0f172a;text-decoration:none;"
            "padding:10px 28px;border-radius:8px;font-weight:700}"
            ".adv{background:#1e293b;border-radius:12px;margin:18px 0 8px;padding:4px 14px 10px}"
            ".adv summary{cursor:pointer;color:#94a3b8;padding:10px 0;font-weight:600;"
            "list-style-position:outside}"
            ".adv[open] summary{color:#e2e8f0;border-bottom:1px solid #334155;margin-bottom:4px}"
            ".foot{color:#64748b;font-size:.82rem;text-align:center;margin:18px 0 8px;line-height:1.5}"
            "</style></head><body>");
  html += body;
  html += F("</body></html>");
  return html;
}

void WebPortal::handleRoot() {
  // `/` is always the read-only status page (SoftAP and STA). Settings live at /cfg
  // after login — never serve that HTML here (also busts old cached SoftAP `/` setup).
  sendNoCache();

  // Product first viewport: brand + badge + large local time + identity + CTA.
  // Engineering KV rows live under <details>. Seconds painted locally; /status calibrates.
  String body = F(
      "<div class='hero'>"
      "<div class='brand'>GNSS NTP</div>"
      "<div class='badge' id='badge'>检测中…</div>"
      "<div class='timeLocal' id='localHero'>--:--:--</div>"
      "<div class='timeUtc' id='utcHero'>UTC --</div>"
      "<div class='ident' id='ident'>--</div>"
      "<p><a class='btn' href='/cfg'>设置</a></p>"
      "</div>"
      "<details class='adv'>"
      "<summary>工程细节</summary>"
      "<div class='row'><span class='k'>NTP</span><span class='v' id='ntpState'>--</span></div>"
      "<div class='row'><span class='k'>时钟</span><span class='v' id='clk'>--</span></div>"
      "<div class='row'><span class='k'>Residual</span><span class='v' id='res'>--</span></div>"
      "<div class='row'><span class='k'>频偏</span><span class='v' id='freq'>--</span></div>"
      "<div class='row'><span class='k'>Root 质量</span><span class='v' id='qual'>--</span></div>"
      "<div class='row'><span class='k'>策略</span><span class='v' id='apol'>--</span></div>"
      "<div class='row'><span class='k'>守时上限</span><span class='v' id='hold'>--</span></div>"
      "<div class='row'><span class='k'>守时中</span><span class='v' id='holdms'>--</span></div>"
      "<div class='row'><span class='k'>Stratum</span><span class='v' id='stratum'>--</span></div>"
      "<div class='row'><span class='k'>Leap</span><span class='v' id='li'>--</span></div>"
      "<div class='row'><span class='k'>RefID</span><span class='v' id='refId'>GPSS</span></div>"
      "<div class='row'><span class='k'>查询次数</span><span class='v' id='ntpReq'>--</span></div>"
      "<div class='row'><span class='k'>本地完整</span><span class='v' id='localFull'>--</span></div>"
      "<div class='row'><span class='k'>UTC 完整</span><span class='v' id='utcFull'>--</span></div>"
      "<div class='row'><span class='k'>GPS 锁定</span><span class='v' id='fix'>--</span></div>"
      "<div class='row'><span class='k'>搜星</span><span class='v' id='sats'>--</span></div>"
      "<div class='row'><span class='k'>HDOP</span><span class='v' id='hdop'>--</span></div>"
      "<div class='row'><span class='k'>PPS</span><span class='v' id='pps'>--</span></div>"
      "<div class='row'><span class='k'>NMEA 龄</span><span class='v' id='age'>--</span></div>"
      "<div class='row'><span class='k'>时间有效</span><span class='v' id='tvalid'>--</span></div>"
      "<div class='row'><span class='k'>经纬度</span><span class='v' id='ll'>--</span></div>"
      "<div class='row'><span class='k'>已服务</span><span class='v' id='served'>--</span></div>"
      "<div class='row'><span class='k'>限流 RATE</span><span class='v' id='rate'>--</span></div>"
      "<div class='row'><span class='k'>DENY</span><span class='v' id='deny'>--</span></div>"
      "<div class='row'><span class='k'>静默丢弃</span><span class='v' id='drop'>--</span></div>"
      "<div class='row'><span class='k'>ACL 拒绝</span><span class='v' id='adny'>--</span></div>"
      "<div class='row'><span class='k'>活跃客户端</span><span class='v' id='clients'>--</span></div>"
      "<div class='row'><span class='k'>ACL 模式</span><span class='v' id='aclm'>--</span></div>"
      "<div class='row'><span class='k'>IP</span><span class='v' id='ip'>--</span></div>"
      "<div class='row'><span class='k'>SSID</span><span class='v' id='ssid'>--</span></div>"
      "<div class='row'><span class='k'>RSSI</span><span class='v' id='rssi'>--</span></div>"
      "<div class='row'><span class='k'>MAC</span><span class='v' id='mac'>--</span></div>"
      "<div class='row'><span class='k'>运行</span><span class='v' id='up'>--</span></div>"
      "<div class='row'><span class='k'>固件</span><span class='v' id='fw'>--</span></div>"
      "<div class='row'><span class='k'>内存</span><span class='v' id='heap'>--</span></div>"
      "<div class='row'><span class='k'>温度</span><span class='v' id='temp'>--</span></div>"
      "</details>"
      "<p class='foot'>时间本地外推 · 状态约 2 s 校准（UI 瘦包）<br>"
      "JSON <a href='/status?view=ui'>/status?view=ui</a> · "
      "<a href='/status'>/status</a> · 指标 <a href='/metrics'>/metrics</a></p>"
      "<script>"
      "function pad(n){return n<10?'0'+n:''+n}"
      "function fmtFull(epoch,tz){"
      " if(!epoch)return '--';"
      " const d=new Date((epoch+tz*3600)*1000);"
      " return d.getUTCFullYear()+'-'+pad(d.getUTCMonth()+1)+'-'+pad(d.getUTCDate())+' '"
      "  +pad(d.getUTCHours())+':'+pad(d.getUTCMinutes())+':'+pad(d.getUTCSeconds())}"
      "function fmtHms(epoch,tz){"
      " if(!epoch)return '--:--:--';"
      " const d=new Date((epoch+tz*3600)*1000);"
      " return pad(d.getUTCHours())+':'+pad(d.getUTCMinutes())+':'+pad(d.getUTCSeconds())}"
      "function fmtUp(s){"
      " if(s==null)return '--';"
      " const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60),x=s%60;"
      " return (d?d+'d ':'')+pad(h)+':'+pad(m)+':'+pad(x)}"
      "function setCls(el,c){el.className='v '+(c||'')}"
      "function setBadge(cls,text){"
      " const el=document.getElementById('badge');"
      " el.className='badge '+(cls||''); el.textContent=text}"
      "function applyBadge(s1,have){"
      " if(s1)setBadge('ok','S1');"
      " else if(have)setBadge('warn','HLD');"
      " else setBadge('bad','WAIT')}"
      "let tickBusy=false, tickStarted=0, tickTimer=0, baseEpoch=0, baseMono=0, tzHours=0;"
      "function paintedEpoch(){"
      " if(!baseEpoch)return 0;"
      " return baseEpoch+Math.floor((Date.now()-baseMono)/1000)}"
      "function applyUtc(ep,fracMs){"
      " if(!ep)return;"
      " const fm=(fracMs!=null&&isFinite(fracMs))?Math.max(0,Math.min(999,fracMs|0)):0;"
      " const painted=paintedEpoch();"
      " if(baseEpoch && ep < painted-1)return;"
      " baseEpoch=ep; baseMono=Date.now()-fm;"
      "}"
      "function paintTime(){"
      " if(!baseEpoch)return;"
      " const ep=paintedEpoch();"
      " document.getElementById('localHero').textContent=fmtHms(ep,tzHours);"
      " document.getElementById('utcHero').textContent='UTC '+fmtHms(ep,0);"
      " const lf=document.getElementById('localFull');"
      " const uf=document.getElementById('utcFull');"
      " if(lf)lf.textContent=fmtFull(ep,tzHours)+' (UTC'+(tzHours>=0?'+':'')+tzHours+')';"
      " if(uf)uf.textContent=fmtFull(ep,0);"
      "}"
      "function scheduleTick(afterMs){"
      " if(tickTimer)clearTimeout(tickTimer);"
      " tickTimer=setTimeout(tick,Math.max(400,afterMs|0))"
      "}"
      "function statusUrl(){"
      " const d=document.querySelector('details.adv');"
      " return (d&&d.open)?'/status':'/status?view=ui'}"
      "function applyHero(j){"
      " const g=j.gps||{}, n=j.ntp||{};"
      " const have=!!n.synced, s1=!!n.stratum1Ready;"
      " applyBadge(s1,have);"
      " tzHours=j.tzHours||0;"
      " applyUtc(g.utcEpoch, g.utcFracMs);"
      " paintTime();"
      " const ip=j.ip||'--', ssid=j.ssid||'--';"
      " const rssi=(j.rssi!=null)?(j.rssi+' dBm'):'--';"
      " document.getElementById('ident').textContent=ip+' · '+ssid+' · '+rssi;"
      "}"
      "function applyDetails(j){"
      " const g=j.gps||{}, n=j.ntp||{}, c=j.clock||{};"
      " const have=!!n.synced, pps=!!g.ppsFresh, s1=!!n.stratum1Ready;"
      " const st=document.getElementById('ntpState');"
      " if(!st)return;"
      " st.textContent=s1?'S1 就绪':(have?'HLD 降级/守时':'WAIT 未同步');"
      " setCls(st,s1?'ok':(have?'warn':'bad'));"
      " document.getElementById('clk').textContent=c.state||'--';"
      " document.getElementById('res').textContent=(c.residualMs!=null)?(c.residualMs+' ms'):'--';"
      " document.getElementById('freq').textContent=(c.freqPpm!=null)"
      "  ?(Number(c.freqPpm).toFixed(2)+' ppm'):'--';"
      " document.getElementById('qual').textContent=(g.qualityMs!=null)?(g.qualityMs+' ms'):'--';"
      " document.getElementById('apol').textContent=j.anomalyLabel||'--';"
      " document.getElementById('hold').textContent=(j.holdoverSec!=null)?(j.holdoverSec+' s'):'--';"
      " const hm=document.getElementById('holdms');"
      " hm.textContent=(c.holdoverMs>0)?(Math.round(c.holdoverMs/1000)+' s'):'-';"
      " setCls(hm,c.holdoverMs>0?'warn':'');"
      " document.getElementById('stratum').textContent=(n.stratum!=null)?n.stratum:'--';"
      " const li=document.getElementById('li');"
      " li.textContent=(n.li!=null)?('LI='+n.li):'--'; setCls(li,n.li===0?'ok':'warn');"
      " document.getElementById('refId').textContent=n.refId||'GPSS';"
      " document.getElementById('ntpReq').textContent=(n.requests!=null)?n.requests:'--';"
      " const fx=document.getElementById('fix');"
      " fx.textContent=g.fix?'是':'否'; setCls(fx,g.fix?'ok':'bad');"
      " document.getElementById('sats').textContent=(g.satellites!=null)?g.satellites:'--';"
      " document.getElementById('hdop').textContent=(g.hdop!=null)?Number(g.hdop).toFixed(1):'--';"
      " const pp=document.getElementById('pps');"
      " pp.textContent=(pps?'正常':'无')+' ('+(g.ppsCount||0)+')'; setCls(pp,pps?'ok':'bad');"
      " document.getElementById('age').textContent=(g.ageMs!=null)?(g.ageMs+' ms'):'--';"
      " const tv=document.getElementById('tvalid');"
      " tv.textContent=g.timeValid?'有效':'无效'; setCls(tv,g.timeValid?'ok':'bad');"
      " document.getElementById('ll').textContent=g.fix"
      "  ?(Number(g.lat).toFixed(5)+', '+Number(g.lon).toFixed(5)):'--';"
      " document.getElementById('served').textContent=(n.served!=null)?n.served:'--';"
      " document.getElementById('rate').textContent=(n.rateLimited!=null)?n.rateLimited:'--';"
      " document.getElementById('deny').textContent=(n.denied!=null)?n.denied:'--';"
      " document.getElementById('drop').textContent=(n.dropped!=null)?n.dropped:'--';"
      " document.getElementById('adny').textContent=(n.aclDenied!=null)?n.aclDenied:'--';"
      " document.getElementById('clients').textContent=(n.clients!=null)?n.clients:'--';"
      " document.getElementById('aclm').textContent=(j.ntpAclLabel||'--')"
      "  +' ('+((j.ntpAcl&&j.ntpAcl.length)||0)+')';"
      " document.getElementById('ip').textContent=j.ip||'--';"
      " document.getElementById('ssid').textContent=j.ssid||'--';"
      " document.getElementById('rssi').textContent=(j.rssi!=null)?(j.rssi+' dBm'):'--';"
      " document.getElementById('mac').textContent=j.mac||'--';"
      " document.getElementById('up').textContent=fmtUp(j.uptimeSec);"
      " document.getElementById('fw').textContent=(j.fwMark||j.fwVersion||'--')"
      "  +' · '+(j.otaRunning||'?')+'/'+(j.otaState||'?');"
      " const hb=Math.round((j.freeHeap||0)/1024), hmn=Math.round((j.minFreeHeap||0)/1024);"
      " const he=document.getElementById('heap');"
      " he.textContent=hb+' KB (min '+hmn+')'; setCls(he,hb<20480?'bad':'');"
      " const tp=document.getElementById('temp');"
      " tp.textContent=(c.tempC!=null)?(Number(c.tempC).toFixed(1)+'°C · '"
      "  +(c.tempComp?('补偿 '+Number(c.tempCorrPpm||0).toFixed(2)+' ppm'):'补偿关')):'--';"
      "}"
      "async function tick(){"
      " if(tickBusy){"
      "  if(Date.now()-tickStarted < 3000){scheduleTick(400);return;}"
      " }"
      " tickBusy=true; tickStarted=Date.now();"
      " const t0=Date.now();"
      " try{"
      "  const r=await fetch(statusUrl()); const j=await r.json();"
      "  applyHero(j);"
      "  if(j.view!=='ui')applyDetails(j);"
      " }catch(e){}"
      " tickBusy=false;"
      " scheduleTick(2000-(Date.now()-t0));"
      "}"
      "const adv=document.querySelector('details.adv');"
      " if(adv){adv.addEventListener('toggle',function(){if(adv.open)tick();});}"
      "tick(); setInterval(paintTime,250);"
      "</script>");

  server_.send(200, "text/html", buildPage("GNSS NTP", body, false));
}

void WebPortal::handleSetup() {
  if (!requireSession(true)) {
    return;
  }
  uint8_t apol = 0;
  uint8_t aclm = 0;
  bool tcmp = false;
  int16_t tcpc = CLK_TEMP_COEFF_CENTI;
  uint32_t ooffMin = OLED_IDLE_OFF_DEFAULT_MS / 60000;
  String aclLines;
  String savedSsid;
  bool haveSaved = false;
  AppSettings s;
  if (settingsCopy(pdMS_TO_TICKS(50), &s)) {
    apol = static_cast<uint8_t>(s.anomalyPolicy);
    aclm = static_cast<uint8_t>(s.ntpAclMode);
    tcmp = s.tempComp;
    tcpc = s.tempCoeffCenti;
    ooffMin = s.oledIdleOffMs / 60000;
    for (uint8_t i = 0; i < s.ntpAclCount && i < NTP_ACL_MAX_ENTRIES; ++i) {
      if (i) {
        aclLines += '\n';
      }
      aclLines += s.ntpAcl[i].toString();
    }
    savedSsid = s.wifiSsid;
    haveSaved = !savedSsid.isEmpty();
  }
  String body;
  body.reserve(7200);
  body += F("<!-- ntp-cfg-v2 -->"
            "<h1>NTP 设置</h1><p><a href='/'>返回状态</a> · <a href='/logout'>退出</a></p>");
  if (haveSaved) {
    body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>已保存的 WiFi</h2><p>SSID: <b>");
    body += savedSsid;
    body += F("</b></p>"
              "<p style='color:#64748b;font-size:.85rem'>固件更新后会自动重连；无需重新输入 WiFi 密码。"
              "仅当路由器改密或换热点时才需要下方重新配网。</p>"
              "<button type='button' onclick='reconnectSaved()'>使用已保存网络重连</button>"
              "<p id='rmsg'></p></div>");
  }
  body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>GPS 异常策略</h2>"
            "<select id='apol'>"
            "<option value='0'>立即拒绝授时 (Refuse)</option>"
            "<option value='1'>短时守时 Holdover 30s</option>"
            "<option value='2'>长时守时 Holdover 5min</option>"
            "</select>"
            "<button type='button' onclick='savePolicy()'>保存策略</button>"
            "<p id='pmsg'></p></div>");
  body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>NTP ACL 白名单</h2>"
            "<p style='color:#64748b;font-size:.85rem'>默认 Off。开启 AllowList 后仅列出的 IPv4 可取时"
            "（最多 8 条；未命中静默丢弃；空列表=拒绝全部）。</p>"
            "<label>模式</label><select id='aclm'>"
            "<option value='0'>Off（不限制）</option>"
            "<option value='1'>AllowList</option>"
            "</select>"
            "<label>允许的 IP（每行一个）</label>"
            "<textarea id='acllist' rows='5' style='width:100%;font-family:monospace'></textarea>"
            "<button type='button' onclick='saveAcl()'>保存 ACL</button>"
            "<p id='amsg'></p></div>");
  body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>晶振温度补偿</h2>"
            "<p style='color:#64748b;font-size:.85rem'>默认关。用片上温度对 Holdover 外推做一阶 "
            "ppm/°C 修正（相对最近 PPS 估频时的温度）。系数可改，默认 -0.50。</p>"
            "<label>模式</label><select id='tcmp'>"
            "<option value='0'>Off</option>"
            "<option value='1'>On</option>"
            "</select>"
            "<label>系数 ppm/°C</label>"
            "<input id='tcpc' type='number' step='0.01'>"
            "<button type='button' onclick='saveTemp()'>保存温度补偿</button>"
            "<p id='tmsg'></p></div>");
  body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>屏幕息屏</h2>"
            "<p style='color:#64748b;font-size:.85rem'>无旋钮操作达到该时长后关闭 OLED 面板（防烧屏）。"
            "旋转/单击旋钮唤醒，首个动作仅唤醒不导航；系统提示（OK IP 等）会自动亮屏。0 = 常亮。</p>"
            "<label>息屏时间</label><select id='ooff'>"
            "<option value='0'>常亮（不息屏）</option>"
            "<option value='1'>1 分钟</option>"
            "<option value='5'>5 分钟</option>"
            "<option value='10'>10 分钟（默认）</option>"
            "<option value='30'>30 分钟</option>"
            "</select>"
            "<button type='button' onclick='saveScreen()'>保存息屏</button>"
            "<p id='smsg'></p></div>");
  body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>WiFi 配网</h2>"
            "<p>扫描热点，选择 SSID，输入密码后连接。</p>"
            "<button type='button' onclick='scan()'>扫描 WiFi</button>"
            "<label>SSID</label><select id='ssid'></select>"
            "<label>Password</label><input id='pass' type='password'>"
            "<button type='button' onclick='saveWifi()'>连接</button>"
            "<p id='msg'></p></div>");
  body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>固件 OTA</h2>"
            "<p style='color:#64748b;font-size:.85rem'>上传 PlatformIO 产出的 "
            "<code>firmware.bin</code>（仅 app，勿用 merged 整片镜像）。"
            "升级期间<strong>停止 NTP 授时</strong>（KoD <code>RSTR</code>），并让出 CPU/Flash 以尽快完成；"
            "成功后自动重启，NVS 配置保留。状态灯："
            "上传中<strong>琥珀快闪</strong> → 成功<strong>绿灯常亮</strong> → 失败<strong>红闪</strong>。"
            "串口烧录仍可用作兜底。</p>"
            "<p>当前 <b>");
  body += FW_VERSION;
  body += F("</b> · 目标 <code>");
  body += gOta.expectedChipName();
  body += F("</code> · 运行分区 <code>");
  body += gOta.runningLabel();
  body += F("</code> · 下一写入 <code>");
  body += gOta.nextLabel();
  body += F("</code> · 槽位 ");
  body += String(gOta.nextSlotSize());
  body += F(" B · 状态 <code>");
  body += gOta.imageStateLabel();
  body += F("</code></p>"
            "<input id='fw' type='file' accept='.bin,application/octet-stream'>"
            "<button type='button' id='obtn' onclick='doOta()'>上传并升级</button>"
            "<div id='obar' class='obar'><div id='ofill' class='ofill'></div></div>"
            "<p id='omsg'></p></div>");
  body += F("<script>"
            "async function sleep(ms){return new Promise(r=>setTimeout(r,ms));}"
            "async function postSave(obj){"
            " const r=await fetch('/save',{method:'POST',credentials:'same-origin',"
            "  headers:{'Content-Type':'application/json'},body:JSON.stringify(obj)});"
            " if(r.status===401){location.href='/login';return '';}"
            " return r.text();}"
            "function fmtBytes(n){"
            " if(n<1024)return n+' B';"
            " if(n<(1024*1024))return (n/1024).toFixed(1)+' KB';"
            " return (n/(1024*1024)).toFixed(2)+' MB';}"
            "function setOtaUi(pct,msg,cls){"
            " const bar=document.getElementById('obar');"
            " const fill=document.getElementById('ofill');"
            " const omsg=document.getElementById('omsg');"
            " if(bar){bar.style.display='block';}"
            " if(fill){fill.style.width=Math.max(0,Math.min(100,pct|0))+'%';}"
            " if(omsg){omsg.className=cls||'';omsg.textContent=msg||'';}"
            "}"
            "function checkFwHeader(f){"
            " return new Promise(function(resolve){"
            "  if(!f||f.size<14){resolve('文件过小，请重新下载 firmware.bin');return;}"
            "  const maxSlot=");
  body += String(gOta.nextSlotSize());
  body += F(";"
            "  if(maxSlot>0&&f.size>maxSlot){resolve('文件超过 OTA 槽位 '+maxSlot+' B');return;}"
            "  if(f.size<200*1024){resolve('文件过小（可能下载不完整）');return;}"
            "  const r=new FileReader();"
            "  r.onload=function(){"
            "   const u=new Uint8Array(r.result);"
            "   if(u[0]!==0xE9){resolve('不是 ESP app 镜像（magic≠E9）；勿用 merged 整片包');return;}"
            "   if(u[1]<1||u[1]>16){resolve('镜像头异常，请重新下载');return;}"
            "   resolve('');"
            "  };"
            "  r.onerror=function(){resolve('无法读取文件头');};"
            "  r.readAsArrayBuffer(f.slice(0,16));"
            " });}"
            "async function doOta(){"
            " const btn=document.getElementById('obtn');"
            " const f=document.getElementById('fw').files[0];"
            " if(!f){setOtaUi(0,'请先选择 .bin','bad');return;}"
            " if(!f.name.toLowerCase().endsWith('.bin')){"
            "  setOtaUi(0,'仅接受 .bin 固件','bad');return;}"
            " const herr=await checkFwHeader(f);"
            " if(herr){setOtaUi(0,herr,'bad');return;}"
            " const fd=new FormData(); fd.append('firmware',f,f.name);"
            " const xhr=new XMLHttpRequest();"
            " xhr.open('POST','/ota'); xhr.withCredentials=true; xhr.timeout=600000;"
            " if(btn){btn.disabled=true;}"
            " setOtaUi(0,'准备上传 '+f.name+' ('+fmtBytes(f.size)+')...','warn');"
            " xhr.upload.onprogress=function(e){"
            "  if(e.lengthComputable&&e.total>0){"
            "   const pct=Math.floor(100*e.loaded/e.total);"
            "   setOtaUi(pct,'上传中 '+pct+'% · '+fmtBytes(e.loaded)+' / '+fmtBytes(e.total),'warn');"
            "  }else{setOtaUi(0,'上传中 '+fmtBytes(e.loaded)+' ...','warn');}"
            " };"
            " xhr.upload.onload=function(){"
            "  setOtaUi(100,'上传完成，设备正在写入 Flash 并校验...','warn');"
            " };"
            " xhr.onload=function(){"
            "  if(btn){btn.disabled=false;}"
            "  if(xhr.status===401){location.href='/login';return;}"
            "  if(xhr.status>=200&&xhr.status<300){"
            "   setOtaUi(100,'✓ 升级成功：'+xhr.responseText+' · 即将重启，约 10 秒后自动刷新','ok');"
            "   setTimeout(function(){location.href='/';},10000);"
            "   return;}"
            "  setOtaUi(0,'失败 ('+xhr.status+'): '+xhr.responseText,'bad');"
            " };"
            " xhr.onerror=function(){"
            "  /* 成功后重启常会掐断 TCP：按「可能已成功」提示 */"
            "  setOtaUi(100,'连接已断开（多为升级成功后重启）。请等约 15 秒后刷新，OLED 确认新版本。','warn');"
            "  if(btn){btn.disabled=false;}"
            "  setTimeout(function(){location.href='/';},15000);"
            " };"
            " xhr.ontimeout=function(){"
            "  setOtaUi(0,'上传超时','bad'); if(btn){btn.disabled=false;}"
            " };"
            " xhr.send(fd);"
            "}"
            "async function scan(){"
            " try{"
            " document.getElementById('msg').textContent='Scanning...';"
            " let j=null;"
            " for(let i=0;i<50;i++){"
            "  const r=await fetch('/scan',{credentials:'same-origin'});"
            "  if(r.status===401){location.href='/login';return;}"
            "  if(r.status===202){await sleep(250);continue;}"
            "  if(!r.ok){document.getElementById('msg').textContent='Scan failed';return;}"
            "  j=await r.json(); break;"
            " }"
            " if(!j){document.getElementById('msg').textContent='Scan timeout';return;}"
            " const s=document.getElementById('ssid'); s.innerHTML='';"
            " j.forEach(n=>{const o=document.createElement('option');"
            " o.value=n.ssid; o.textContent=n.ssid+' ('+n.rssi+'dBm)'; s.appendChild(o);});"
            " document.getElementById('msg').textContent='Found '+j.length+' networks';"
            " }catch(e){document.getElementById('msg').textContent=String(e);}"
            "}"
            "async function saveWifi(){"
            " try{"
            " const ssid=document.getElementById('ssid').value;"
            " const pass=document.getElementById('pass').value;"
            " document.getElementById('msg').textContent=await postSave({ssid,pass});"
            " }catch(e){document.getElementById('msg').textContent=String(e);}"
            "}"
            "async function reconnectSaved(){"
            " try{"
            "  document.getElementById('rmsg').textContent=await postSave({reconnectSaved:true});"
            " }catch(e){document.getElementById('rmsg').textContent=String(e);}"
            "}"
            "async function savePolicy(){"
            " try{"
            " const anomalyPolicy=parseInt(document.getElementById('apol').value,10);"
            " document.getElementById('pmsg').textContent=await postSave({anomalyPolicy});"
            " }catch(e){document.getElementById('pmsg').textContent=String(e);}"
            "}"
            "async function saveAcl(){"
            " try{"
            " const ntpAclMode=parseInt(document.getElementById('aclm').value,10);"
            " const ntpAcl=document.getElementById('acllist').value.split(/\\r?\\n/)"
            "  .map(s=>s.trim()).filter(s=>s.length>0);"
            " document.getElementById('amsg').textContent=await postSave({ntpAclMode,ntpAcl});"
            " }catch(e){document.getElementById('amsg').textContent=String(e);}"
            "}"
            "async function saveTemp(){"
            " try{"
            " const tempComp=parseInt(document.getElementById('tcmp').value,10)===1;"
            " const tempCoeff=parseFloat(document.getElementById('tcpc').value);"
            " document.getElementById('tmsg').textContent=await postSave({tempComp,tempCoeff});"
            " }catch(e){document.getElementById('tmsg').textContent=String(e);}"
            "}"
            "async function saveScreen(){"
            " try{"
            " const oledIdleMin=parseInt(document.getElementById('ooff').value,10);"
            " document.getElementById('smsg').textContent=await postSave({oledIdleMin});"
            " }catch(e){document.getElementById('smsg').textContent=String(e);}"
            "}"
            "document.getElementById('apol').value='");
  body += String(apol);
  body += F("';"
            "document.getElementById('aclm').value='");
  body += String(aclm);
  body += F("';"
            "document.getElementById('tcmp').value='");
  body += String(tcmp ? 1 : 0);
  body += F("';"
            "document.getElementById('tcpc').value='");
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(tempCoeffPpmPerC(tcpc)));
    body += buf;
  }
  body += F("';"
            "document.getElementById('ooff').value='");
  body += String(static_cast<unsigned>(ooffMin));
  body += F("';"
            "document.getElementById('acllist').value=");
  // JSON-encode the ACL lines for safe JS string.
  {
    JsonDocument tmp;
    tmp.set(aclLines);
    String enc;
    serializeJson(tmp, enc);
    body += enc;
  }
  body += F(";");
  if (!haveSaved) {
    body += F("scan();");
  }
  body += F("</script>");
  sendNoCache();
  server_.send(200, "text/html", buildPage("NTP 设置", body));
}

void WebPortal::handleScan() {
  if (!requireSession(false)) {
    return;
  }
  if (wifi_ == nullptr) {
    server_.send(503, "application/json", "{\"error\":\"no wifi\"}");
    return;
  }

  // Do not kick STA join or scanDelete() an in-flight OLED harvest.
  if (wifi_->isConnecting() && !wifi_->isStaConnected()) {
    server_.send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  const bool cacheFresh =
      wifi_->scanState() == WifiScanState::Done && !wifi_->lastScan().empty() &&
      static_cast<int32_t>(millis() - wifi_->lastHarvestMs()) < WIFI_SCAN_CACHE_MS;
  if (!wifi_->isScanRunning() && !wifi_->peekScanDone() && !cacheFresh) {
    if (!wifi_->startScan()) {
      server_.send(503, "application/json", "{\"status\":\"busy\"}");
      return;
    }
  }

  std::vector<WifiNetwork> nets;
  const WifiScanState st = wifi_->pollScan(&nets);
  if (st == WifiScanState::Running) {
    server_.send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  if (st == WifiScanState::Failed) {
    server_.send(500, "application/json", "{\"status\":\"failed\"}");
    return;
  }

  const std::vector<WifiNetwork>& src = nets.empty() ? wifi_->lastScan() : nets;

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& n : src) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = jsonSafeSsid(n.ssid);
    o["rssi"] = n.rssi;
  }
  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void WebPortal::handleSave() {
  if (!requireSession(false)) {
    return;
  }
  String body = server_.arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    server_.send(400, "text/plain", "Bad JSON");
    return;
  }

  // Optional password overrides (still require auth above).
  bool touchMgmt = false;
  if (doc["apPassword"].is<const char*>() || doc["webPassword"].is<const char*>()) {
    AppSettings s;
    if (!settingsCopy(pdMS_TO_TICKS(100), &s)) {
      server_.send(503, "text/plain", "Settings busy");
      return;
    }
    if (doc["apPassword"].is<const char*>()) {
      String ap = doc["apPassword"].as<const char*>();
      if (ap == "null") {
        ap = "";
      }
      if (!ap.isEmpty() && ap.length() < 8) {
        server_.send(400, "text/plain", "apPassword must be >=8 chars");
        return;
      }
      s.apPassword = ap;
      touchMgmt = true;
    }
    if (doc["webPassword"].is<const char*>()) {
      String wp = doc["webPassword"].as<const char*>();
      if (wp == "null") {
        wp = "";
      }
      s.webPassword = wp;
      touchMgmt = true;
    }
    if (touchMgmt && !settingsCommit(pdMS_TO_TICKS(100), s)) {
      server_.send(503, "text/plain", "Settings busy");
      return;
    }
  }

  // Reuse NVS WiFi without retyping password (SoftAP escape / post-OTA).
  if (doc["reconnectSaved"] == true) {
    String ssid;
    String pass;
    AppSettings s;
    if (settingsCopy(pdMS_TO_TICKS(200), &s)) {
      ssid = s.wifiSsid;
      pass = s.wifiPass;
    }
    if (ssid.isEmpty()) {
      server_.send(400, "text/plain", "No saved WiFi");
      return;
    }
    pendingSsid_ = ssid;
    pendingPass_ = pass;
    pendingConnect_ = true;
    server_.send(200, "text/plain", "Reconnecting with saved WiFi...");
    return;
  }

  bool savedPolicy = false;
  if (!doc["anomalyPolicy"].isNull()) {
    const int v = doc["anomalyPolicy"].as<int>();
    if (v >= 0 && v <= static_cast<int>(AnomalyPolicy::HoldoverLong)) {
      AppSettings s;
      if (settingsCopy(pdMS_TO_TICKS(200), &s)) {
        s.anomalyPolicy = static_cast<AnomalyPolicy>(v);
        const uint16_t defHold = anomalyPolicyDefaultHoldoverSec(s.anomalyPolicy);
        if (defHold > 0) {
          s.holdoverSec = defHold;
        }
        if (!doc["holdoverSec"].isNull()) {
          uint16_t hs = doc["holdoverSec"].as<uint16_t>();
          if (hs < 10) hs = 10;
          if (hs > 600) hs = 600;
          s.holdoverSec = hs;
        }
        if (settingsCommit(pdMS_TO_TICKS(200), s)) {
          savedPolicy = true;
        }
      }
    }
  }

  bool savedAcl = false;
  const bool haveAclMode = !doc["ntpAclMode"].isNull();
  const bool haveAclList = doc["ntpAcl"].is<JsonArray>();
  if (haveAclMode || haveAclList) {
    AppSettings s;
    if (settingsCopy(pdMS_TO_TICKS(200), &s)) {
      if (haveAclMode) {
        const int m = doc["ntpAclMode"].as<int>();
        s.ntpAclMode =
            (m == static_cast<int>(NtpAclMode::AllowList)) ? NtpAclMode::AllowList : NtpAclMode::Off;
      }
      if (haveAclList) {
        JsonArray arr = doc["ntpAcl"].as<JsonArray>();
        uint8_t n = 0;
        for (JsonVariant v : arr) {
          if (n >= NTP_ACL_MAX_ENTRIES) {
            break;
          }
          if (!v.is<const char*>()) {
            continue;
          }
          IPAddress ip;
          if (ip.fromString(v.as<const char*>()) && static_cast<uint32_t>(ip) != 0) {
            s.ntpAcl[n++] = ip;
          }
        }
        s.ntpAclCount = n;
      }
      if (settingsCommit(pdMS_TO_TICKS(200), s)) {
        savedAcl = true;
      }
    }
  }

  bool savedTemp = false;
  const bool haveTempComp = !doc["tempComp"].isNull();
  const bool haveTempCoeff = !doc["tempCoeff"].isNull();
  if (haveTempComp || haveTempCoeff) {
    AppSettings s;
    if (settingsCopy(pdMS_TO_TICKS(200), &s)) {
      if (haveTempComp) {
        s.tempComp = doc["tempComp"].as<bool>();
      }
      if (haveTempCoeff) {
        float k = doc["tempCoeff"].as<float>();
        if (k < -5.0f) {
          k = -5.0f;
        }
        if (k > 5.0f) {
          k = 5.0f;
        }
        s.tempCoeffCenti = static_cast<int16_t>(lroundf(k * 100.0f));
      }
      if (settingsCommit(pdMS_TO_TICKS(200), s)) {
        savedTemp = true;
      }
    }
  }

  // OLED idle blanking timeout, in whole minutes (0 = always on).
  bool savedScreen = false;
  if (!doc["oledIdleMin"].isNull()) {
    uint32_t mins = doc["oledIdleMin"].as<uint32_t>();
    if (mins > 60) {
      mins = 60;
    }
    AppSettings s;
    if (settingsCopy(pdMS_TO_TICKS(200), &s)) {
      s.oledIdleOffMs = mins * 60000UL;
      if (settingsCommit(pdMS_TO_TICKS(200), s)) {
        savedScreen = true;
      }
    }
  }

  // Only touch WiFi creds when ssid is a real JSON string (not missing/null).
  if (doc["ssid"].is<const char*>()) {
    pendingSsid_ = doc["ssid"].as<const char*>();
    pendingPass_ = doc["pass"].is<const char*>() ? String(doc["pass"].as<const char*>()) : String();
    if (pendingSsid_ == "null" || pendingSsid_ == "undefined") {
      pendingSsid_ = "";
    }
    if (!pendingSsid_.isEmpty()) {
      AppSettings s;
      if (settingsCopy(pdMS_TO_TICKS(200), &s)) {
        s.wifiSsid = pendingSsid_;
        s.wifiPass = pendingPass_;
        if (settingsCommit(pdMS_TO_TICKS(200), s)) {
          pendingConnect_ = true;
          server_.send(200, "text/plain", "Saved. Connecting...");
          return;
        }
      }
      server_.send(503, "text/plain", "Settings busy");
      return;
    }
  }

  if (savedPolicy || touchMgmt || savedAcl || savedTemp || savedScreen) {
    if (savedTemp) {
      server_.send(200, "text/plain", "Temp comp saved");
    } else if (savedScreen) {
      server_.send(200, "text/plain", "Screen timeout saved");
    } else if (savedAcl) {
      server_.send(200, "text/plain", "ACL saved");
    } else {
      server_.send(200, "text/plain", savedPolicy ? "Policy saved" : "Password updated");
    }
    return;
  }
  server_.send(400, "text/plain", "SSID or anomalyPolicy or ntpAcl required");
}

void WebPortal::handleOtaUpload() {
  HTTPUpload& upload = server_.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaAuthOk_ = sessionCookieOk();
    otaError_[0] = '\0';
    gOta.clear();

    if (!otaAuthOk_) {
      strncpy(otaError_, "Unauthorized", sizeof(otaError_) - 1);
      Serial.println("[ota] reject: no session");
      server_.client().stop();
      return;
    }
    sessionUntilMs_ = millis() + 30UL * 60UL * 1000UL;

    size_t contentLen = 0;
    if (server_.hasHeader("Content-Length")) {
      contentLen = static_cast<size_t>(strtoul(server_.header("Content-Length").c_str(), nullptr, 10));
    }
    if (contentLen == 0 && server_.clientContentLength() > 0) {
      contentLen = static_cast<size_t>(server_.clientContentLength());
    }
    if (!gOta.beginSession(contentLen, otaError_, sizeof(otaError_))) {
      otaAuthOk_ = false;
      server_.client().stop();
      return;
    }
    postUiText("OTA uploading...");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaAuthOk_ || !gOta.sessionActive()) {
      return;
    }
    if (!gOta.write(upload.buf, upload.currentSize, otaError_, sizeof(otaError_))) {
      server_.client().stop();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!otaAuthOk_) {
      return;
    }
    if (!gOta.sessionActive() && !gOta.succeeded()) {
      if (otaError_[0] == '\0') {
        strncpy(otaError_, "Upload aborted", sizeof(otaError_) - 1);
      }
      if (ipcOtaPhase() != OtaPhase::Failed) {
        gOta.fail(otaError_);
      }
      return;
    }
    if (gOta.finish(otaError_, sizeof(otaError_))) {
      Serial.printf("[ota] success %u bytes → reboot\n", static_cast<unsigned>(upload.totalSize));
      postUiText("OTA OK reboot");
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (otaError_[0] == '\0') {
      strncpy(otaError_, "Upload aborted", sizeof(otaError_) - 1);
    }
    gOta.fail(otaError_);
    Serial.println("[ota] client aborted");
  }
}

void WebPortal::handleOtaDone() {
  sendNoCache();
  server_.sendHeader("Connection", "close");
  if (!otaAuthOk_) {
    server_.send(401, "text/plain", otaError_[0] ? otaError_ : "Unauthorized");
    otaAuthOk_ = false;
    return;
  }
  if (!gOta.succeeded()) {
    server_.send(400, "text/plain", otaError_[0] ? otaError_ : "OTA failed");
    otaAuthOk_ = false;
    if (ipcOtaPhase() != OtaPhase::Failed) {
      gOta.fail(otaError_[0] ? otaError_ : "OTA failed");
    }
    return;
  }
  server_.send(200, "text/plain", "OK — rebooting into new firmware");
  Serial.println("[ota] reboot in 800ms");
  gOta.kickNetAlive();
  // Give the HTTP stack a moment to flush the success body before restart
  // (clients need the 200 + progress UI "完成" cue).
  delay(800);
  ESP.restart();
}

void WebPortal::handleStatus() {
  const bool uiView =
      server_.hasArg("view") && server_.arg("view").equalsIgnoreCase("ui");

  const WifiLinkSnapshot link = wifi_ ? wifi_->linkSnapshot() : WifiLinkSnapshot{};
  const GpsStatus st = gps_ ? gps_->snapshot() : GpsStatus{};
  const bool otaBusy = ipcOtaBusy();
  const bool syncOk = !otaBusy && st.timeValid &&
                      (st.clockState == ClockState::Locked || st.clockState == ClockState::Degraded ||
                       st.clockState == ClockState::Holdover);
  const bool s1 =
      !otaBusy && st.timeValid && st.clockState == ClockState::Locked && st.ppsFresh;

  uint32_t utcEpoch = st.utcEpoch;
  uint32_t utcFracMs = 0;
  {
    uint32_t liveSec = 0;
    uint32_t liveFrac = 0;
    if (gps_ && gps_->nowUtc(liveSec, liveFrac)) {
      utcEpoch = liveSec;
      utcFracMs =
          static_cast<uint32_t>((static_cast<double>(liveFrac) / 4294967296.0) * 1000.0);
    }
  }

  int tzHours = 8;
  AppSettings s;
  const bool haveSettings = settingsCopy(pdMS_TO_TICKS(20), &s);
  if (haveSettings) {
    tzHours = s.timezoneHours;
  }

  // Product UI poll: badge + clock calibrate + identity + light reference.
  if (uiView) {
    JsonDocument doc;
    doc["view"] = "ui";
    doc["fwMark"] = FW_MARK;
    doc["tzHours"] = tzHours;
    doc["ip"] = (link.staUp ? link.staIp : link.apIp).toString();
    doc["ssid"] = link.staUp ? link.staSsid : "";
    doc["rssi"] = link.staUp ? link.rssi : 0;
    doc["badge"] = s1 ? "S1" : (syncOk ? "HLD" : "WAIT");
    JsonObject gps = doc["gps"].to<JsonObject>();
    gps["utcEpoch"] = utcEpoch;
    gps["utcFracMs"] = utcFracMs;
    gps["satellites"] = st.satellites;
    gps["ppsFresh"] = st.ppsFresh;
    gps["ppsCount"] = st.ppsCount;
    JsonObject clock = doc["clock"].to<JsonObject>();
    clock["state"] = clockStateLabel(st.clockState);
    clock["freqPpm"] = st.freqPpm;
    JsonObject ntp = doc["ntp"].to<JsonObject>();
    ntp["synced"] = syncOk;
    ntp["stratum1Ready"] = s1;
    String out;
    serializeJson(doc, out);
    sendNoCache();
    server_.send(200, "application/json", out);
    return;
  }

  JsonDocument doc;
  doc["fwVersion"] = FW_VERSION;
  doc["fwMark"] = FW_MARK;
  doc["otaRunning"] = gOta.runningLabel();
  doc["otaNext"] = gOta.nextLabel();
  doc["otaNextSize"] = gOta.nextSlotSize();
  doc["otaState"] = gOta.imageStateLabel();
  doc["otaBusy"] = otaBusy;
  doc["otaPhase"] = ipcOtaPhaseLabel();
  doc["otaChip"] = gOta.expectedChipName();
  doc["ntpServing"] = !otaBusy;
  doc["sta"] = link.staUp;
  doc["ip"] = (link.staUp ? link.staIp : link.apIp).toString();
  doc["ssid"] = link.staUp ? link.staSsid : "";
  doc["rssi"] = link.staUp ? link.rssi : 0;
  doc["mac"] = wifi_ ? wifi_->macAddress() : "";
  doc["uptimeSec"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["minFreeHeap"] = ESP.getMinFreeHeap();

  AnomalyPolicy apol = AnomalyPolicy::Refuse;
  uint16_t hold = 0;
  String savedSsid;
  NtpAclMode aclMode = NtpAclMode::Off;
  uint8_t aclCount = 0;
  bool tcmp = false;
  int16_t tcpc = CLK_TEMP_COEFF_CENTI;
  IPAddress aclIps[NTP_ACL_MAX_ENTRIES];
  if (haveSettings) {
    doc["tzHours"] = s.timezoneHours;
    apol = s.anomalyPolicy;
    hold = s.holdoverSec;
    savedSsid = s.wifiSsid;
    tcmp = s.tempComp;
    tcpc = s.tempCoeffCenti;
    aclMode = s.ntpAclMode;
    aclCount = s.ntpAclCount;
    if (aclCount > NTP_ACL_MAX_ENTRIES) {
      aclCount = NTP_ACL_MAX_ENTRIES;
    }
    for (uint8_t i = 0; i < aclCount; ++i) {
      aclIps[i] = s.ntpAcl[i];
    }
  } else {
    doc["tzHours"] = tzHours;
  }
  doc["anomalyPolicy"] = static_cast<uint8_t>(apol);
  doc["anomalyLabel"] = anomalyPolicyMenuLabel(apol);
  doc["holdoverSec"] = hold;
  doc["savedSsid"] = savedSsid;
  doc["hasSavedWifi"] = !savedSsid.isEmpty();
  doc["ntpAclMode"] = static_cast<uint8_t>(aclMode);
  doc["ntpAclLabel"] = ntpAclModeMenuLabel(aclMode);
  doc["tempComp"] = tcmp;
  doc["tempCoeff"] = tempCoeffPpmPerC(tcpc);
  {
    JsonArray arr = doc["ntpAcl"].to<JsonArray>();
    for (uint8_t i = 0; i < aclCount; ++i) {
      arr.add(aclIps[i].toString());
    }
  }

  JsonObject gps = doc["gps"].to<JsonObject>();
  gps["fix"] = st.validFix;
  gps["satellites"] = st.satellites;
  gps["hdop"] = st.hdop;
  gps["lat"] = st.lat;
  gps["lon"] = st.lon;
  gps["ppsFresh"] = st.ppsFresh;
  gps["ppsCount"] = st.ppsCount;
#if GPS_PPS_RMT_EN || GPS_PPS_RMT_REG_EN
  JsonObject ppsRmt = gps["ppsRmt"].to<JsonObject>();
  ppsRmt["armed"] = st.ppsRmt.ok;
  ppsRmt["active"] = st.ppsRmt.active;
  ppsRmt["samples"] = st.ppsRmt.samples;
  ppsRmt["deltaMeanUs"] = st.ppsRmt.deltaMeanUx10 / 10.0;
  ppsRmt["deltaMinUs"] = st.ppsRmt.deltaMinUs;
  ppsRmt["deltaMaxUs"] = st.ppsRmt.deltaMaxUs;
  ppsRmt["oddPulse"] = st.ppsRmt.oddPulse;
  ppsRmt["fallbacks"] = st.ppsRmt.fallbacks;
  ppsRmt["lastWidthUs"] = st.ppsRmt.lastWidthUs;
  ppsRmt["idfOk"] = st.ppsRmt.idfOk;
  ppsRmt["idfFrames"] = st.ppsRmt.idfFrames;
  ppsRmt["idfFirstSyms"] = st.ppsRmt.idfFirstSyms;
  ppsRmt["idfLastSyms"] = st.ppsRmt.idfLastSyms;
  ppsRmt["idfLastD0Us"] = st.ppsRmt.idfLastD0Us;
  ppsRmt["idfLastD1Us"] = st.ppsRmt.idfLastD1Us;
  ppsRmt["idfEmptyFrames"] = st.ppsRmt.idfEmptyFrames;
  ppsRmt["idfDataFrames"] = st.ppsRmt.idfDataFrames;
  char rs[12];
  snprintf(rs, sizeof(rs), "0x%08x", st.ppsRmt.idfRawStatus);
  ppsRmt["idfRawStatus"] = rs;
  ppsRmt["idfStage"] = st.ppsRmt.idfStage;
  ppsRmt["idfErr"] = st.ppsRmt.idfErr;
  ppsRmt["regOk"] = st.ppsRmt.regOk;
  ppsRmt["regFrames"] = st.ppsRmt.regFrames;
  ppsRmt["regDataFrames"] = st.ppsRmt.regDataFrames;
  ppsRmt["regEmptyFrames"] = st.ppsRmt.regEmptyFrames;
  ppsRmt["regOverflows"] = st.ppsRmt.regOverflows;
  ppsRmt["regOwnerErr"] = st.ppsRmt.regOwnerErr;
  ppsRmt["regLastWidthUs"] = st.ppsRmt.regLastWidthUs;
  ppsRmt["regLastSymbols"] = st.ppsRmt.regLastSymbols;
  ppsRmt["regRxChannel"] = st.ppsRmt.regRxChannel;
  char rrs[16];
  snprintf(rrs, sizeof(rrs), "0x%08x", st.ppsRmt.regLastStatus);
  ppsRmt["regLastStatus"] = rrs;
#endif
  gps["utcEpoch"] = utcEpoch;
  gps["utcFracMs"] = utcFracMs;
  gps["ageMs"] = st.ageMs;
  gps["timeValid"] = st.timeValid;
  gps["qualityMs"] = st.qualityMs;

  JsonObject clock = doc["clock"].to<JsonObject>();
  clock["state"] = clockStateLabel(st.clockState);
  clock["stateCode"] = static_cast<uint8_t>(st.clockState);
  clock["residualMs"] = st.residualMs;
  clock["freqPpm"] = st.freqPpm;
  clock["tempC"] = st.tempC;
  clock["tempRefC"] = st.tempRefC;
  clock["tempCorrPpm"] = st.tempCorrPpm;
  clock["tempComp"] = st.tempComp;
  clock["holdoverMs"] = st.holdoverMs;
  JsonObject ext = doc["extClock"].to<JsonObject>();
  ext["enabled"] = st.extClockEnabled;
  ext["healthy"] = st.extClockHealthy;
  ext["driver"] = st.extClockDriver ? st.extClockDriver : "none";
  if (isfinite(st.extClockPpmFloor)) {
    ext["ppmFloor"] = st.extClockPpmFloor;
  }
  if (isfinite(st.extClockTempC)) {
    ext["tempC"] = st.extClockTempC;
  }

  JsonObject ntp = doc["ntp"].to<JsonObject>();
  ntp["synced"] = syncOk;
  ntp["stratum"] = syncOk ? 1 : 16;
  ntp["stratum1Ready"] = s1;
  ntp["refId"] = otaBusy ? "RSTR" : (syncOk ? "GPSS" : "INIT");
  // LI is leap-second indicator only; holdover stays LI=0 with rising dispersion.
  ntp["li"] = syncOk ? 0 : 3;
  ntp["otaRefuse"] = otaBusy;
  ntp["requests"] = ntp_ ? ntp_->requestCount() : 0;
  ntp["served"] = ntp_ ? ntp_->servedCount() : 0;
  ntp["rateLimited"] = ntp_ ? ntp_->rateLimitedCount() : 0;
  ntp["denied"] = ntp_ ? ntp_->deniedCount() : 0;
  ntp["dropped"] = ntp_ ? ntp_->droppedCount() : 0;
  ntp["aclDenied"] = ntp_ ? ntp_->aclDeniedCount() : 0;
  ntp["otaRefused"] = ntp_ ? ntp_->otaRefuseCount() : 0;
  ntp["clients"] = ntp_ ? ntp_->activeClientCount() : 0;

  String out;
  serializeJson(doc, out);
  sendNoCache();
  server_.send(200, "application/json", out);
}

void WebPortal::handleMetrics() {
  // Prometheus-ish text; no auth (read-only, same as /status).
  char buf[768];
  const uint32_t served = ntp_ ? ntp_->servedCount() : 0;
  const uint32_t rate = ntp_ ? ntp_->rateLimitedCount() : 0;
  const uint32_t denied = ntp_ ? ntp_->deniedCount() : 0;
  const uint32_t dropped = ntp_ ? ntp_->droppedCount() : 0;
  const uint32_t aclDenied = ntp_ ? ntp_->aclDeniedCount() : 0;
  const uint32_t otaRefused = ntp_ ? ntp_->otaRefuseCount() : 0;
  const uint32_t reqs = ntp_ ? ntp_->requestCount() : 0;
  const uint8_t clients = ntp_ ? ntp_->activeClientCount() : 0;
  const unsigned heap = ESP.getFreeHeap();
  const unsigned aclMode = ntp_ ? static_cast<unsigned>(ntp_->aclMode()) : 0;
  const unsigned aclCount = ntp_ ? ntp_->aclCount() : 0;
  const unsigned otaBusy = ipcOtaBusy() ? 1 : 0;
  snprintf(buf, sizeof(buf),
           "# TYPE ntp_requests_total counter\n"
           "ntp_requests_total %lu\n"
           "# TYPE ntp_served_total counter\n"
           "ntp_served_total %lu\n"
           "# TYPE ntp_rate_limited_total counter\n"
           "ntp_rate_limited_total %lu\n"
           "# TYPE ntp_denied_total counter\n"
           "ntp_denied_total %lu\n"
           "# TYPE ntp_dropped_total counter\n"
           "ntp_dropped_total %lu\n"
           "# TYPE ntp_acl_denied_total counter\n"
           "ntp_acl_denied_total %lu\n"
           "# TYPE ntp_ota_refused_total counter\n"
           "ntp_ota_refused_total %lu\n"
           "# TYPE ntp_acl_mode gauge\n"
           "ntp_acl_mode %u\n"
           "# TYPE ntp_acl_entries gauge\n"
           "ntp_acl_entries %u\n"
           "# TYPE ntp_clients gauge\n"
           "ntp_clients %u\n"
           "# TYPE ota_busy gauge\n"
           "ota_busy %u\n"
           "# TYPE esp_free_heap_bytes gauge\n"
           "esp_free_heap_bytes %u\n",
           static_cast<unsigned long>(reqs), static_cast<unsigned long>(served),
           static_cast<unsigned long>(rate), static_cast<unsigned long>(denied),
           static_cast<unsigned long>(dropped), static_cast<unsigned long>(aclDenied),
           static_cast<unsigned long>(otaRefused), aclMode, aclCount,
           static_cast<unsigned>(clients), otaBusy, heap);
  server_.send(200, "text/plain; charset=utf-8", buf);
}

