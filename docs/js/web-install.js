/* SPDX-License-Identifier: GPL-3.0-only -- Amalthea browser installer (EXPERIMENTAL prototype)
 *
 * The same steps as installer/install.sh and restore.sh (installer/lib/common.sh), from Chrome or Edge over
 * WebUSB ADB (Tango / ya-webadb, vendored in js/vendor/webadb.js). Everything that changes the Brick is done
 * by the device-side script shipped in the release (payload/amalthea-device.sh), exactly as the command-line
 * installer runs it. The Brick's old adbd has no exit codes and no exec: service, so every command goes
 * through "shell:" and its result is read from the RESULT= line the script prints.
 *
 * Test hooks (used by installer/tests/web/run.sh against the simulated Brick, never against hardware):
 *   ?sim=ws://127.0.0.1:8765/adb  ADB over a WebSocket to installer/tests/web/fake_adbd.py instead of WebUSB
 *   &zip=URL   fetch the installer archive from URL     &opfs   write backups to the origin-private file system
 *   &yes       answer every question "yes"              &auto=install|restore   start at once, report to /result
 */
import {
  Adb, AdbDaemonTransport, AdbDaemonWebUsbDeviceManager, AdbDaemonWebUsbDevice, AdbWebCredentialStore,
  AdbPacketSerializeStream, MaybeConsumable, pipeFrom, unzipSync, sha256,
} from "./vendor/webadb.js";

const DEV_STAGE = "/mnt/UDISK/amalthea-install";
const DEV_BACKUP = "/mnt/UDISK/amalthea-stock-backup";
const DEV_SESSIONS = "/mnt/UDISK/tracker-linux";
const TESTED_KERNEL = "#804";      // stock firmware 1.0.6 (December 2024)
const REPO = "nzimas/amalthea";

const Q = new URLSearchParams(location.search);
const SIM = Q.get("sim");
const YES = SIM !== null && Q.has("yes");
const OPFS = SIM !== null && Q.has("opfs");
const $ = (id) => document.getElementById(id);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const hex = (b) => Array.from(b, (x) => x.toString(16).padStart(2, "0")).join("");
const td = new TextDecoder();

/* ---------- output ---------- */
const transcript = [];
function out(text, cls) {
  transcript.push(text);
  const line = document.createElement("div");
  if (cls) line.className = cls;
  line.textContent = text;
  $("wi-log").append(line);
  line.scrollIntoView({ block: "nearest" });
}
const say = (t) => out(t);
const step = (t) => out("==> " + t, "step");
class Stop extends Error {}
const die = (t) => { throw new Stop(t); };

function progress(label, done, total) {
  const p = $("wi-progress");
  if (label === null) { p.hidden = true; return; }
  p.hidden = false;
  $("wi-progress-label").textContent = label;
  const bar = $("wi-progress-bar");
  if (total) { bar.max = total; bar.value = done; } else bar.removeAttribute("value");
}
const mb = (n) => (n / 1048576).toFixed(n < 10485760 ? 1 : 0) + " MB";

/* a question in the page: resolves with the label of the button pressed (inside the click, so the answer
 * can open a picker or a USB chooser, which browsers allow only in direct response to a click) */
function ask(text, buttons, onClick) {
  return new Promise((resolve, reject) => {
    const box = document.createElement("div");
    box.className = "wi-ask";
    const p = document.createElement("p"); p.textContent = text; box.append(p);
    for (const b of buttons) {
      const btn = document.createElement("button");
      btn.type = "button"; btn.className = "btn small" + (b === buttons[0] ? " primary" : "");
      btn.textContent = b;
      btn.addEventListener("click", async () => {
        box.querySelectorAll("button").forEach((x) => (x.disabled = true));
        try { resolve(onClick ? await onClick(b) : b); } catch (e) { reject(e); }
        box.remove();
        out(`${text} — ${b}`);
      });
      box.append(btn);
    }
    $("wi-log").append(box);
    box.scrollIntoView({ block: "nearest" });
  });
}
async function confirm(text) {
  if (YES) { out(text + " — yes (test mode)"); return; }
  if ((await ask(text, ["Yes, continue", "Cancel"])) !== "Yes, continue") die("Cancelled. Nothing was changed.");
}

