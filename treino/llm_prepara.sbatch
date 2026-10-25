#!/bin/bash
# Prepares the local LLM for the narrator on lab10: builds llama.cpp at a fixed
# tag, downloads the candidate models and measures their speed on the CPU.
#   sbatch --export=ALL,MODELOS="0.6B 1.7B" treino/llm_prepara.sbatch
#SBATCH -J llm_prepara
#SBATCH -p batch
#SBATCH -c 16
#SBATCH --mem=24G
#SBATCH -t 02:00:00
#SBATCH -o treino/llm_prepara_%j.log
set -euo pipefail
LLM=$HOME/Daniel/Pessoal/llm
TAG=v0.7.0
mkdir -p "$LLM/modelos"
if [ ! -d "$LLM/llama.cpp" ]; then
  git clone --depth 1 --branch "$TAG" https://github.com/ggml-org/llama.cpp "$LLM/llama.cpp"
fi
cmake -S "$LLM/llama.cpp" -B "$LLM/llama.cpp/build" -DCMAKE_BUILD_TYPE=Release -DGGML_NATIVE=ON -DLLAMA_CURL=OFF
cmake --build "$LLM/llama.cpp/build" -j 16 --target llama-server llama-bench llama-cli
# The small models are only published in Q8_0.
quant() { case "$1" in 0.6B|1.7B) echo Q8_0 ;; *) echo Q4_K_M ;; esac; }
for m in ${MODELOS:-4B 8B 14B}; do
  f="$LLM/modelos/Qwen3-$m-$(quant $m).gguf"
  [ -s "$f" ] || curl -sL --fail -o "$f" "https://huggingface.co/Qwen/Qwen3-$m-GGUF/resolve/main/Qwen3-$m-$(quant $m).gguf"
  ls -la "$f"
done
# Prompt reading (pp) and generation (tg) speed, tokens per second.
for m in ${MODELOS:-4B 8B 14B}; do
  "$LLM/llama.cpp/build/bin/llama-bench" -m "$LLM/modelos/Qwen3-$m-$(quant $m).gguf" -t 16 -p 512 -n 128 -r 2
done
