#!/bin/bash
#SBATCH -J gm_calibra
#SBATCH -p batch
#SBATCH -c 16
#SBATCH --mem=4G
#SBATCH -o treino/calibra_%j.log
cd "$SLURM_SUBMIT_DIR"
mkdir -p treino/dificuldades_$SLURM_JOB_ID
./build-release/tools/gamerpg_gm_train --calibrate --eval-seeds 1000 --threads 16 --out treino/dificuldades_$SLURM_JOB_ID
