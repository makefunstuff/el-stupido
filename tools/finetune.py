#!/usr/bin/env python3
"""Fine-tune a small model on el-stupido codebook specs using QLoRA.

Uses the TinyLLM approach: take a small pre-trained model (GPT-2 or similar),
fine-tune with LoRA on instruction-following data, merge, export to GGUF.

Usage:
    # Fine-tune GPT-2 (124M) — fits in ~4GB VRAM
    python3 tools/finetune.py \
        --base-model gpt2 \
        --train-data data/codebook_train.csv \
        --val-data data/codebook_val.csv \
        --output models/codebook-gpt2 \
        --epochs 3

    # Fine-tune a TinyLLM 30M model (if available)
    python3 tools/finetune.py \
        --base-model weiser/30M-0.4 \
        --train-data data/codebook_train.csv \
        --output models/codebook-30m

    # Convert to GGUF after fine-tuning
    python3 deps/llama.cpp/convert_hf_to_gguf.py models/codebook-gpt2 \
        --outfile models/codebook-gpt2.gguf
    deps/llama.cpp/build/bin/llama-quantize models/codebook-gpt2.gguf \
        models/codebook-gpt2-q4.gguf q4_0
"""

import argparse
import csv
import os

import torch
from datasets import Dataset
from transformers import (
    AutoModelForCausalLM,
    AutoTokenizer,
    TrainingArguments,
)
from peft import LoraConfig, get_peft_model, TaskType
from trl import SFTTrainer, SFTConfig


def load_csv_dataset(path):
    """Load CSV with instruction/input/output columns into HF Dataset."""
    rows = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            # format as instruction-following prompt
            text = (
                f"### Instruction:\n{row['instruction']}\n\n"
                f"### Input:\n{row['input']}\n\n"
                f"### Response:\n{row['output']}\n\n"
                f"### End"
            )
            rows.append({"text": text})
    return Dataset.from_list(rows)


def main():
    parser = argparse.ArgumentParser(description="Fine-tune model on codebook data")
    parser.add_argument(
        "--base-model", default="gpt2", help="HuggingFace model name or path"
    )
    parser.add_argument("--train-data", default="data/codebook_train.csv")
    parser.add_argument("--val-data", default="data/codebook_val.csv")
    parser.add_argument("--output", default="models/codebook-gpt2")
    parser.add_argument("--epochs", type=int, default=3)
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--lr", type=float, default=2e-4)
    parser.add_argument("--lora-r", type=int, default=8)
    parser.add_argument("--lora-alpha", type=int, default=16)
    parser.add_argument("--max-seq-len", type=int, default=512)
    parser.add_argument(
        "--no-merge",
        action="store_true",
        help="Don't merge LoRA weights (keep adapter separate)",
    )
    args = parser.parse_args()

    print(f"Loading base model: {args.base_model}")
    tokenizer = AutoTokenizer.from_pretrained(args.base_model, trust_remote_code=True)
    model = AutoModelForCausalLM.from_pretrained(
        args.base_model,
        torch_dtype=torch.bfloat16 if torch.cuda.is_bf16_supported() else torch.float16,
        device_map="auto",
        trust_remote_code=True,
    )

    # ensure pad token exists (GPT-2 doesn't have one)
    if tokenizer.pad_token is None:
        tokenizer.pad_token = tokenizer.eos_token
        model.config.pad_token_id = model.config.eos_token_id

    # LoRA config — following TinyLLM defaults
    lora_config = LoraConfig(
        task_type=TaskType.CAUSAL_LM,
        r=args.lora_r,
        lora_alpha=args.lora_alpha,
        lora_dropout=0.1,
        target_modules=["c_attn", "c_proj", "c_fc"],  # GPT-2 layer names
        bias="none",
    )

    model = get_peft_model(model, lora_config)
    trainable = sum(p.numel() for p in model.parameters() if p.requires_grad)
    total = sum(p.numel() for p in model.parameters())
    print(
        f"Trainable params: {trainable:,} / {total:,} ({100 * trainable / total:.1f}%)"
    )

    # load datasets
    print(f"Loading training data: {args.train_data}")
    train_ds = load_csv_dataset(args.train_data)
    val_ds = load_csv_dataset(args.val_data) if args.val_data else None
    print(f"  train: {len(train_ds)} examples, val: {len(val_ds) if val_ds else 0}")

    # training config
    training_args = SFTConfig(
        output_dir=args.output + "-checkpoints",
        num_train_epochs=args.epochs,
        per_device_train_batch_size=args.batch_size,
        per_device_eval_batch_size=args.batch_size,
        gradient_accumulation_steps=4,
        learning_rate=args.lr,
        warmup_ratio=0.1,
        weight_decay=0.01,
        logging_steps=10,
        eval_strategy="epoch" if val_ds else "no",
        save_strategy="epoch",
        save_total_limit=2,
        bf16=torch.cuda.is_bf16_supported(),
        fp16=not torch.cuda.is_bf16_supported(),
        max_length=args.max_seq_len,
        dataset_text_field="text",
        report_to="none",
    )

    trainer = SFTTrainer(
        model=model,
        processing_class=tokenizer,
        train_dataset=train_ds,
        eval_dataset=val_ds,
        args=training_args,
    )

    print(f"Training for {args.epochs} epochs...")
    trainer.train()

    # merge LoRA weights and save
    if not args.no_merge:
        print("Merging LoRA weights...")
        merged = model.merge_and_unload()
        merged.save_pretrained(args.output)
        tokenizer.save_pretrained(args.output)
        print(f"Merged model saved to: {args.output}")
    else:
        model.save_pretrained(args.output)
        tokenizer.save_pretrained(args.output)
        print(f"Adapter saved to: {args.output}")

    print("\nNext steps:")
    print(f"  1. Convert to GGUF:")
    print(
        f"     python3 deps/llama.cpp/convert_hf_to_gguf.py {args.output} --outfile {args.output}.gguf"
    )
    print(f"  2. Quantize (optional):")
    print(
        f"     deps/llama.cpp/build/bin/llama-quantize {args.output}.gguf {args.output}-q4.gguf q4_0"
    )
    print(f"  3. Test with esc:")
    print(
        f"     ./esc --llm {args.output}.gguf examples/llm_prompt.txt -o server --dump-expanded"
    )


if __name__ == "__main__":
    main()
