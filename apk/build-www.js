// Az ../app webappból elkészíti a www/ mappát az Android apphoz:
// bemásolja a fájlokat, és a fő szkript elé beszúrja a Capacitor + BLE plugin szkriptjeit.
const fs = require("fs"), path = require("path");
const src = path.join(__dirname, "..", "app"), dst = path.join(__dirname, "www");
fs.rmSync(dst, { recursive: true, force: true });
fs.mkdirSync(dst, { recursive: true });
for (const f of fs.readdirSync(src)) if (!f.includes(".bak") && f !== "sw.js") fs.copyFileSync(path.join(src, f), path.join(dst, f));
fs.copyFileSync(require.resolve("@capacitor/core/dist/capacitor.js"), path.join(dst, "capacitor.js"));
fs.copyFileSync(require.resolve("@capacitor-community/bluetooth-le/dist/plugin.js"), path.join(dst, "ble-plugin.js"));
const idx = path.join(dst, "index.html");
let html = fs.readFileSync(idx, "utf8");
const tag = '<script src="mqtt.min.js"></script>';
if (!html.includes(tag)) throw new Error("mqtt.min.js script tag not found");
html = html.replace(tag, '<script src="capacitor.js"></script>\n<script src="ble-plugin.js"></script>\n' + tag);
fs.writeFileSync(idx, html);
console.log("www/ kész");
