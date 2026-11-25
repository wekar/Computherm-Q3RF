/* ============================================================================
 *  Q3RF termosztat -- ESP32 hid  (WiFi + BLE + HiveMQ Cloud MQTT)
 *
 *  Az ESP32 a termosztat RL78 vezerlojevel UART-on beszel (lcd_temp_main.c,
 *  "ESP32-KAPCSOLAT" resz), a telefonos app pedig
 *    - kozelrol BLE-n (titkositott, 6 jegyu PIN-nel parositva),
 *    - tavolrol a HiveMQ Cloud MQTT szerveren at (TLS, 8883)
 *  eri el.  A szabalyozast tovabbra is az RL78 vegzi; ha az ESP32 vagy a
 *  halozat kiesik, a termosztat a legutolso beallitassal tovabb mukodik.
 *
 *  BEKOTES (ESP32-WROOM DevKit / kesobb ESP32-C3):
 *    ESP32 TX  (GPIO17 | C3: GPIO4) --4k7--> RL78 P11/RxD0 (pin45, jumper kozepso sor)
 *    ESP32 RX  (GPIO16 | C3: GPIO5) <------- RL78 P12/TxD0 (pin44, jumper also sor)
 *    ESP32 WAKE(GPIO4  | C3: GPIO6) -------> RL78 P30/KR3  (pin21, TP1)
 *      WAKE: kozvetlen, ellenallas NELKUL (open-drain; 4k7 + P30 felhuzo
 *      feszultsegoszto -> nem menne le alacsonyra). TX/WAKE open-drain,
 *      RX-en nincs felhuzo -> nincs visszataplalas az RL78 fele.
 *    GND kozos.  (A jumpereket ki kell venni a sorbol!)
 *
 *  PARANCSOK (szoveg; BLE-n a "cmd" karakterisztikaba irva, MQTT-n a
 *  q3rf/<eszkoz>/cmd temaba kuldve):
 *    SP=21.5          beallitott ertek (18.0 ... 28.0, 0.5-os lepes)
 *    PAIR             parositas a vevovel (vevon elotte M/A -> zold villog)
 *    GET              allapot azonnali kuldese
 *   -- csak BLE-n (biztonsagi okbol): --
 *    WIFI=<ssid>\t<jelszo>
 *    MQTT=<host>\t<port>\t<felhasznalo>\t<jelszo>
 *    PIN=<6 szamjegy>   uj BLE-PIN (a meglevo parositasok torlodnek)
 *    REBOOT
 *  ALLAPOT (JSON): BLE "status" karakterisztika (olvasas + ertesites),
 *  MQTT q3rf/<eszkoz>/state (retained), q3rf/<eszkoz>/online ("1"/"0", LWT).
 *  HOME ASSISTANT: MQTT Discovery (homeassistant/...), klima + szenzorok +
 *  parositas gomb.  Ket kulon tarolt szerver, egyszerre egy aktiv (csak BLE-n):
 *    MQTT=<host>\t<port>\t<felh.>\t<jelszo>   HiveMQ Cloud, TLS
 *    HA=<host>\t<port>\t<felh.>\t<jelszo>     Home Assistant Mosquitto, TLS nelkul
 *    BROKER=HIVEMQ | BROKER=HA                 melyik legyen aktiv
 * ========================================================================== */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <time.h>
#include "root_ca.h"
#include <driver/gpio.h>

#if CONFIG_IDF_TARGET_ESP32C3
static const int PIN_TX = 4, PIN_RX = 5, PIN_WAKE = 6;
#define RLSER Serial1
#else
static const int PIN_TX = 17, PIN_RX = 16, PIN_WAKE = 4;
#define RLSER Serial2
#endif

#define SVC_UUID    "7a1c0000-5133-4252-8f1a-9b2e51335246"
#define STATUS_UUID "7a1c0001-5133-4252-8f1a-9b2e51335246"
#define CMD_UUID    "7a1c0002-5133-4252-8f1a-9b2e51335246"

