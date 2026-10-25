#!/bin/bash
# Runs the narrator test situations on each candidate model, one llama-server
# at a time on a local port inside the job. Answers go to treino/narrador_<model>_<job>.jsonl.
#SBATCH -J llm_avalia
#SBATCH -p batch
#SBATCH -c 16
#SBATCH --mem=24G
#SBATCH -o treino/llm_avalia_%j.log
set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
LLM=$HOME/Daniel/Pessoal/llm
PORT=$((18000 + SLURM_JOB_ID % 1000))
for m in ${MODELOS:-4B 8B 14B}; do
  "$LLM/llama.cpp/build/bin/llama-server" -m "$LLM/modelos/Qwen3-$m-Q4_K_M.gguf" -t 16 -c 8192 \
      --host 127.0.0.1 --port $PORT --jinja > treino/llm_server_${m}_$SLURM_JOB_ID.log 2>&1 &
  pid=$!
  for i in $(seq 1 120); do curl -sf http://127.0.0.1:$PORT/health > /dev/null && break; sleep 2; done
  echo "== Qwen3-$m"
  python3 tools/narrator/avalia.py http://127.0.0.1:$PORT . tools/narrator/situacoes_teste.json \
      treino/narrador_${m}_$SLURM_JOB_ID.jsonl || echo "falhou: $m"
  kill $pid; wait $pid 2>/dev/null || true
done
