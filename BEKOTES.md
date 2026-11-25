# Bekötés: RL78 ↔ ESP32 (WROOM-32 DevKit)

| ESP32 | | RL78 | Panelen |
|---|---|---|---|
| GPIO17 (TX) | → 4,7 kΩ → | pin 45, P11/RxD0 | jumper-blokk középső sora |
| GPIO16 (RX) | ← | pin 44, P12/TxD0 | jumper-blokk alsó sora |
| GPIO4 (WAKE) | → közvetlenül | pin 21, P30/KR3 | TP1 |
| GND | — | GND | elem – |

UART 38400 8N1. Rajz: `BEKOTES_jumper_blokk.svg`.

- A középső és az alsó sorból **vedd ki a jumpert**, és a sor jobb tüskéjére forraszd a vezetéket. A bal oszlopra (pin 47) ne forrassz.
- Az ESP32 a saját USB-tápjáról megy, az RL78 az elemről. Csak a GND közös.
- A TX és a WAKE kimenetet az ESP32 firmware open-drain-re állítja, így nincs visszatáplálás az RL78 felé. Más ESP firmware-rel ez nem garantált.
- A WAKE-be ne tegyél ellenállást, mert akkor a láb nem húzódna le eléggé, és az RL78 nem ébredne fel.
- Üzem közben a J1 programozó-csatlakozón ne maradjon semmi.
- ESP32-C3 esetén (nem kipróbált): TX = GPIO4, RX = GPIO5, WAKE = GPIO6.
