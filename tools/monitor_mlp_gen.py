#!/usr/bin/env python3
"""Progress web UI for MLP data generation (runs on :8139)."""
import json
import os
import threading
import time
from pathlib import Path
from http.server import HTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

PORT = 8139
DATA_DIR = Path("data")
LOG_FILE = DATA_DIR / "mlp_gen_progress.jsonl"

class ProgressHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/progress":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            
            # Read progress from file
            progress = {"attention": 0, "action_evaluator": 0, "world_model": 0, "total": 0}
            if LOG_FILE.exists():
                with open(LOG_FILE) as f:
                    for line in f:
                        try:
                            entry = json.loads(line)
                            progress[entry["task"]] = entry.get("completed", 0)
                        except Exception:
                            pass
                progress["total"] = sum(v for k, v in progress.items() if k != "total")
            
            self.wfile.write(json.dumps(progress).encode())
            return
        
        if parsed.path == "/":
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(HTML.encode())
            return
        
        self.send_response(404)
        self.end_headers()

    def log_message(self, format, *args):
        pass  # suppress logs

HTML = """
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>MLP Data Generation Progress</title>
    <style>
        body { font-family: ui-monospace, Menlo, Consolas, monospace; background:#0d1117; color:#e6edf3; margin:0; padding:24px; }
        h1 { color:#58a6ff; font-size:20px; margin-bottom:16px; }
        .stat { display:inline-block; background:#161b22; border:1px solid #30363d; border-radius:8px; padding:16px 20px; margin:6px; min-width:130px; }
        .stat b { display:block; font-size:26px; color:#7ee787; }
        .stat span { font-size:11px; color:#8b949e; }
        .bar { background:#21262d; border-radius:8px; height:24px; margin:16px 0; overflow:hidden; width:100%; max-width:600px; }
        .bar > div { background:linear-gradient(90deg,#1f6feb,#388bfd); height:100%; width:0; transition:width .5s; }
        #log { background:#161b22; border:1px solid #30363d; border-radius:8px; padding:14px; white-space:pre-wrap; max-height:40vh; overflow-y:auto; font-size:13px; }
        .status { color:#7ee787; }
        .warning { color:#d29922; }
        .error { color:#f85149; }
    </style>
</head>
<body>
    <h1>MLP Data Generation Progress</h1>
    <div class="stat"><b id="attention">0</b><span>Attention (target: 200)</span></div>
    <div class="stat"><b id="action_evaluator">0</b><span>Action Evaluator (target: 200)</span></div>
    <div class="stat"><b id="world_model">0</b><span>World Model (target: 100)</span></div>
    <div class="stat"><b id="total">0</b><span>Total (target: 500)</span></div>
    <div class="bar"><div id="barFill"></div></div>
    <div id="log"></div>
    <script>
        const targets = {attention: 200, action_evaluator: 200, world_model: 100, total: 500};
        function update() {
            fetch('/api/progress').then(r => r.json()).then(p => {
                for (const [k, v] of Object.entries(p)) {
                    const el = document.getElementById(k);
                    if (el) el.textContent = v;
                }
                const pct = Math.min(100, (p.total / targets.total) * 100);
                document.getElementById('barFill').style.width = pct + '%';
                const logEl = document.getElementById('log');
                logEl.textContent = `attention: ${p.attention}/200  action_evaluator: ${p.action_evaluator}/200  world_model: ${p.world_model}/100  total: ${p.total}/500`;
            }).catch(e => console.error(e));
        }
        setInterval(update, 1000);
        update();
    </script>
</body>
</html>
"""

def write_progress(task, completed):
    DATA_DIR.mkdir(exist_ok=True)
    entry = {"task": task, "completed": completed, "ts": time.time()}
    with open(LOG_FILE, "a") as f:
        f.write(json.dumps(entry) + "\n")

def run_server():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    httpd = HTTPServer(("", PORT), ProgressHandler)
    print(f"Progress UI on http://localhost:{PORT}")
    httpd.serve_forever()

if __name__ == "__main__":
    run_server()