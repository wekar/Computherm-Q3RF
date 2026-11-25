# Q3RF saját firmware

Computherm Q3RF termosztát (RL78 R7F0C001G2) saját firmware-rel. Opcionálisan egy ESP32 hídon át vezérelhető telefonról vagy Home Assistantból.

*Nem hivatalos, hobbi célú firmware – a Computherm gyártóhoz nincs köze. Használat saját felelősségre.*

| Mappa | Tartalom |
|---|---|
| `rl78/` | `q3rf_rl78_v1.0.mot` termosztát firmware + forrás. Programozás: `PROGRAMOZAS_CH341A.md` |
| `esp32/` | `q3rf_esp32_v0.3.0_merged.bin` (0x0-ra), `bin/` külön fájlok, forrás |
| `app/` | a telefonos app forrása (webapp) |
| `apk/` | Android-csomagolás (Capacitor). Az APK-t a GitHub Actions fordítja, és a **Releases** oldalra teszi |
| `BEKOTES.md` | RL78 ↔ ESP32 bekötés |

Fordítás: RL78 – `sh build.sh lcd_temp_main.c q3rf_rl78` (a `BIN=` sor legyen a saját toolchain-útvonalad). ESP32 – PlatformIO: `pio run -e esp32dev`.

## RL78 (termosztát)
A termosztát ESP32 nélkül is teljes értékűen működik.
- Hőmérsékletmérés 10 s-onként. Beállítás 18–28 °C között, 0,5 °C-os lépésben (elemcsere után 21,0 °C). Fűtés ±0,2 °C hiszterézissel.
- Rádióvezérlés a vevő felé az eredeti protokollal.
- LEARN gomb: új ID-t generál, és elmenti az adatflashbe, így elemcsere után is megmarad.
- ESP32 felé: UART 38400 8N1, ébresztés a P30 lábon. Parancsok: `$Q`, `$P,<sp×10>`, `$L`. Válasz: `$S,<T×10>,<SP×10>,<fűt>,<ID>`.
- **Ha módosítod a forrást:** a ROM adatflash-író rutinja hibás, ezt a `_rom_wr_fix` kerüli meg – ne vedd ki. A `.bss` maradjon FFD00 alatt. A flash shield window 15..15.

## ESP32 (híd, v0.3.0)
- **Bluetooth LE** PIN-nel (alap PIN: 123456). Eszköznév: `q3rf-XXXXXX`.
- **MQTT, két választható szerverrel.** Mindkettő beállítása tárolódik, de egyszerre csak az egyik aktív:
  - **HiveMQ Cloud** (TLS 8883): az app távolról is eléri a termosztátot.
  - **Home Assistant** (helyi Mosquitto, TLS nélkül, 1883): MQTT Discovery-vel automatikusan megjelenik. Entitások: termosztát, hőmérséklet, fűtés, kapcsolat, WiFi-jel, párosítás gomb.
- Témák: `q3rf/<eszköz>/state`, `.../cmd`, `.../online`.
- Parancsok: `SP=21.5`, `PAIR`, `GET`. Csak Bluetooth-on: `WIFI=`, `MQTT=` (HiveMQ), `HA=` (Home Assistant), `BROKER=HIVEMQ|HA`, `PIN=`, `REBOOT`. A mezőket tabulátor választja el.
- Feltöltés: `esptool.py --chip esp32 write_flash 0x0 q3rf_esp32_v0.3.0_merged.bin`. A korábbi beállítások megmaradnak.

## App
Mutatja a hőmérsékletet, a beállított értéket és a fűtés állapotát. Közelről Bluetooth-on, távolról HiveMQ-n keresztül csatlakozik. Bluetooth-on lehet beállítani a WiFi-t, a szervert (HiveMQ vagy Home Assistant) és a PIN-t. Telepítés: a **Releases** oldalról töltsd le a `q3rf-termosztat.apk` fájlt, és nyisd meg a telefonon (engedélyezni kell az ismeretlen forrásból való telepítést). Első csatlakozáskor a telefon kéri a Bluetooth PIN-t.

Új APK készítése: `git tag v0.3.1 && git push --tags`, vagy az Actions fülön az „APK” munkafolyamat kézi indítása.

## Home Assistant beüzemelése
1. HA-ban telepítsd a **Mosquitto broker** bővítményt, és add hozzá az **MQTT** integrációt.
2. Az appban csatlakozz Bluetooth-on, és állítsd be a WiFi-t.
3. „Termosztát szervere” → **Home Assistant** → add meg a HA gép IP-címét, az `1883` portot, a felhasználót és a jelszót → **Mentés és bekapcsolás**.
4. A HA-ban megjelenik a **Q3RF termosztát** eszköz. Home Assistant módban az app csak Bluetooth-on éri el a termosztátot.
