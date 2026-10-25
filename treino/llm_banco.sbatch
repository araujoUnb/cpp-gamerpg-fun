#!/bin/bash
# Writes the narrator's phrase bank with the model (tools/narrator_bank.cpp).
# The bank goes to treino/banco_<job>.json; copy it to data/narrator/banco.json.
#   sbatch --export=ALL,MODELO=8B treino/llm_banco.sbatch
#SBATCH -J llm_banco
#SBATCH -p batch
#SBATCH -c 16
#SBATCH --mem=24G
#SBATCH -t 02:00:00
#SBATCH -o treino/llm_banco_%j.log
set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DGAMERPG_BUILD_TESTS=OFF > /dev/null
cmake --build build-release -j 16 --target gamerpg_narrator_bank > /dev/null
LLM=$HOME/Daniel/Pessoal/llm
PORT=$((18000 + SLURM_JOB_ID % 1000))
model=$(ls "$LLM"/modelos/Qwen3-${MODELO:-8B}-*.gguf | head -1)
"$LLM/llama.cpp/build/bin/llama-server" -m "$model" -t 16 -c 8192 --host 127.0.0.1 --port $PORT --jinja \
    > treino/llm_server_banco_$SLURM_JOB_ID.log 2>&1 &
pid=$!
for i in $(seq 1 120); do curl -sf http://127.0.0.1:$PORT/health > /dev/null && break; sleep 2; done
./build-release/tools/gamerpg_narrator_bank --url http://127.0.0.1:$PORT --data data --saida treino/banco_$SLURM_JOB_ID.json
kill $pid; wait $pid 2>/dev/null || true
