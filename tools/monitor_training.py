#!/usr/bin/env python3
import http.server
import json
import os
import time

LOG_FILE = "/tmp/opencode/lora_train.log"
PORT = 8137

HTML = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Ashgrove LoRA Training</title>
<style>
  body { font-family: ui-monospace, Menlo, Consolas, monospace; background:#0d1117; color:#e6edf3; margin:0; padding:24px; }
  h1 { font-size:20px; color:#58a6ff; }
  .stat { display:inline-block; background:#161b22; border:1px solid #30363d; border-radius:8px; padding:14px 20px; margin:6px; min-width:130px; }
  .stat b { display:block; font-size:26px; color:#7ee787; }
  .stat span { font-size:11px; color:#8b949e; }
  .bar { background:#21262d; border-radius:8px; height:24px; margin:16px 0; overflow:hidden; width:100%; }
  .bar > div { background:linear-gradient(90deg,#1f6feb,#388bfd); height:100%; width:0; transition:width .5s; }
  #log { background:#161b22; border:1px solid #30363d; border-radius:8px; padding:14px; white-space:pre-wrap; max-height:60vh; overflow-y:auto; font-size:13px; }
  #meta { color:#8b949e; font-size:12px; margin-bottom:8px; }
  .row { margin-top:20px; }
</style>
</head>
<body>
<h1>Ashgrove &#8212; Qwen2.5-0.5B LoRA Training</h1>
<div id="meta">Connecting&#8230;</div>
<div>
  <div class="stat"><b id="pct">0%</b><span>progress</span></div>
  <div class="stat"><b id="step">0/0</b><span>step / total</span></div>
  <div class="stat"><b id="loss">-</b><span>train loss</span></div>
  <div class="stat"><b id="evl">-</b><span>eval loss</span></div>
  <div class="stat"><b id="elapsed">0s</b><span>elapsed</span></div>
  <div class="stat"><b id="eta">-</b><span>ETA</span></div>
  <div class="stat"><b id="status">-</b><span>status</span></div>
</div>
<div class="bar"><div id="fill"></div></div>
<div class="row">Log:</div>
<div id="log">(waiting for training to start&#8230;)</div>
<script>
async function refresh(){
  try{
    const r = await fetch('/api');
    const d = await r.json();
    document.getElementById('pct').textContent = d.pct + '%';
    document.getElementById('step').textContent = d.step + ' / ' + d.total;
    document.getElementById('loss').textContent = d.loss ?? '-';
    document.getElementById('evl').textContent = d.evl ?? '-';
    document.getElementById('elapsed').textContent = d.elapsed;
    document.getElementById('eta').textContent = d.eta ?? '-';
    document.getElementById('status').textContent = d.status;
    document.getElementById('fill').style.width = d.pct + '%';
    document.getElementById('log').textContent = d.log;
    document.getElementById('meta').textContent = 'last update ' + new Date(d.ts*1000).toLocaleTimeString() +
      ' &middot; epochs=' + d.epochs + ' lr=' + d.lr + ' r=' + d.r + ' alpha=' + d.alpha +
      ' &middot; train=' + d.train + ' val=' + d.val + ' &middot; host=0.0.0.0:' + d.port;
    const el = document.getElementById('log');
    el.scrollTop = el.scrollHeight;
  }catch(e){ document.getElementById('meta').textContent = 'connection error: ' + e; }
  setTimeout(refresh, 2000);
}
refresh();
</script>
</body>
</html>
"""


def parse_log():
    meta = {"epochs": "?", "lr": "?", "r": "?", "alpha": "?",
            "train": "?", "val": "?", "port": PORT}
    last = {}
    try:
        with open(LOG_FILE) as f:
            lines = f.read().splitlines()
        for ln in lines:
            if ln.startswith("epochs="):
                for kv in ln.split():
                    if "=" in kv:
                        k, v = kv.split("=", 1)
                        meta[k] = v
                continue
            # e.g. [   12/ 240]  5.0%  123s loss=2.3401
            if "[" in ln and "%" in ln:
                step, total = 0, 0
                import re
                m = re.search(r"\[\s*(\d+)\s*/\s*(\d+)\]\s*([\d.]+)%\s*(\d+)s", ln)
                if m:
                    step, total = int(m.group(1)), int(m.group(2))
                    pct = float(m.group(3))
                    elapsed = int(m.group(4))
                    last["step"], last["total"], last["pct"], last["elapsed"] = \
                        step, total, pct, elapsed
                lm = re.search(r"loss=([\d.]+|NaN|inf)", ln)
                if lm:
                    last["loss"] = lm.group(1)
                em = re.search(r"eval_loss=([\d.]+|NaN|inf)", ln)
                if em:
                    last["evl"] = em.group(1)
            if "TRAINING_COMPLETE" in ln:
                last["status"] = "COMPLETE"
    except FileNotFoundError:
        pass
    total = last.get("total", 0)
    pct = last.get("pct", 0)
    elapsed = last.get("elapsed", 0)
    eta = None
    if pct and total and elapsed:
        rem = (elapsed / pct) * (100 - pct)
        eta = "%dh%02dm" % (rem // 3600, (rem % 3600) // 60)
    status = last.get("status", "RUNNING" if last.get("step") else "STARTING")
    log_text = "\n".join(lines) if "lines" in dir() and lines else "(waiting for training to start...)"
    return {
        "pct": round(pct, 1),
        "step": last.get("step", 0),
        "total": total,
        "loss": last.get("loss"),
        "evl": last.get("evl"),
        "elapsed": "%ds" % elapsed,
        "eta": eta,
        "status": status,
        "log": log_text,
        "ts": int(time.time()),
        "epochs": meta["epochs"], "lr": meta["lr"], "r": meta["r"],
        "alpha": meta["alpha"], "train": meta["train"], "val": meta["val"],
        "port": PORT,
    }


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/api":
            body = json.dumps(parse_log()).encode()
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
