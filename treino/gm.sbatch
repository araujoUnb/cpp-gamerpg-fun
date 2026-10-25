#!/bin/bash
#SBATCH -J gm_train
#SBATCH -p batch
#SBATCH -c 16
#SBATCH --mem=4G
#SBATCH -o treino/gm_%j.log
cd "$SLURM_SUBMIT_DIR"
./build-release/tools/gamerpg_gm_train --generations 300 --population 64 --seeds 100 --eval-seeds 1000 --threads 16 --out treino/tactical_$SLURM_JOB_ID.json
