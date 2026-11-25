# RL78 programozás CH341A-val

A CH341A **UART módban, 3,3 V-os** jelszinten kell használni (CH341SER driverrel). Az elem legyen a termosztátban, a VDD-t nem kell bekötni.

```
CH341A GND ─────────── J1 G
CH341A RXD ──┬──────── J1 T (TOOL0)
CH341A TXD ─[1k]┘
CH341A DTR ─────────── J1 R (RESET)
```

```
rl78flash.exe -v -a -x -P 0 -C 1024 -D 1024 -m1 -p 3.0 COMx q3rf_rl78_v1.0.mot
```
- A `-x` kapcsolóval megmarad a párosítás. Nélküle (vagy új panelen) újra kell párosítani a vevővel.
- Ha `FAILED baud rate set` hibát kapsz, próbáld újra.
- Írás után **vedd le az adaptert a J1-ről**, különben a termosztát nem indul el.
