#!/usr/bin/env python3
"""Web UI to monitor Town Consciousness dataset generation progress.

Reads data/dataset_consolidation_progress.json (written by
gen_consolidation_dataset.py) and serves a live dashboard on port 8138.
Mirrors tools/monitor_training.py.
"""
import http.server
import json
import os
import re
import time

PROGRESS_FILE = "data/dataset_consolidation_progress.json"
PORT = 8138

HTML = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Ashgrove Dataset Generation</title>
<style>
  body { font-family: ui-monospace, Menlo, Consolas, monospace; background:#0d1117; color:#e6edf3; margin:0; padding:24px; }
  h1 { font-size:20px; color:#58a6ff; }
  .stat { display:inline-block; background:#161b22; border:1px solid #30363d; border-radius:8px; padding:14px 20px; margin:6px; min-width:130px; }
  .stat b { display:block; font-size:26px; color:#7ee787; }
  .stat span { font-size:11px; color:#8b949e; }
  .bar { background:#21262d; border-radius:8px; height:24px; margin:16px 0; overflow:hidden; width:100%; }
  .bar > div { background:linear-gradient(90deg,#1f6ebf,#388bfd); height:100%; width:0; transition:width .5s; }
  #log { background:#161b22; border:1px solid #30363d; border-radius:8px; padding:14px; white-space:pre-wrap; max-height:50vh; overflow-y:auto; font-size:12px; color:#8b949e; }
  #meta { color:#8b949e; font-size:12px; margin-bottom:8px; }
  .row { margin-top:20px; }
</style>
</head>
<body>
<h1>Ashgrove &#8212; Town Consciousness Dataset Generation</h1>
<div id="meta">Connecting&#8230;</div>
<div>
  <div class="stat"><b id="pct">0%</b><span>scenarios done</span></div>
  <div class="stat"><b id="done">0/0</b><span>done / total</span></div>
  <div class="stat"><b id="valid">-</b><span>valid rows</span></div>
  <div class="stat"><b id="invalid">-</b><span>invalid outputs</span></div>
  <div class="stat"><b id="failed">-</b><span>failed</span></div>
  <div class="stat"><b id="rpm">-</b><span>req / min</span></div>
  <div class="stat"><b id="eta">-</b><span>ETA</span></div>
  <div class="stat"><b id="status">-</b><span>status</span></div>
</div>
<div class="bar"><div id="fill"></div></div>
<div class="row">Recent errors / output:</div>
<div id="log">(waiting for generation to start&#8230;)</div>
<script>
async function refresh(){
  try{
    const r = await fetch('/api');
    const d = await r.json();
    document.getElementById('pct').textContent = d.pct + '%';
    document.getElementById('done').textContent = d.done + ' / ' + d.total;
    document.getElementById('valid').textContent = d.ok;
    document.getElementById('invalid').textContent = d.invalid;
    document.getElementById('failed').textContent = d.failed;
    document.getElementById('rpm').textContent = d.rpm;
    document.getElementById('eta').textContent = d.eta ?? '-';
    document.getElementById('status').textContent = d.status;
    document.getElementById('fill').style.width = d.pct + '%';
    document.getElementById('log').textContent = d.log;
    document.getElementById('meta').textContent = 'last update ' + new Date(d.ts*1000).toLocaleTimeString() +
      ' &middot; host=0.0.0.0:' + d.port;
    const el = document.getElementById('log');
    el.scrollTop = el.scrollHeight;
  }catch(e){ document.getElementById('meta').textContent = 'connection error: ' + e; }
  setTimeout(refresh, 3000);
}
refresh();
</script>
</body>
</html>
"""


def read_progress():
    d = {}
    if os.path.exists(PROGRESS_FILE):
        try:
            with open(PROGRESS_FILE) as f:
                d = json.load(f)
        except (json.JSONDecodeError, OSError):
            d = {}
    total = int(d.get("n_scenarios", 0))
    done = int(d.get("done", 0) or d.get("ok", 0))
    pct = 100.0 * done / total if total else 0.0
    ok = int(d.get("ok", 0))
    invalid = int(d.get("invalid", 0))
    failed = int(d.get("failed", 0))
    requests = int(d.get("requests", 0))
    ts = int(d.get("ts", time.time()))
    start_ts = int(d.get("start_ts", ts))
    eta = None
    elapsed = max(0, time.time() - start_ts)
    if requests > 0 and elapsed > 0:
        rps = requests / elapsed
        rpm = rps * 60
        remaining = max(0, total - done)
        if rps > 0:
            eta_s = remaining / rps
            eta = "%dh%02dm" % (eta_s // 3600, (eta_s % 3600) // 60)
    else:
        rpm = 0
    status = d.get("status", "STARTING")
    msg = d.get("msg", "")
    log = "status=%s\n" % status
    if msg:
        log += "msg=%s\n" % msg
    log += "last scenario=%s done=%d ok=%d invalid=%d failed=%d requests=%d\n" % (
        d.get("seed", "?"), done, ok, invalid, failed, requests)
    return {
        "pct": round(pct, 1),
        "done": done,
        "total": total,
        "ok": ok,
        "invalid": invalid,
        "failed": failed,
        "rpm": round(rpm, 1),
        "eta": eta,
        "status": status,
        "log": log,
        "ts": ts,
        "port": PORT,
    }


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/api":
            body = json.dumps(read_progress()).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            body = HTML.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print("serving on 0.0.0.0:%d" % PORT, flush=True)
    srv.serve_forever()