/* ---------------- beallitasok (NVS) ---------------- */
static Preferences prefs;
static String cfgSsid, cfgWpass, cfgMhost, cfgMuser, cfgMpass;
static uint16_t cfgMport = 8883;
/* Home Assistant (helyi Mosquitto, TLS nelkul) + melyik szerver az aktiv */
static String cfgHhost, cfgHuser, cfgHpass;
static uint16_t cfgHport = 1883;
static uint8_t cfgBroker = 0;        /* 0 = HiveMQ Cloud (TLS), 1 = Home Assistant */
static uint32_t cfgPin = BLE_DEFAULT_PASSKEY;

static void loadConfig() {
  prefs.begin("q3rf", true);
  cfgSsid  = prefs.getString("ssid", "");
  cfgWpass = prefs.getString("wpass", "");
  cfgMhost = prefs.getString("mhost", "");
  cfgMport = prefs.getUShort("mport", 8883);
  cfgMuser = prefs.getString("muser", "");
  cfgMpass = prefs.getString("mpass", "");
  cfgPin   = prefs.getUInt("pin", BLE_DEFAULT_PASSKEY);
  cfgHhost = prefs.getString("hhost", "");
  cfgHport = prefs.getUShort("hport", 1883);
  cfgHuser = prefs.getString("huser", "");
  cfgHpass = prefs.getString("hpass", "");
  cfgBroker = prefs.getUChar("broker", 0);
  prefs.end();
}

/* ---------------- allapot ---------------- */
static String devId, topicBase;
static struct {
  int t10 = -1000, sp10 = 0, heat = 0;
  String id = "----";
  uint32_t lastRx = 0;
  bool any = false;
} rl;
static String lastMsg = "";
static bool stateDirty = true;

static bool rlLinkOk() { return rl.any && (millis() - rl.lastRx) < 35000UL; }

/* ---------------- RL78 UART-kapcsolat ---------------- */
static String pendCmd;           /* pl. "P,215" vagy "L" vagy "Q" */
static int pendExpectSp = -1;
static uint8_t pendTries = 0;
static uint32_t pendSent = 0;

static uint8_t nmeaCs(const String &s) {
  uint8_t c = 0;
  for (size_t i = 0; i < s.length(); i++) c ^= (uint8_t)s[i];
  return c;
}
static void rlSendNow(const String &body) {
  char tail[8];
  snprintf(tail, sizeof(tail), "*%02X\r\n", nmeaCs(body));
  digitalWrite(PIN_WAKE, LOW);   /* ebresztes: lefuto el a P30/KR3-on */
  delay(2);
  digitalWrite(PIN_WAKE, HIGH);
  delay(5);
  RLSER.print('$');
  RLSER.print(body);
  RLSER.print(tail);
  Serial.printf("[RL78] -> $%s%s", body.c_str(), tail);
}
static void rlQueue(const String &body, int expectSp = -1) {
  pendCmd = body;
  pendExpectSp = expectSp;
  pendTries = 0;
  pendSent = 0;
}
static void rlService() {
  if (pendCmd.isEmpty()) return;
  if (pendSent != 0 && millis() - pendSent < 3000UL) return;
  if (pendTries >= 4) {
    Serial.printf("[RL78] nincs valasz: %s\n", pendCmd.c_str());
    lastMsg = "A termosztat nem valaszol";
    pendCmd = "";
    stateDirty = true;
    return;
  }
  rlSendNow(pendCmd);
  pendTries++;
  pendSent = millis();
}
static void rlHandleLine(const String &line) {
  /* $S,<t10>,<sp10>,<heat>,<ID>*CS   vagy   $E,<kod>*CS */
  int star = line.lastIndexOf('*');
  if (!line.startsWith("$") || star < 2 || star + 3 > (int)line.length()) return;
  String body = line.substring(1, star);
  if (strtoul(line.substring(star + 1, star + 3).c_str(), nullptr, 16) != nmeaCs(body)) {
    Serial.printf("[RL78] rossz checksum: %s\n", line.c_str());
    return;
  }
  if (body.startsWith("S,")) {
    int a = body.indexOf(',', 2), b = body.indexOf(',', a + 1), c = body.indexOf(',', b + 1);
    if (a < 0 || b < 0 || c < 0) return;
    int t = body.substring(2, a).toInt();
    int sp = body.substring(a + 1, b).toInt();
    int h = body.substring(b + 1, c).toInt();
    String id = body.substring(c + 1);
    if (t != rl.t10 || sp != rl.sp10 || h != rl.heat || id != rl.id || !rl.any) stateDirty = true;
    rl.t10 = t; rl.sp10 = sp; rl.heat = h; rl.id = id;
    rl.lastRx = millis();
    rl.any = true;
    if (!pendCmd.isEmpty() && pendSent != 0) {
      /* P: a kert SP-vel jott vissza; Q: barmely $S a valasz (ms-on belul jon);
         L: a parositas ~1,4 s, az az elotti $S meg a regi allapot */
      bool done = (pendExpectSp >= 0) ? (sp == pendExpectSp)
                 : (pendCmd == "Q") ? true
                 : (millis() - pendSent > 300UL);
      if (done) { pendCmd = ""; lastMsg = "OK"; stateDirty = true; }
    }
  } else if (body.startsWith("E,")) {
    Serial.printf("[RL78] hiba: %s\n", body.c_str());
    rl.lastRx = millis();
    /* hibas vetel az RL78-on -> 0,5 s mulva ujra (nem 3 s) */
    if (!pendCmd.isEmpty() && pendSent != 0) pendSent = millis() - 2500UL;
  }
}
static void rlPoll() {
  static String buf;
  while (RLSER.available()) {
    char ch = (char)RLSER.read();
    if (ch == '\n') {
      buf.trim();
      if (buf.length()) { Serial.printf("[RL78] <- %s\n", buf.c_str()); rlHandleLine(buf); }
      buf = "";
    } else if (buf.length() < 64) {
      buf += ch;
    }
  }
}