/* ---------- the installer package (the release archive) ---------- */
let pkg = null;    // { build, files: Map(name -> Uint8Array) }

async function hashBytes(b) { return hex(new Uint8Array(await crypto.subtle.digest("SHA-256", b))); }

async function loadZip(bytes, name) {
  let entries;
  try { entries = unzipSync(bytes, { filter: (f) => /\/payload\/[^/]+$/.test(f.name) }); }
  catch (e) { throw new Stop(`${name} is not a readable zip archive (${e.message}).`); }
  const files = new Map();
  for (const [path, data] of Object.entries(entries)) files.set(path.split("/").pop(), data);
  for (const f of ["BUILD", "SHA256SUMS", "amalthea-device.sh", "busybox", "amalthea-boot.img", "amalthea-rootfs.img.gz",
                   "rootfs.sha256", "boot.sha256", "amalthea", "usb_f_uac2.ko"])
    if (!files.has(f)) throw new Stop(`${name} is not an Amalthea installer archive (payload/${f} is missing).`);
  for (const line of td.decode(files.get("SHA256SUMS")).split("\n")) {
    const [sum, f] = line.trim().split(/\s+/);
    if (!sum) continue;
    if (!files.has(f) || (await hashBytes(files.get(f))) !== sum) throw new Stop(`${name} is damaged (checksum of ${f}). Download it again.`);
  }
  return { build: td.decode(files.get("BUILD")).trim(), files, name };
}

async function choosePackage(file) {
  $("wi-pkg-status").textContent = "Checking " + file.name + "…";
  try {
    pkg = await loadZip(new Uint8Array(await file.arrayBuffer()), file.name);
    $("wi-pkg-status").textContent = `${file.name}: Amalthea build ${pkg.build}, every file verified.`;
    $("wi-pkg-status").className = "ok";
    if (latest && Number(pkg.build) < Number(latest)) $("wi-pkg-status").textContent += ` (Build ${latest} is newer.)`;
  } catch (e) {
    pkg = null;
    $("wi-pkg-status").textContent = e.message; $("wi-pkg-status").className = "bad";
  }
  ready();
}

