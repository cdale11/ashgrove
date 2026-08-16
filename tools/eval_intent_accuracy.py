#!/usr/bin/env python3
"""Measure intent-parsing ACCURACY (action + parameters) against golden cases.

Unlike eval_intents.py (which only checks for a "useful" response line), this
harness reads the ACTUAL parsed intent that the server recorded in
data/cmdlog.jsonl and compares action + parameters to the expected intent.

Use it to prove additive training did not regress intent parsing:
  1. BEFORE training (old model): record accuracy = BASELINE.
  2. AFTER training  (new model): rerun, compare. Accuracy must not drop.

Usage:
    tools/eval_intent_accuracy.py [--eval data/eval_set.jsonl]
                                  [--base http://localhost:8080]
                                  [--cmdlog data/cmdlog.jsonl] [--timeout 60]
"""
import argparse
import json
import os
import sys
import urllib.request


def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument("--eval", default="data/eval_set.jsonl")
    ap.add_argument("--base", default="http://localhost:8080")
    ap.add_argument("--cmdlog", default="data/cmdlog.jsonl")
    ap.add_argument("--timeout", type=int, default=60)
    return ap.parse_args()


def post(base, path, payload, timeout):
    req = urllib.request.Request(
        base + path, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


def main():
    args = parse_args()
    cases = []
    with open(args.eval) as f:
        for line in f:
            line = line.strip()
            if line:
                cases.append(json.loads(line))

    player_id = post(args.base, "/join", {"name": "eval_acc"}, args.timeout)["player_id"]
    print("eval player_id=%d cases=%d" % (player_id, len(cases)), flush=True)

    results = []
    for c in cases:
        text = c["text"]
        expected_action = c.get("expected_action")
        expected_params = c.get("expected_params", {})
        try:
            post(args.base, "/cmd", {"cmd": text, "player_id": player_id}, args.timeout)
        except Exception as e:
            results.append({"id": c["id"], "ok": False, "err": str(e),
                            "text": text, "exp": expected_action})
            continue

        # Read the newest cmdlog entry for this player + raw text.
        intent = None
        if os.path.exists(args.cmdlog):
            with open(args.cmdlog) as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        rec = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if rec.get("player_id") == player_id and rec.get("raw") == text:
                        intent = rec.get("intent")
        if intent is None:
            results.append({"id": c["id"], "ok": False,
                            "err": "no cmdlog entry", "text": text,
                            "exp": expected_action})
            continue

        action = intent.get("action")
        params = intent.get("parameters", {})
        action_ok = (action == expected_action)
        # parameters match exactly for non-empty expected; else require empty.
        if expected_params:
            params_ok = (params == expected_params)
        else:
            params_ok = (params == {} or params is None)
        ok = action_ok and params_ok
        results.append({"id": c["id"], "ok": ok, "action_ok": action_ok,
                        "params_ok": params_ok, "text": text,
                        "exp": expected_action, "got": action,
                        "exp_params": expected_params, "got_params": params})

    ok = sum(1 for r in results if r["ok"])
    print("intent accuracy: %d/%d (%.1f%%)" % (ok, len(results), 100.0 * ok / len(results)))
    print("action match:    %d/%d" % (
        sum(1 for r in results if r.get("action_ok")), len(results)))
    print("params match:    %d/%d" % (
        sum(1 for r in results if r.get("params_ok")), len(results)))
    print("--- failures ---")
    for r in results:
        if not r["ok"]:
            print("  [%s] exp action=%s params=%s | got action=%s params=%s"
                  % (r["id"], r["exp"], r.get("exp_params"), r.get("got"),
                     r.get("got_params")))
    return 0 if ok == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())