/* ---------------- allapot JSON ---------------- */
static String fmt10(int v) {
  char b[12];
  snprintf(b, sizeof(b), "%s%d.%d", v < 0 ? "-" : "", abs(v) / 10, abs(v) % 10);
  return String(b);
}
static String jsonEsc(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}
static PubSubClient *mqttPtr = nullptr;
static String stateJson(bool withConfig) {
  String j = "{";
  j += "\"t\":" + String(rl.any && rl.t10 != -1000 ? fmt10(rl.t10) : "null");
  j += ",\"sp\":" + String(rl.any ? fmt10(rl.sp10) : "null");
  j += ",\"heat\":" + String(rl.heat);
  j += ",\"id\":\"" + rl.id + "\"";
  j += ",\"link\":" + String(rlLinkOk() ? 1 : 0);
  j += ",\"busy\":" + String(pendCmd.isEmpty() ? 0 : 1);
  j += ",\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? 1 : 0);
  j += ",\"mqtt\":" + String(mqttPtr && mqttPtr->connected() ? 1 : 0);
  if (WiFi.status() == WL_CONNECTED) {
    j += ",\"rssi\":" + String(WiFi.RSSI());
    j += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  }
  j += ",\"dev\":\"" + devId + "\"";
  j += ",\"fw\":\"" FW_VERSION "\"";
  j += ",\"msg\":\"" + jsonEsc(lastMsg) + "\"";
  if (withConfig) {
    j += ",\"ssid\":\"" + jsonEsc(cfgSsid) + "\"";
    j += ",\"mhost\":\"" + jsonEsc(cfgMhost) + "\"";
    j += ",\"mport\":" + String(cfgMport);
    j += ",\"muser\":\"" + jsonEsc(cfgMuser) + "\"";
    j += ",\"hhost\":\"" + jsonEsc(cfgHhost) + "\"";
    j += ",\"hport\":" + String(cfgHport);
    j += ",\"huser\":\"" + jsonEsc(cfgHuser) + "\"";
  }
  j += ",\"broker\":\"" + String(cfgBroker ? "ha" : "hivemq") + "\"";
  j += "}";
  return j;
}

/* ---------------- WiFi + MQTT ---------------- */
static WiFiClientSecure tls;
static WiFiClient plain;             /* port 1883: TLS nelkul (pl. Home Assistant Mosquitto) */
static PubSubClient mqtt(tls);
static bool isHa() { return cfgBroker == 1; }
static bool mqttUseTls() { return !isHa(); }
static const String &bHost() { return isHa() ? cfgHhost : cfgMhost; }
static uint16_t bPort()      { return isHa() ? cfgHport : cfgMport; }
static const String &bUser() { return isHa() ? cfgHuser : cfgMuser; }
static const String &bPass() { return isHa() ? cfgHpass : cfgMpass; }
/* kapcsolat bontasa: elotte "0" az online temara (rendes bontasnal nincs LWT) */
static void mqttDrop() {
  if (mqtt.connected()) {
    mqtt.publish((topicBase + "/online").c_str(), "0", true);
    mqtt.disconnect();
  }
}

