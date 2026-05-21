#!/usr/bin/env bash
set -euo pipefail

PROJECT=test_vco_duty_cycling_rms
SEL_H=sw/applications/test_vco_duty_cycling_rms/duty_sequence_select.h
WAVES=build/epfl_cheep_cheep_0.3.0/sim-verilator/logs/waves.fst

dataset=7

make clean all
make cheep-gen
make verilator-build

for seq in 1 2 3 4; do
  echo "=== Running sequence $seq ==="

  cat > "$SEL_H" <<EOF
#ifndef DUTY_SEQ_ID
#define DUTY_SEQ_ID $seq
#endif
EOF

  make app PROJECT="$PROJECT" BOOT_MODE=force
  make verilator-run BOOT_MODE=force

  python3 scripts/plotter/evaluation/VCO_duty_cycling/vcd_timing_rms.py \
    "$WAVES" \
    --rms-by-duty-phase \
    --start-ms 0 \
    --stop-ms 100 \
    --csv "sequence_${seq}_${dataset}.csv" \
    --raw-csv "sequence_${seq}_raw_${dataset}.csv"

done

make verilator-waves