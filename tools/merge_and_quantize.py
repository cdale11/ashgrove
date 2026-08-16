#!/usr/bin/env python3
"""Post-training pipeline: merge LoRA -> full HF model -> f16 GGUF -> Q4_K_M.

Takes the freshly trained (mixed intent+consolidation) LoRA adapter and produces
the runtime gguf that `build/ashgrove_server` loads
(data/qwen2.5-0.5b-ashgrove-q4_k_m.gguf).

Steps:
  1. PeftModel.merge_and_unload()  -> full HF model  (data/lora-merged/)
  2. convert_hf_to_gguf.py         -> f16 GGUF       (data/lora-merged-f16.gguf)
  3. llama-quantize Q4_K_M         -> runtime model  (data/qwen2.5-0.5b-ashgrove-q4_k_m.gguf)

Usage:
    conda run -n ashgrove python3 tools/merge_and_quantize.py \
        --adapter data/lora-adapter-mixed \
        --out data/qwen2.5-0.5b-ashgrove-q4_k_m.gguf

The new model overwrites the runtime gguf in place. Back up the old gguf first if
you want to keep it. Stop the server before swapping the model.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LLAMA_CPP = os.environ.get("LLAMA_CPP_DIR", "/home/umang/llama.cpp")
CONVERT_HF = os.path.join(LLAMA_CPP, "convert_hf_to_gguf.py")
QUANTIZE = os.path.join(LLAMA_CPP, "build", "bin", "llama-quantize")


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="Qwen/Qwen2.5-0.5B-Instruct")
    ap.add_argument("--adapter", required=True,
                    help="Trained LoRA adapter dir (e.g. data/lora-adapter-mixed)")
    ap.add_argument("--merge-dir", default="data/lora-merged",
                    help="Where to write the merged full model")
    ap.add_argument("--gguf-f16", default="data/lora-merged-f16.gguf")
    ap.add_argument("--out", default="data/qwen2.5-0.5b-ashgrove-q4_k_m.gguf")
    ap.add_argument("--threads", type=int, default=4)
    args = ap.parse_args()

    if not os.path.exists(args.adapter):
        print("adapter dir not found: %s" % args.adapter)
        return 1
    if not os.path.exists(CONVERT_HF):
        print("convert_hf_to_gguf.py not found under %s" % LLAMA_CPP)
        return 1
    if not os.path.exists(QUANTIZE):
        print("llama-quantize not found under %s" % LLAMA_CPP)
        return 1

    # 1. Merge adapter into base.
    shutil.rmtree(args.merge_dir, ignore_errors=True)
    os.makedirs(args.merge_dir, exist_ok=True)
    run([sys.executable, "-c", (
        "from transformers import AutoModelForCausalLM, AutoTokenizer; "
        "from peft import PeftModel; "
        "import sys; "
        "base=AutoModelForCausalLM.from_pretrained('%s', torch_dtype='float32'); "
        "tok=AutoTokenizer.from_pretrained('%s'); "
        "m=PeftModel.from_pretrained(base,'%s'); "
        "m=m.merge_and_unload(); "
        "m.save_pretrained('%s'); "
        "tok.save_pretrained('%s')"
    ) % (args.base, args.base, args.adapter, args.merge_dir, args.merge_dir)])

    # 2. HF model -> f16 GGUF.
    run([sys.executable, CONVERT_HF, args.merge_dir,
         "--outfile", args.gguf_f16, "--outtype", "f16"])

    # 3. Quantize to Q4_K_M (matches current runtime model).
    run([QUANTIZE, args.gguf_f16, args.out, "Q4_K_M"])
    size = os.path.getsize(args.out)
    print("WROTE %s (%.1f MiB)" % (args.out, size / 1048576), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())