/* ---------------- Home Assistant MQTT Discovery ----------------
 * A HA (MQTT integracio) a homeassistant/<komponens>/<eszkoz>/<objektum>/config
 * retained uzenetekbol magatol felveszi az eszkozt.  Ujrakuldjuk csatlakozaskor
 * es amikor a HA ujraindul (homeassistant/status = "online"). */
static const char *HA_PREFIX = "homeassistant";

static String haDevice() {
  return ",\"dev\":{\"ids\":[\"" + devId + "\"],\"name\":\"Q3RF termosztát\","
         "\"mf\":\"Computherm (saját fw)\",\"mdl\":\"Q3RF\",\"sw\":\"" FW_VERSION "\"}";
}
static void haPublish(const char *comp, const char *obj, const String &body) {
  String topic = String(HA_PREFIX) + "/" + comp + "/" + devId + "/" + obj + "/config";
  String j = "{\"~\":\"" + topicBase + "\",\"uniq_id\":\"" + devId + "_" + obj + "\","
             "\"avty_t\":\"~/online\",\"pl_avail\":\"1\",\"pl_not_avail\":\"0\"," + body + haDevice() + "}";
  bool ok = mqtt.publish(topic.c_str(), j.c_str(), true);
  Serial.printf("[HA] %s/%s %s (%u B)\n", comp, obj, ok ? "OK" : "HIBA", (unsigned)j.length());
}
static void haDiscovery() {
  haPublish("climate", "thermostat",
    "\"name\":null,"
    "\"curr_temp_t\":\"~/state\",\"curr_temp_tpl\":\"{{ value_json.t }}\","
    "\"temp_stat_t\":\"~/state\",\"temp_stat_tpl\":\"{{ value_json.sp }}\","
    "\"temp_cmd_t\":\"~/cmd\",\"temp_cmd_tpl\":\"SP={{ value }}\","
    "\"act_t\":\"~/state\",\"act_tpl\":\"{{ 'heating' if value_json.heat else 'idle' }}\","
    "\"modes\":[\"heat\"],\"mode_stat_t\":\"~/state\",\"mode_stat_tpl\":\"heat\","
    "\"min_temp\":18,\"max_temp\":28,\"temp_step\":0.5,\"precision\":0.1,\"temp_unit\":\"C\"");
  haPublish("sensor", "temperature",
    "\"name\":\"Hőmérséklet\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\","
    "\"unit_of_meas\":\"°C\",\"stat_t\":\"~/state\",\"val_tpl\":\"{{ value_json.t }}\"");
  haPublish("binary_sensor", "heating",
    "\"name\":\"Fűtés\",\"dev_cla\":\"heat\",\"stat_t\":\"~/state\","
    "\"val_tpl\":\"{{ 'ON' if value_json.heat else 'OFF' }}\"");
  haPublish("binary_sensor", "link",
    "\"name\":\"Termosztát kapcsolat\",\"dev_cla\":\"connectivity\",\"ent_cat\":\"diagnostic\","
    "\"stat_t\":\"~/state\",\"val_tpl\":\"{{ 'ON' if value_json.link else 'OFF' }}\"");
  haPublish("sensor", "rssi",
    "\"name\":\"WiFi jel\",\"dev_cla\":\"signal_strength\",\"unit_of_meas\":\"dBm\","
    "\"ent_cat\":\"diagnostic\",\"stat_t\":\"~/state\",\"val_tpl\":\"{{ value_json.rssi | default(none) }}\"");
  haPublish("button", "pair",
    "\"name\":\"Párosítás a vevővel\",\"ent_cat\":\"config\",\"cmd_t\":\"~/cmd\",\"pl_prs\":\"PAIR\"");
}
static uint32_t mqttNextTry = 0, mqttBackoff = 2000;
static bool timeOk = false;

static void wifiStart() {
  WiFi.disconnect(true);
  if (cfgSsid.isEmpty()) { Serial.println("[WiFi] nincs beallitva"); return; }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(devId.c_str());
  WiFi.begin(cfgSsid.c_str(), cfgWpass.c_str());
  Serial.printf("[WiFi] csatlakozas: %s\n", cfgSsid.c_str());
}

