#!/bin/bash
# Runs the narrator test situations on each candidate model, one llama-server
# at a time on a local port inside the job, through tools/narrator_eval.cpp
# (the same request and checks as the game). Answers go to
# treino/narrador_<model>_<job>.jsonl.
#   sbatch --export=ALL,MODELOS=8B,AMOSTRAS=3 treino/llm_avalia.sbatch
#SBATCH -J llm_avalia
#SBATCH -p batch
#SBATCH -c 16
#SBATCH --mem=24G
#SBATCH -t 01:30:00
#SBATCH -o treino/llm_avalia_%j.log
set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DGAMERPG_BUILD_TESTS=OFF > /dev/null
cmake --build build-release -j 16 --target gamerpg_narrator_eval > /dev/null
LLM=$HOME/Daniel/Pessoal/llm
PORT=$((18000 + SLURM_JOB_ID % 1000))
for m in ${MODELOS:-8B}; do
  model=$(ls "$LLM"/modelos/Qwen3-$m-*.gguf | head -1)
  "$LLM/llama.cpp/build/bin/llama-server" -m "$model" -t 16 -c 8192 \
      --host 127.0.0.1 --port $PORT --jinja > treino/llm_server_${m}_$SLURM_JOB_ID.log 2>&1 &
  pid=$!
  for i in $(seq 1 120); do curl -sf http://127.0.0.1:$PORT/health > /dev/null && break; sleep 2; done
  echo "== Qwen3-$m"
  ./build-release/tools/gamerpg_narrator_eval --url http://127.0.0.1:$PORT --data data \
      --situacoes tools/narrator/situacoes_teste.json --amostras ${AMOSTRAS:-3} \
      --saida treino/narrador_${m}_$SLURM_JOB_ID.jsonl || echo "falhou: $m"
  kill $pid; wait $pid 2>/dev/null || true
done
