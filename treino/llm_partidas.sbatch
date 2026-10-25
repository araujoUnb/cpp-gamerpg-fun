#!/bin/bash
# Plays whole auto battles narrated by each model (tools/narrator_play.cpp):
# scenarios x class pairs x seeds, measuring time per round and repetition.
# Results in treino/partidas_<model>_<job>.jsonl, summary in the log.
#   sbatch --export=ALL,MODELOS="8B 1.7B 0.6B",SEMENTES=2 treino/llm_partidas.sbatch
# RESERVA=banco lets the phrase bank cover the rounds the model misses.
# ANTERIORES=N overrides how many previous narrations the model sees (mestre.json).
#SBATCH -J llm_partidas
#SBATCH -p batch
#SBATCH -c 16
#SBATCH --mem=24G
#SBATCH -t 04:00:00
#SBATCH -o treino/llm_partidas_%j.log
set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DGAMERPG_BUILD_TESTS=OFF > /dev/null
cmake --build build-release -j 16 --target gamerpg_narrator_play > /dev/null
LLM=$HOME/Daniel/Pessoal/llm
PORT=$((18000 + SLURM_JOB_ID % 1000))
for m in ${MODELOS:-8B}; do
  model=$(ls "$LLM"/modelos/Qwen3-$m-*.gguf | head -1)
  "$LLM/llama.cpp/build/bin/llama-server" -m "$model" -t 16 -c 8192 --host 127.0.0.1 --port $PORT --jinja \
      > treino/llm_server_${m}_$SLURM_JOB_ID.log 2>&1 &
  pid=$!
  for i in $(seq 1 120); do curl -sf http://127.0.0.1:$PORT/health > /dev/null && break; sleep 2; done
  echo "== Qwen3-$m"
  ./build-release/tools/gamerpg_narrator_play --narrador http://127.0.0.1:$PORT --data data --sementes ${SEMENTES:-2} \
      ${ANTERIORES:+--anteriores $ANTERIORES} ${RESERVA:+--reserva $RESERVA} --saida treino/partidas_${m}_$SLURM_JOB_ID.jsonl || echo "falhou: $m"
  kill $pid; wait $pid 2>/dev/null || true
done