static void handleCommand(String cmd, bool fromBle);

static void mqttCallback(char *topic, byte *payload, unsigned int len) {
  String s;
  for (unsigned int i = 0; i < len && i < 300; i++) s += (char)payload[i];
  if (strcmp(topic, "homeassistant/status") == 0) {   /* HA ujraindult -> discovery ujra */
    if (s == "online") haDiscovery();
    stateDirty = true;
    return;
  }
  Serial.printf("[MQTT] cmd: %s\n", s.c_str());
  handleCommand(s, false);
}

static void mqttService() {
  if (WiFi.status() != WL_CONNECTED || bHost().isEmpty()) {
    if (mqtt.connected()) mqtt.disconnect();
    return;
  }
  if (!timeOk && mqttUseTls()) {       /* a tanusitvany-ellenorzeshez pontos ido kell */
    static bool ntpStarted = false;
    if (!ntpStarted) { configTime(0, 0, "pool.ntp.org", "time.google.com"); ntpStarted = true; }
    if (time(nullptr) < 1700000000) return;
    timeOk = true;
    Serial.println("[NTP] ido rendben");
  }
  if (mqtt.connected()) { mqtt.loop(); return; }
  if (millis() < mqttNextTry) return;
  String online = topicBase + "/online";
  Serial.printf("[MQTT] csatlakozas (%s): %s:%u\n", isHa() ? "Home Assistant" : "HiveMQ TLS", bHost().c_str(), bPort());
  if (mqttUseTls()) mqtt.setClient(tls); else mqtt.setClient(plain);
  mqtt.setServer(bHost().c_str(), bPort());
  /* ures felhasznalo -> anonim kapcsolodas (Mosquitto-nal lehet ilyen) */
  bool con = bUser().isEmpty()
           ? mqtt.connect(devId.c_str(), online.c_str(), 1, true, "0")
           : mqtt.connect(devId.c_str(), bUser().c_str(), bPass().c_str(), online.c_str(), 1, true, "0");
  if (con) {
    Serial.println("[MQTT] csatlakozva");
    mqtt.publish(online.c_str(), "1", true);
    mqtt.subscribe((topicBase + "/cmd").c_str(), 1);
    if (isHa()) {
      mqtt.subscribe("homeassistant/status", 0);
      haDiscovery();
    }
    mqttBackoff = 2000;
    stateDirty = true;
  } else {
    Serial.printf("[MQTT] hiba: %d\n", mqtt.state());
    mqttNextTry = millis() + mqttBackoff;
    mqttBackoff = min<uint32_t>(mqttBackoff * 2, 60000);
  }
}

/* ---------------- BLE ---------------- */
static NimBLEServer *bleServer = nullptr;
static NimBLECharacteristic *chStatus = nullptr, *chCmd = nullptr;
static QueueHandle_t bleQueue;
struct BleMsg { char s[300]; };

class CmdCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    BleMsg m;
    std::string v = c->getValue();
    size_t n = v.size() < sizeof(m.s) - 1 ? v.size() : sizeof(m.s) - 1;
    memcpy(m.s, v.data(), n);
    m.s[n] = 0;
    xQueueSend(bleQueue, &m, 0);       /* a fociklus dolgozza fel */
  }
};
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, ble_gap_conn_desc *d) override {
    Serial.println("[BLE] kapcsolodott");
    stateDirty = true;
  }
  void onDisconnect(NimBLEServer *s) override {
    Serial.println("[BLE] bontva");
    NimBLEDevice::startAdvertising();
  }
  void onAuthenticationComplete(ble_gap_conn_desc *d) override {
    Serial.printf("[BLE] parositas: %s\n", d->sec_state.encrypted ? "OK" : "SIKERTELEN");
  }
};

static void bleStart() {
  NimBLEDevice::init(devId.c_str());
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setSecurityAuth(true, true, true);          /* bonding, MITM, secure connections */
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);   /* a telefon kerdezi a PIN-t */
  NimBLEDevice::setSecurityPasskey(cfgPin);
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());
  NimBLEService *svc = bleServer->createService(SVC_UUID);
  chStatus = svc->createCharacteristic(STATUS_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::NOTIFY);
  chCmd = svc->createCharacteristic(CMD_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  chCmd->setCallbacks(new CmdCallbacks());
  svc->start();
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);
  adv->setScanResponse(true);
  adv->start();
  Serial.printf("[BLE] hirdetes: %s (PIN %06lu)\n", devId.c_str(), (unsigned long)cfgPin);
}

