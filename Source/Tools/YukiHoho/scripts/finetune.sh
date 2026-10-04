#!/bin/bash
# finetune.sh — Offline fine-tune pass for Yuki
# Called by WorkboardManager on startup when training data exists.
# Usage: finetune.sh <training_data> [base_model_path]
#
# Input can be:
#   - .txt file: ChatML-formatted text, fed directly to llama-finetune
#   - .jsonl file: Converted to ChatML text first (legacy support)
#
# Output: Source/Tools/YukiHoho/models/yuki_finetuned.gguf

set -euo pipefail

TRAINING_DATA="$1"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
YUKI_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$YUKI_DIR/build"
OUTPUT_MODEL="$YUKI_DIR/models/yuki_finetuned.gguf"

# Base model — second arg or autodetect smallest available
BASE_MODEL="${2:-}"
if [ -z "$BASE_MODEL" ]; then
    for candidate in \
        "$YUKI_DIR/models/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf" \
        "$YUKI_DIR/models/deepcoder-1.5b.gguf"; do
        if [ -f "$candidate" ]; then
            BASE_MODEL="$candidate"
            break
        fi
    done
fi

FINETUNE_BIN="$BUILD_DIR/bin/llama-finetune"

# --- Validate ---
if [ -z "$TRAINING_DATA" ] || [ ! -f "$TRAINING_DATA" ]; then
    echo "Usage: finetune.sh <training_data.txt|.jsonl> [base_model.gguf]" >&2
    exit 1
fi
if [ -z "$BASE_MODEL" ] || [ ! -f "$BASE_MODEL" ]; then
    echo "ERROR: No base model found. Pass as second arg or place in models/" >&2
    exit 1
fi
if [ ! -x "$FINETUNE_BIN" ]; then
    echo "ERROR: llama-finetune not found at $FINETUNE_BIN" >&2
    echo "Build it: cd $BUILD_DIR && make llama-finetune -j4" >&2
    exit 1
fi

# --- Determine input format ---
TRAINING_TEXT=""
CLEANUP_TEMP=false

if [[ "$TRAINING_DATA" == *.txt ]]; then
    # Already ChatML — use directly
    TRAINING_TEXT="$TRAINING_DATA"
    echo "Input is ChatML text — no conversion needed"
elif [[ "$TRAINING_DATA" == *.jsonl ]]; then
    # Legacy JSONL — convert to ChatML
    TRAINING_TEXT="$YUKI_DIR/training/.finetune_input.txt"
    CLEANUP_TEMP=true

    echo "Converting JSONL to ChatML text..."
    python3 -c "
import json, sys

out = []
for line in open(sys.argv[1]):
    line = line.strip()
    if not line:
        continue
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        continue
    messages = obj.get('messages', [])
    if not messages and 'prompt' in obj:
        messages = [
            {'role': 'user', 'content': obj['prompt']},
            {'role': 'assistant', 'content': obj.get('answer', obj.get('response', ''))}
        ]
    for msg in messages:
        role = msg.get('role', 'user')
        content = msg.get('content', '')
        out.append(f'<|im_start|>{role}\n{content}<|im_end|>')
    out.append('')

with open(sys.argv[2], 'w') as f:
    f.write('\n'.join(out))
print(f'Converted {len(out)} ChatML segments')
" "$TRAINING_DATA" "$TRAINING_TEXT"
else
    echo "ERROR: Unsupported format. Use .txt (ChatML) or .jsonl" >&2
    exit 1
fi

if [ ! -s "$TRAINING_TEXT" ]; then
    echo "ERROR: Training data is empty" >&2
    exit 1
fi

INPUT_SIZE=$(stat -c%s "$TRAINING_TEXT" 2>/dev/null || echo 0)
echo ""
echo "=== Yuki Fine-Tune ==="
echo "Input:      $TRAINING_TEXT ($INPUT_SIZE bytes)"
echo "Base model: $BASE_MODEL"
echo "Output:     $OUTPUT_MODEL"
echo ""

# --- Run llama-finetune ---
echo "Running llama-finetune (AdamW)..."
export LD_LIBRARY_PATH="$BUILD_DIR/bin:${LD_LIBRARY_PATH:-}"

"$FINETUNE_BIN" \
    --model "$BASE_MODEL" \
    --file "$TRAINING_TEXT" \
    --output "$OUTPUT_MODEL" \
    --epochs 2 \
    --learning-rate 1e-5 \
    --optimizer adamw \
    --val-split 0.05 \
    --batch-size 256 \
    --ctx-size 512 \
    --threads "$(nproc)"

FINETUNE_EXIT=$?

# Clean up temp file if we created one
if [ "$CLEANUP_TEMP" = true ]; then
    rm -f "$TRAINING_TEXT"
fi

if [ $FINETUNE_EXIT -ne 0 ]; then
    echo "ERROR: llama-finetune failed (exit $FINETUNE_EXIT)" >&2
    exit $FINETUNE_EXIT
fi

if [ ! -f "$OUTPUT_MODEL" ]; then
    echo "ERROR: Output model not created" >&2
    exit 1
fi

OUTPUT_SIZE=$(stat -c%s "$OUTPUT_MODEL" 2>/dev/null || echo 0)
echo ""
echo "=== Fine-tune complete ==="
echo "Output: $OUTPUT_MODEL ($OUTPUT_SIZE bytes)"
echo "Yuki will load this on next startup."
