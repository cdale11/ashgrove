import argparse
import json
import time
import math

from datasets import load_dataset
from transformers import (
    AutoTokenizer,
    AutoModelForCausalLM,
    DataCollatorForSeq2Seq,
    Trainer,
    TrainingArguments,
    set_seed,
)

set_seed(42)

PROMPT_TMPL = (
    "Below is a command in Ashgrove Valley. "
    "Classify it into the correct intent JSON.\n\n"
    "### Instruction:\n{instruction}\n\n"
    "### Input:\n{input}\n\n### Response:\n{output}"
)


def build_prompt(example):
    return PROMPT_TMPL.format(
        instruction=example["instruction"],
        input=example["input"],
        output=example["output"],
    )


def tokenize(example, tokenizer):
    text = build_prompt(example)
    result = tokenizer(
        text, truncation=True, max_length=args.max_len, padding=False
    )
    return {
        "input_ids": result["input_ids"],
        "attention_mask": result["attention_mask"],
        "labels": list(result["input_ids"]),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="Qwen/Qwen2.5-0.5B-Instruct")
    parser.add_argument("--train", default="/tmp/opencode/train.json")
    parser.add_argument("--val", default="/tmp/opencode/val.json")
    parser.add_argument("--out", default="/home/umang/ashgrove/data/lora-adapter")
    parser.add_argument("--epochs", type=int, default=3)
    parser.add_argument("--lr", type=float, default=2e-4)
    parser.add_argument("--r", type=int, default=16)
    parser.add_argument("--alpha", type=int, default=32)
    parser.add_argument("--max-len", type=int, default=256)
    parser.add_argument("--batch", type=int, default=4)
    parser.add_argument("--accum", type=int, default=4)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--log-file", default="/tmp/opencode/lora_train.log")
    args = parser.parse_args()
    globals()["args"] = args

    tokenizer = AutoTokenizer.from_pretrained(args.base)
    if tokenizer.pad_token is None:
        tokenizer.pad_token = tokenizer.eos_token

    model = AutoModelForCausalLM.from_pretrained(args.base, torch_dtype="float32")

    from peft import LoraConfig, get_peft_model

    lora = LoraConfig(
        r=args.r,
        lora_alpha=args.alpha,
        target_modules=["q_proj", "k_proj", "v_proj", "o_proj",
                        "gate_proj", "up_proj", "down_proj"],
        lora_dropout=0.05,
        task_type="CAUSAL_LM",
        bias="none",
    )
    model = get_peft_model(model, lora)
    model.print_trainable_parameters()

    train_ds = load_dataset("json", data_files=args.train)["train"]
    val_ds = load_dataset("json", data_files=args.val)["train"]
    train_ds = train_ds.map(
        lambda e: tokenize(e, tokenizer), remove_columns=train_ds.column_names
    )
    val_ds = val_ds.map(
        lambda e: tokenize(e, tokenizer), remove_columns=val_ds.column_names
    )

    steps = math.ceil(len(train_ds) / (args.batch * args.accum))
    logging_steps = max(1, steps // 100)
    log_file = open(args.log_file, "w", buffering=1)
    log_file.write("epochs=%d lr=%g r=%d alpha=%d train=%d val=%d\n" % (
        args.epochs, args.lr, args.r, args.alpha, len(train_ds), len(val_ds)))
    log_file.flush()

    training_args = TrainingArguments(
        output_dir=args.out,
        num_train_epochs=args.epochs,
        learning_rate=args.lr,
        per_device_train_batch_size=args.batch,
        per_device_eval_batch_size=args.batch,
        gradient_accumulation_steps=args.accum,
        eval_strategy="steps",
        eval_steps=max(40, steps // 6),
        logging_steps=logging_steps,
        logging_first_step=True,
        save_strategy="steps",
        save_steps=steps,
        save_total_limit=1,
        report_to=[],
        remove_unused_columns=False,
        dataloader_num_workers=0,
        fp16=False,
        bf16=False,
        optim="adamw_torch",
        lr_scheduler_type="cosine",
        warmup_steps=math.ceil(steps * 0.03),
        weight_decay=0.01,
        load_best_model_at_end=True,
        metric_for_best_model="eval_loss",
        seed=42,
    )

    trainer = Trainer(
        model=model,
        args=training_args,
        train_dataset=train_ds,
        eval_dataset=val_ds,
        data_collator=DataCollatorForSeq2Seq(tokenizer, padding=True),
    )
    trainer.add_callback(ProgressCallback(log_file))
    trainer.train()
    model.save_pretrained(args.out)
    log_file.write("TRAINING_COMPLETE saved=%s\n" % args.out)
    log_file.flush()


from transformers import TrainerCallback


class ProgressCallback(TrainerCallback):
    def __init__(self, log_file):
        self.log = log_file
        self.t0 = time.time()

    def on_log(self, args, state, control, logs=None, **kwargs):
        if state.global_step == 0:
            return
        step = state.global_step
        total = state.max_steps
        pct = 100.0 * step / total if total else 0.0
        elapsed = time.time() - self.t0
        loss = logs.get("loss")
        evl = logs.get("eval_loss")
        msg = "[%5d/%5d] %5.1f%% %6.0fs loss=%s%s" % (
            step, total, pct, elapsed,
            "%.4f" % loss if loss is not None else "?",
            "  eval_loss=%.4f" % evl if evl is not None else "")
        self.log.write(msg + "\n")
        self.log.flush()


if __name__ == "__main__":
    main()