/* ---------------- parancsok ---------------- */
static void handleCommand(String cmd, bool fromBle) {
  cmd.trim();
  if (cmd.isEmpty()) return;
  String up = cmd;
  int eq = up.indexOf('=');
  String key = (eq >= 0 ? up.substring(0, eq) : up);
  key.toUpperCase();
  String val = (eq >= 0 ? cmd.substring(eq + 1) : String());

  if (key == "GET") {
    stateDirty = true;
    rlQueue("Q");
  } else if (key == "SP") {
    float f = val.toFloat();
    int sp = (int)lroundf(f * 10.0f);
    if (sp < 180) sp = 180;
    if (sp > 280) sp = 280;
    sp = ((sp + 2) / 5) * 5;
    rlQueue("P," + String(sp), sp);
    lastMsg = "Beallitas: " + fmt10(sp);
    stateDirty = true;
  } else if (key == "PAIR") {
    rlQueue("L");
    lastMsg = "Parositas folyamatban";
    stateDirty = true;
  } else if (!fromBle) {
    lastMsg = "Ez a parancs csak Bluetooth-on engedelyezett";
    stateDirty = true;
  } else if (key == "WIFI") {
    int tab = val.indexOf('\t');
    cfgSsid = tab >= 0 ? val.substring(0, tab) : val;
    cfgWpass = tab >= 0 ? val.substring(tab + 1) : "";
    prefs.begin("q3rf", false);
    prefs.putString("ssid", cfgSsid);
    prefs.putString("wpass", cfgWpass);
    prefs.end();
    lastMsg = "WiFi mentve";
    stateDirty = true;
    wifiStart();
  } else if (key == "MQTT" || key == "HA") {
    bool ha = (key == "HA");
    String f[4];
    int from = 0;
    for (int i = 0; i < 4; i++) {
      int tab = val.indexOf('\t', from);
      f[i] = (tab >= 0 && i < 3) ? val.substring(from, tab) : val.substring(from);
      from = tab + 1;
      if (tab < 0) break;
    }
    uint16_t port = f[1].length() ? (uint16_t)f[1].toInt() : (ha ? 1883 : 8883);
    prefs.begin("q3rf", false);
    if (ha) {
      cfgHhost = f[0]; cfgHport = port; cfgHuser = f[2]; cfgHpass = f[3];
      prefs.putString("hhost", cfgHhost);
      prefs.putUShort("hport", cfgHport);
      prefs.putString("huser", cfgHuser);
      prefs.putString("hpass", cfgHpass);
    } else {
      cfgMhost = f[0]; cfgMport = port; cfgMuser = f[2]; cfgMpass = f[3];
      prefs.putString("mhost", cfgMhost);
      prefs.putUShort("mport", cfgMport);
      prefs.putString("muser", cfgMuser);
      prefs.putString("mpass", cfgMpass);
    }
    prefs.end();
    if (ha == isHa()) mqttDrop();      /* csak ha az aktiv szerver beallitasa valtozott */
    mqttNextTry = 0;
    mqttBackoff = 2000;
    lastMsg = ha ? "Home Assistant mentve" : "HiveMQ mentve";
    stateDirty = true;
  } else if (key == "BROKER") {
    String v = val; v.trim(); v.toUpperCase();
    uint8_t b = (v == "HA") ? 1 : (v == "HIVEMQ") ? 0 : 0xFF;
    if (b == 0xFF) {
      lastMsg = "BROKER=HIVEMQ vagy BROKER=HA";
    } else {
      if (b != cfgBroker) mqttDrop();
      cfgBroker = b;
      prefs.begin("q3rf", false);
      prefs.putUChar("broker", cfgBroker);
      prefs.end();
      mqttNextTry = 0;
      mqttBackoff = 2000;
      lastMsg = b ? "Aktiv: Home Assistant" : "Aktiv: HiveMQ";
    }
    stateDirty = true;
  } else if (key == "PIN") {
    if (val.length() == 6 && val.toInt() >= 0) {
      cfgPin = (uint32_t)val.toInt();
      prefs.begin("q3rf", false);
      prefs.putUInt("pin", cfgPin);
      prefs.end();
      NimBLEDevice::deleteAllBonds();
      lastMsg = "Uj PIN mentve, ujraindulas";
      stateDirty = true;
      delay(300);
      ESP.restart();
    } else {
      lastMsg = "A PIN 6 szamjegy legyen";
      stateDirty = true;
    }
  } else if (key == "REBOOT") {
    ESP.restart();
  } else {
    lastMsg = "Ismeretlen parancs: " + key;
    stateDirty = true;
  }
}

