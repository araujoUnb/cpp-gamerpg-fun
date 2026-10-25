#!/bin/bash
# Serves the narrator model on lab10 for playing. The game reaches it through
# an ssh tunnel:  ssh -N -L 8080:127.0.0.1:8080 lab10
# then:           ./build/cli/gamerpg_cli --narrador http://127.0.0.1:8080
# Model:          sbatch --export=ALL,MODELO=8B treino/llm_servidor.sbatch
#SBATCH -J llm_servidor
#SBATCH -p batch
#SBATCH -c 16
#SBATCH --mem=24G
#SBATCH -t 08:00:00
#SBATCH -o treino/llm_servidor_%j.log
set -euo pipefail
LLM=$HOME/Daniel/Pessoal/llm
exec "$LLM/llama.cpp/build/bin/llama-server" -m "$LLM/modelos/Qwen3-${MODELO:-8B}-Q4_K_M.gguf" -t 16 -c 8192 \
    --host 127.0.0.1 --port ${PORTA:-8080} --jinja