let latest = null;
async function latestRelease() {
  // The API answers cross-origin; the asset download itself does not (GitHub's release storage sends no
  // CORS header), so the archive is downloaded by the user and chosen here.
  try {
    const r = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`);
    const j = await r.json();
    const a = j.assets.find((x) => /^amalthea-\d+-installer\.zip$/.test(x.name));
    latest = (j.tag_name || "").replace(/^build-/, "");
    $("wi-latest").innerHTML = "";
    const link = document.createElement("a");
    link.href = a.browser_download_url; link.textContent = a.name;
    $("wi-latest").append("Latest release: ", link, ` (${mb(a.size)}).`);
  } catch { /* offline, or rate-limited: the static link in the page still works */ }
}

/* ---------- the connection ---------- */
const credentials = new AdbWebCredentialStore("Amalthea installer");
const usb = SIM ? null : AdbDaemonWebUsbDeviceManager.BROWSER;
let adb = null, serial = "";

/* ADB packets over a WebSocket (test only): the same AdbDaemonConnection shape as the WebUSB one */
function wsDevice(url) {
  return {
    serial: "SIMSERIAL01",
    async connect() {
      const ws = new WebSocket(url);
      ws.binaryType = "arraybuffer";
      await new Promise((ok, bad) => { ws.onopen = ok; ws.onerror = () => bad(new Error("simulated Brick not reachable")); });
      let buf = new Uint8Array(0);
      const readable = new ReadableStream({
        start(c) {
          ws.onmessage = (ev) => {
            const d = new Uint8Array(ev.data), nb = new Uint8Array(buf.length + d.length);
            nb.set(buf); nb.set(d, buf.length); buf = nb;
            while (buf.length >= 24) {
              const v = new DataView(buf.buffer, buf.byteOffset, 24), n = v.getUint32(12, true);
              if (buf.length < 24 + n) break;
              c.enqueue({ command: v.getUint32(0, true), arg0: v.getUint32(4, true), arg1: v.getUint32(8, true),
                          payloadLength: n, checksum: v.getUint32(16, true), magic: v.getUint32(20, true),
                          payload: buf.slice(24, 24 + n) });
              buf = buf.slice(24 + n);
            }
          };
          ws.onclose = () => { try { c.close(); } catch {} };
        },
        cancel() { ws.close(); },
      });
      const writable = pipeFrom(new MaybeConsumable.WritableStream({
        write(chunk) { if (ws.readyState === 1) ws.send(chunk.slice()); },
        close() { ws.close(); },
      }), new AdbPacketSerializeStream());
      return { readable, writable };
    },
  };
}

async function open(device) {
  let connection;
  try { connection = await device.connect(); }
  catch (e) {
    if (e instanceof AdbDaemonWebUsbDevice.DeviceBusyError)
      die("Another program is using the Brick's ADB connection. Quit it (on the command line: adb kill-server), close other installer tabs, and try again.");
    throw e;
  }
  const transport = await AdbDaemonTransport.authenticate({ serial: device.serial, connection, credentialStore: credentials });
  serial = device.serial;
  return new Adb(transport);
}

async function findDevice(prefer) {
  if (SIM) return wsDevice(SIM);
  const list = await usb.getDevices();
  return list.find((d) => d.serial === prefer) || (list.length === 1 ? list[0] : null);
}

/* the first connection: the USB chooser, inside a click */
async function connectChosen() {
  if (SIM) return open(wsDevice(SIM));
  const device = await usb.requestDevice();
  if (!device) die("No device was chosen.");
  return open(device);
}

/* after a reboot: wait for the Brick to come back (wait_for_brick), without a click while the browser
 * still has permission for it; if it reappears as a device it has no permission for, ask for a click */
async function waitForBrick(seconds) {
  const until = Date.now() + seconds * 1000;
  let asked = null, shownAt = Date.now() + 45000;
  while (Date.now() < until) {
    try {
      const d = await findDevice(serial);
      if (d) return await withTimeout(open(d), 15000, "no ADB answer");
    } catch (e) { if (e instanceof Stop) throw e; }
    if (!SIM && !asked && Date.now() > shownAt) {
      asked = ask("The Brick has not reappeared yet. If it is on and connected, the browser may need you to choose it again.",
        ["Choose the Brick"], async () => { const d = await usb.requestDevice(); return d ? open(d) : null; })
        .catch(() => null);
      asked.then((a) => { if (a) asked.done = a; });
    }
    if (asked && asked.done) return asked.done;
    await sleep(2000);
  }
  die("No Brick found over ADB.");
}

function withTimeout(p, ms, what) {
  return Promise.race([p, sleep(ms).then(() => { throw new Error(what); })]);
}

async function disconnect() {
  const a = adb; adb = null;
  if (a) await withTimeout(a.close(), 3000, "close").catch(() => {});
}

/* ---------- ADB operations, as the shell scripts use them ---------- */
async function shell(cmd) {
  return (await adb.createSocketAndWait("shell:" + cmd)).replace(/\r/g, "");
}

async function withSync(fn) {
  const sync = await adb.sync();
  try { return await fn(sync); } finally { await sync.dispose().catch(() => {}); }
}

/* a stream that counts what passes through it (for the progress bar) and hashes it */
function meter(label, total, hash) {
  let done = 0, shown = 0;
  return new TransformStream({
    transform(chunk, c) {
      if (hash) hash.update(chunk);
      done += chunk.length;
      if (done - shown > 1048576 || done === total) { shown = done; progress(`${label}: ${mb(done)} of ${mb(total)}`, done, total); }
      c.enqueue(chunk);
    },
  });
}

async function push(sync, name, blob, path, mode = 0o644) {
  try {
    await sync.write({ filename: path, file: blob.stream().pipeThrough(meter(name, blob.size)), permission: mode,
                       mtime: Math.floor(Date.now() / 1000) });
  } catch (e) { die(`Could not copy ${name} to the Brick (${e.message}).`); }
}

async function pushInstaller() {
  step("Copying the installer to the Brick");
  await shell(`mkdir -p ${DEV_STAGE}`);
  await withSync(async (sync) => {
    for (const [name, data] of pkg.files) await push(sync, name, new Blob([data]), `${DEV_STAGE}/${name}`);
  });
  progress(null);
  await shell(`chmod 755 ${DEV_STAGE}/busybox`);
}

/* device COMMAND: run the device-side script, print its LOG lines, fail on RESULT=FAIL */
let OUT = "";
async function device(cmd, what) {
  if (what) progress(what);
  try { OUT = await shell(`${DEV_STAGE}/busybox sh ${DEV_STAGE}/amalthea-device.sh ${cmd}`); }
  finally { if (what) progress(null); }
  for (const l of OUT.split("\n")) if (l.startsWith("LOG ")) say("    " + l.slice(4));
  const result = OUT.split("\n").filter((l) => l.startsWith("RESULT=")).pop() || "";
  if (result === "RESULT=OK") return;
  if (result.startsWith("RESULT=FAIL")) die(result.slice(12));
  die("The Brick did not answer as expected:\n" + OUT);
}

let P = {};
async function probe() {
  await device("probe");
  const val = (k) => (OUT.split("\n").find((l) => l.startsWith(k + "=")) || "").slice(k.length + 1);
  P = { HW: val("HW"), SYSTEM: val("SYSTEM"), KERNEL: val("KERNEL"), STOCK: val("STOCK_VERSION"), BATTERY: val("BATTERY"),
        BACKUP: val("BACKUP"), MODE: val("MODE"), BOOT_CURRENT: val("BOOT_CURRENT"), INSTALLED_ROOT: val("INSTALLED_ROOT"),
        REASON: val("RECOVERY_REASON") };
  say(`    Brick ${serial}: ${P.SYSTEM} system, kernel ${P.KERNEL}, battery ${P.BATTERY}%`);
}

function needBattery() {
  if (!(Number(P.BATTERY) >= 50)) die(`The battery is at ${P.BATTERY}%. Charge the Brick to at least 50% and run this again.`);
}

async function reboot(cmd) {
  // the daemon dies with the system: the command never answers, the connection just goes away
  await withTimeout(shell(cmd), 8000, "rebooting").catch(() => {});
  await disconnect();
}

/* ---------- the computer's folder (File System Access API) ---------- */
let folder = null;
async function needFolder(why) {
  if (folder) return folder;
  if (OPFS) return (folder = await navigator.storage.getDirectory());
  const pick = async () => (folder = await window.showDirectoryPicker({ id: "amalthea", mode: "readwrite", startIn: "documents" }));
  await ask(why, ["Choose a folder", "Cancel"], (b) => (b === "Cancel" ? die("Cancelled. Nothing was changed.") : pick()));
  say(`    Folder: ${folder.name}`);
  return folder;
}

async function fileIn(dir, name) {
  try { return await (await dir.getFileHandle(name)).getFile(); } catch { return null; }
}

async function hashFile(file, label) {
  const h = sha256.create();
  await file.stream().pipeThrough(meter(label, file.size, h)).pipeTo(new WritableStream());
  return hex(h.digest());
}

/* check_sums: every "sum  name" line of SHA256SUMS against the files next to it */
async function checkSums(dir) {
  const sums = await fileIn(dir, "SHA256SUMS");
  if (!sums) return false;
  for (const line of (await sums.text()).split("\n")) {
    const [sum, name] = line.trim().split(/\s+/);
    if (!sum) continue;
    const f = await fileIn(dir, name);
    if (!f || (await hashFile(f, "Checking " + name)) !== sum) { progress(null); return false; }
  }
  progress(null);
  return true;
}

async function pullTo(sync, path, dir, name, optional) {
  const fh = await dir.getFileHandle(name, { create: true });
  const w = await fh.createWritable();
  const h = sha256.create();
  let done = 0, shown = 0;
  try {
    const reader = sync.read(path).getReader();
    for (;;) {
      const { value, done: end } = await reader.read();
      if (end) break;
      h.update(value); await w.write(value); done += value.length;
      if (done - shown > 4194304) { shown = done; progress(`${name}: ${mb(done)}`, done, name === "stock-rootfs.img" ? 524288000 : 0); }
    }
    await w.close();
  } catch (e) {
    await w.abort().catch(() => {});
    await dir.removeEntry(name).catch(() => {});
    if (optional) return null;
    die(`Could not copy ${name} from the Brick (${e.message}).`);
  } finally { progress(null); }
  return hex(h.digest());
}

async function pullStockBackup() {
  const top = await needFolder("Choose the folder on this computer where the stock backup is kept (about 515 MB). Keep it: restoring the stock system uses it.");
  const dest = await top.getDirectoryHandle(`backup-${serial}`, { create: true });
  if (await checkSums(dest)) { say(`    A verified copy is already on this computer: ${top.name}/backup-${serial}`); return; }
  const got = {};
  await withSync(async (sync) => {
    for (const f of ["SHA256SUMS", "stock-version.txt", "stock-boot.img", "stock-rootfs.img"])
      got[f] = await pullTo(sync, `${DEV_BACKUP}/${f}`, dest, f);
  });
  const sums = await (await fileIn(dest, "SHA256SUMS")).text();
  for (const line of sums.split("\n")) {
    const [sum, name] = line.trim().split(/\s+/);
    if (sum && got[name] !== sum) die("The copied backup does not match the Brick's. Run the installer again.");
  }
  say(`    Saved to ${top.name}/backup-${serial} (keep this folder: restoring uses it)`);
}

async function backupSessions() {
  const top = await needFolder("Choose a folder on this computer for a copy of your saved sessions.");
  const d = new Date(), p = (n) => String(n).padStart(2, "0");
  const name = `sessions-${serial}-${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`;
  const dest = await top.getDirectoryHandle(name, { create: true });
  await withSync(async (sync) => {
    for (const f of ["session-slots", "fx-session-slots", "lifeblood-session-slots", "session.amalthea", "device-settings.amalthea"])
      await pullTo(sync, `${DEV_SESSIONS}/${f}`, dest, f, true);
  });
  say(`    Saved sessions copied to ${top.name}/${name}`);
}

/* ---------- the flows (install.sh / restore.sh) ---------- */
async function waitForAmalthea() {
  step("Waiting for the Brick to install and start Amalthea (one to three minutes; do not disconnect it)");
  progress("Waiting for the Brick to restart");
  await sleep(SIM ? 3000 : 15000);
  try { adb = await waitForBrick(400); } finally { progress(null); }
  await probe();
  if (P.SYSTEM === "amalthea" && P.MODE === "system") {
    say(""); out("Done. Amalthea is installed and running.", "ok");
    say("Read the manual: https://nzimas.github.io/amalthea/manual.html");
    return;
  }
  if (P.SYSTEM === "recovery")
    die(`The Brick stopped in Amalthea's RAM recovery: ${P.REASON || "no reason recorded"}.\nNothing is lost. Run the installer again to retry, or restore the stock system.`);
  die(`The Brick restarted into an unexpected state (${P.SYSTEM}, mode ${P.MODE || "none"}).`);
}

async function installFromStock() {
  if (!P.KERNEL.includes(TESTED_KERNEL)) {
    say(""); say(`WARNING: this Brick runs stock firmware ${P.STOCK || "unknown"} (kernel ${P.KERNEL}).`);
    say(`Amalthea was developed on stock 1.0.6 (kernel ${TESTED_KERNEL}). It brings its own kernel, but on this firmware it is untested.`);
    await confirm("Continue anyway?");
  }
  needBattery();
  await needFolder("Choose the folder on this computer where the stock backup will be kept (about 515 MB). Keep it: restoring the stock system uses it.");
  step("Backing up the stock system on the Brick");
  await device("backup", "Reading the stock partitions on the Brick (a few minutes)");
  step("Copying the stock backup to this computer");
  await pullStockBackup();
  step("Unpacking Amalthea on the Brick");
  await device("stage", "Unpacking on the Brick");
  step("Writing the Amalthea boot partition");
  await device("arm", "Writing the boot partition");
  step("Restarting the Brick");
  await reboot("reboot");
  await waitForAmalthea();
}

async function updateAmalthea() {
  say(""); say(`This Brick already runs Amalthea: it will be updated to build ${pkg.build}.`);
  say("The instruments restart blank and stopped, as on every start: save your session first.");
  await confirm("Update now?");
  step("Copying your saved sessions to this computer");
  await backupSessions();
  step("Unpacking the update on the Brick");
  await device("stage", "Unpacking on the Brick");
  await probe();
  const want = td.decode(pkg.files.get("rootfs.sha256")).split(" ")[0];
  if (P.INSTALLED_ROOT === want && P.BOOT_CURRENT === "yes") {
    step("Restarting the instruments");
    await shell("kill -TERM $(pidof amalthea)").catch(() => {});
    say(""); out(`Done. Amalthea build ${pkg.build} is running.`, "ok");
    return;
  }
  needBattery();
  step("Writing the Amalthea boot partition");
  await device("arm", "Writing the boot partition");
  step("Restarting the Brick");
  await reboot("reboot");
  await waitForAmalthea();
}

async function retryFromRecovery() {
  say(""); say(`The Brick is in Amalthea's RAM recovery: ${P.REASON || "no reason recorded"}.`);
  await confirm("Retry the installation?");
  await device("retry-install");
  step("Restarting the Brick");
  await reboot("reboot -f");
  await waitForAmalthea();
}

async function install() {
  out(`Amalthea installer · build ${pkg.build}`, "step");
  await pushInstaller();
  await probe();
  if (P.HW !== "TG3040") die("The connected device is not a TRIMUI Brick (TG3040). Nothing was changed.");
  if (P.SYSTEM === "stock") return installFromStock();
  if (P.SYSTEM === "amalthea") return updateAmalthea();
  if (P.SYSTEM === "recovery") return retryFromRecovery();
  die("The Brick is running a system this installer does not recognise. Nothing was changed.");
}

async function localBackup() {
  const top = await needFolder(`There is no stock backup on the Brick. Choose the folder that holds backup-${serial} (or the backup folder itself).`);
  for (const dir of [await top.getDirectoryHandle(`backup-${serial}`).catch(() => null), top]) {
    if (!dir || !(await fileIn(dir, "SHA256SUMS"))) continue;
    if (!(await checkSums(dir))) die(`The backup in ${dir.name} is damaged.`);
    return dir;
  }
  die(`No stock backup in ${top.name} (looked for backup-${serial}/SHA256SUMS and SHA256SUMS).`);
}

async function restore() {
  out("Restore the stock TRIMUI system", "step");
  await pushInstaller();
  await probe();
  if (P.HW !== "TG3040") die("The connected device is not a TRIMUI Brick (TG3040). Nothing was changed.");
  if (P.SYSTEM === "stock") { out("This Brick is already running the stock system. Nothing to do.", "ok"); return; }
  if (P.SYSTEM === "amalthea") {
    if (P.BACKUP !== "present") {
      const dir = await localBackup();
      step("Copying the stock backup to the Brick");
      await shell(`mkdir -p ${DEV_BACKUP}`);
      await withSync(async (sync) => {
        for (const f of ["stock-boot.img", "stock-rootfs.img", "SHA256SUMS"]) await push(sync, f, await fileIn(dir, f), `${DEV_BACKUP}/${f}`);
      });
      progress(null);
    }
    step("Preparing recovery");
    await device("restore-prepare");
    step("Restarting into Amalthea's RAM recovery");
    await reboot("reboot");
    progress("Waiting for the Brick to restart");
    await sleep(SIM ? 2000 : 8000);
    try { adb = await waitForBrick(180); } finally { progress(null); }
    await probe();
    if (P.SYSTEM !== "recovery") die(`The Brick did not restart into recovery (it reports: ${P.SYSTEM}). Run the restore again.`);
  } else if (P.SYSTEM !== "recovery") die("The Brick is running a system this installer does not recognise. Nothing was changed.");
  step("Writing the stock system (about a minute; keep the cable connected)");
  await device("restore-write", "Writing the stock partitions");
  step("Restarting into the stock system");
  await reboot("reboot -f");
  // (restore.sh stops here; the page also waits to see the stock system answer, when it does)
  progress("Waiting for the stock system");
  await sleep(SIM ? 2000 : 15000);
  try { adb = await waitForBrick(SIM ? 60 : 180); await probe(); } catch { P.SYSTEM = ""; } finally { progress(null); }
  say("");
  out(P.SYSTEM === "stock" ? "Done. The Brick is running the stock TRIMUI system again."
                           : "Done. The Brick is restarting into the stock TRIMUI system.", "ok");
  say("Amalthea's saved sessions remain in internal storage (/mnt/UDISK/tracker-linux).");
}

/* ---------- the page ---------- */
let busy = false;
function ready() {
  const ok = !busy && pkg && $("wi-ack").checked;
  $("wi-install").disabled = !ok;
  $("wi-restore").disabled = !(!busy && pkg);
}

async function run(flow, name) {
  busy = true; ready();
  $("wi-log").hidden = false;
  let result = "ok", error = "";
  try {
    if (!adb) { step("Connecting to the Brick"); adb = await connectChosen(); }
    say(`    Connected: ${serial}`);
    await flow();
  } catch (e) {
    result = "fail"; error = e.message;
    out("ERROR: " + (e instanceof Stop ? e.message : `${e.message || e} (unexpected; the log above shows how far it got)`), "bad");
  } finally {
    progress(null);
    await disconnect();
    busy = false; ready();
    if (SIM !== null && Q.has("auto")) fetch("/result", { method: "POST", body: JSON.stringify({ flow: name, result, error, log: transcript }) }).catch(() => {});
  }
}

function init() {
  const missing = [];
  if (!window.isSecureContext) missing.push("a secure (https) page");
  if (!SIM && !navigator.usb) missing.push("WebUSB");
  if (!OPFS && !window.showDirectoryPicker) missing.push("the File System Access API");
  if (missing.length) {
    $("wi-support").hidden = false;
    $("wi-support").textContent = `This browser lacks ${missing.join(" and ")}. Use a current Chrome or Edge on a computer, or the command-line installer.`;
    return;
  }
  if (SIM) { $("wi-sim").hidden = false; $("wi-sim").textContent = `TEST MODE: simulated Brick at ${SIM}. No USB device is used.`; }
  $("wi-zip").addEventListener("change", (e) => e.target.files[0] && choosePackage(e.target.files[0]));
  $("wi-ack").addEventListener("change", ready);
  $("wi-install").addEventListener("click", () => run(install, "install"));
  $("wi-restore").addEventListener("click", () => run(restore, "restore"));
  latestRelease();
  ready();
  const zip = SIM !== null && Q.get("zip");
  if (zip) fetch(zip).then((r) => r.blob()).then((b) => choosePackage(new File([b], zip.split("/").pop()))).then(() => {
    const auto = Q.get("auto");
    if (auto === "install") { $("wi-ack").checked = true; run(install, "install"); }
    if (auto === "restore") run(restore, "restore");
  });
}
init();