/* ---------------- setup / loop ---------------- */
void setup() {
  Serial.begin(115200);
  delay(200);
  /* WAKE open-drain: csak lehuz, magasra az RL78 belso felhuzoja viszi
     (nincs 3,3 V-os betap az RL78 elemes VDD-jebe, ellenallas sem kell) */
  digitalWrite(PIN_WAKE, HIGH);
  pinMode(PIN_WAKE, OUTPUT_OPEN_DRAIN);
  RLSER.begin(38400, SERIAL_8N1, PIN_RX, PIN_TX);
  /* NINCS visszataplalas az RL78 VDD-jébe (elem nélkül 1,8 V-ot mértünk rajta
     -> órákig az LVD-küszöbön billegő RL78 -> sérült kódflash):
     - TX open-drain: csak lehúz, magasra az RL78 P11 belső felhúzója viszi;
     - RX-en nincs ESP-oldali felhúzó: az RL78 P12 felhúzója tartja magasan,
       amíg a TxD0 Hi-Z (elem nélkül lebeg -> szemét, a checksum kiszűri). */
  gpio_set_direction((gpio_num_t)PIN_TX, GPIO_MODE_OUTPUT_OD);  /* az UART-jel utvonalat nem bantja */
  gpio_pullup_dis((gpio_num_t)PIN_RX);
  gpio_pulldown_dis((gpio_num_t)PIN_RX);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[20];
  snprintf(b, sizeof(b), "q3rf-%02X%02X%02X", mac[3], mac[4], mac[5]);
  devId = b;
  topicBase = "q3rf/" + devId;
  Serial.printf("\nQ3RF ESP32 hid %s, eszkoz: %s\n", FW_VERSION, devId.c_str());

  loadConfig();
  bleQueue = xQueueCreate(4, sizeof(BleMsg));
  tls.setCACert(ROOT_CA);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(MQTT_MAX_PACKET_SIZE);
  mqtt.setKeepAlive(60);
  mqttPtr = &mqtt;
  bleStart();
  wifiStart();
  rlQueue("Q");
}

void loop() {
  BleMsg m;
  while (xQueueReceive(bleQueue, &m, 0) == pdTRUE) {
    Serial.printf("[BLE] cmd: %s\n", m.s);
    handleCommand(String(m.s), true);
  }

  rlPoll();
  rlService();
  mqttService();

  static bool lastWifi = false, lastMqtt = false, lastLink = false;
  bool w = WiFi.status() == WL_CONNECTED, q = mqtt.connected(), l = rlLinkOk();
  if (w != lastWifi || q != lastMqtt || l != lastLink) {
    if (w && !lastWifi) Serial.printf("[WiFi] csatlakozva, IP %s\n", WiFi.localIP().toString().c_str());
    lastWifi = w; lastMqtt = q; lastLink = l;
    stateDirty = true;
  }

  static uint32_t lastPub = 0;
  if (stateDirty || millis() - lastPub > 60000UL) {
    stateDirty = false;
    lastPub = millis();
    if (chStatus) {
      chStatus->setValue(stateJson(true));
      chStatus->notify();
    }
    if (mqtt.connected()) mqtt.publish((topicBase + "/state").c_str(), stateJson(false).c_str(), true);
  }

  /* link nelkul 15 s-onkent lekerdezes (az RL78 csak akkor kuld magatol,
     ha mar latott ervenyes parancsot) */
  static uint32_t lastQ = 0;
  if (!rlLinkOk() && pendCmd.isEmpty() && millis() - lastQ > 15000UL) {
    lastQ = millis();
    rlQueue("Q");
  }
  delay(5);
}
