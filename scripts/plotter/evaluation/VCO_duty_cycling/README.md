# Duty-Cycling RMS Experiment

This directory contains the scripts used to quantify how VCO duty cycling affects
GSR conductance reconstruction error in Verilator simulations.

All commands below are meant to be run from the repository root. The evaluation
folder is:

```text
scripts/plotter/evaluation/VCO_duty_cycling/
```

The goal is to compare RMS error across VCO duty cycles while controlling for two
major sources of bias:

- the order in which duty cycles are tested, because the conductance trace changes
  with time;
- the selected conductance dataset, because different trace segments have
  different intrinsic difficulty and baseline RMS.

The experiment therefore has three layers:

1. Run each conductance dataset with several duty-cycle orderings.
2. Cross-validate the four orderings within one conductance dataset.
3. Normalize and compare cross-validated results across conductance datasets.

## Files

### Measurement and Per-Sequence Analysis

- `vcd_timing_rms.py`

  Parses `waves.fst` or `.vcd` waveforms. It has three relevant modes:

  - default timing-based RMS mode, kept for backward compatibility;
  - `--measure-duty-cycles`, which measures EN on/off timing;
  - `--rms-by-duty-phase`, which decodes firmware debug markers and computes RMS
    per duty-cycle phase.

- `run_duty_rms_sequences.sh`

  Builds and runs the firmware for four duty-cycle orderings, then runs
  `vcd_timing_rms.py` on each produced waveform.

- `summarize_duty_rms_sequences.py`

  Aggregates the four per-sequence CSV files for one dataset into one
  cross-validation CSV.

### Cross-Dataset Analysis

- `plot_duty_rms_across_ranges.py`

  Reads all cross-validation CSVs in nested `csv/data_*` folders, normalizes each
  dataset by its best duty-cycle RMS, writes normalized CSVs, and generates
  comparison plots.

## Firmware Debug Protocol

The firmware app is:

```text
sw/applications/test_vco_duty_cycling_rms/main.c
```

The RMS script expects `debug_section` writes to use this 32-bit layout:

```text
[tag:8 bits][payload:24 bits]
```

The tags are:

```text
0xA0 -> phase marker, payload = duty_cycle_code
0x00 -> valid conductance sample, payload = G_nS
0xAF -> experiment done
```

Only `0x00` sample markers are used as measured conductance values. If a read
after a configuration change does not produce a valid sample and therefore does
not write debug, it is automatically ignored.

Optionally, pass this to ignore the first debug-written sample in each phase:

```bash
--discard-first-per-phase 1
```

## Duty-Cycle Codes

This branch uses inverse duty-cycle codes:

```text
duty_cycle_code = 8 -> 12.5%
duty_cycle_code = 4 -> 25%
duty_cycle_code = 2 -> 50%
duty_cycle_code = 1 -> 100%
```

The four orderings used for cross-validation are:

```c
static const uint8_t duty_seq_1[] = {8U, 4U, 2U, 1U};
static const uint8_t duty_seq_2[] = {1U, 2U, 4U, 8U};
static const uint8_t duty_seq_3[] = {4U, 1U, 8U, 2U};
static const uint8_t duty_seq_4[] = {2U, 8U, 1U, 4U};
```

The purpose of these orderings is to reduce bias from conductance drift over time.

## Ground Truth Definition

The RMS script compares each debug-written conductance sample against the
conductance model value at the beginning of the integration window.

For duty-cycled modes:

```text
duty_cycle_code != 1:
    G_ref = rskin.G_nS at the latest VCOp.EN rising edge before the debug sample
```

For 100% duty cycle, `EN` does not toggle, so the script uses the VCO refresh
edge:

```text
duty_cycle_code == 1:
    G_ref = rskin.G_nS at the latest VCOp.REFRESH rising edge before the debug sample
```

The measured value is:

```text
G_meas = payload of the latest 0x00 debug sample
```

The sample error is:

```text
error_nS = G_meas - G_ref
```

The per-duty RMS is:

```text
RMS_nS = sqrt(mean(error_nS^2))
```

## Running One Sequence Manually

After building and running the simulation, analyze one waveform with:

```bash
python3 scripts/plotter/evaluation/VCO_duty_cycling/vcd_timing_rms.py \
  build/epfl_cheep_cheep_0.3.0/sim-verilator/logs/waves.fst \
  --rms-by-duty-phase \
  --start-ms 0 \
  --stop-ms 100 \
  --csv sequence_1_7.csv \
  --raw-csv sequence_1_raw_7.csv
```

Bare CSV filenames are written under:

```text
scripts/plotter/evaluation/VCO_duty_cycling/csv/
```

The summary CSV contains one row per duty cycle (all samples with that duty cycle). The raw CSV contains one row per sample.

## Running Four Sequences for One Dataset

Use:

```bash
scripts/plotter/evaluation/VCO_duty_cycling/run_duty_rms_sequences.sh
```

Before running, set the `data_set` variable in that script to match the dataset
identifier. For example:

```bash
data_set=7
```

The script:

1. regenerates and rebuilds the simulation;
2. writes `duty_sequence_select.h` with `DUTY_SEQ_ID` from 1 to 4;
3. builds the firmware;
4. runs Verilator;
5. analyzes `waves.fst`;
6. writes sequence CSVs;
7. writes one cross-validation CSV.

For data_set identifier `7`, the expected outputs are:

```text
scripts/plotter/evaluation/VCO_duty_cycling/csv/sequence_1_7.csv
scripts/plotter/evaluation/VCO_duty_cycling/csv/sequence_1_raw_7.csv
scripts/plotter/evaluation/VCO_duty_cycling/csv/sequence_2_7.csv
scripts/plotter/evaluation/VCO_duty_cycling/csv/sequence_2_raw_7.csv
scripts/plotter/evaluation/VCO_duty_cycling/csv/sequence_3_7.csv
scripts/plotter/evaluation/VCO_duty_cycling/csv/sequence_3_raw_7.csv
scripts/plotter/evaluation/VCO_duty_cycling/csv/sequence_4_7.csv
scripts/plotter/evaluation/VCO_duty_cycling/csv/sequence_4_raw_7.csv
scripts/plotter/evaluation/VCO_duty_cycling/csv/duty_rms_cross_validation_7.csv
```

If you organize results into folders, place the cross-validation file like this:

```text
scripts/plotter/evaluation/VCO_duty_cycling/csv/data_600_700/duty_rms_cross_validation_7.csv
```

## Conductance Datasets

A conductance dataset is one selected contiguous line range from the conductance
trace file. The line range is selected in:

```text
hw/ip/analog_subsystem/resistor.sv
```

The relevant parameters are ```line_start``` and ```line_end``` defined here:

```systemverilog
module resistor #(
    string FILE_NAME      = "../../../hw/ip/analog_subsystem/conductance.txt",
    real   CHANGE_RATE_HZ = 200_00,
    int    line_start     = 500_000,
    int    line_end       = 600_000
)
```
For example:

```systemverilog
int line_start = 100_000,
int line_end   = 200_000
```

corresponds to a dataset that should be stored under:

```text
scripts/plotter/evaluation/VCO_duty_cycling/csv/data_100_200/
```

Each folder under `scripts/plotter/evaluation/VCO_duty_cycling/csv/` represents one conductance dataset:

```text
data_100_200/
data_200_300/
data_300_400/
data_400_500/
data_500_600/
```

Each folder should contain one cross-validation CSV:

```text
duty_rms_cross_validation_<id>.csv
```

The dataset labels are inferred from folder names. The plotting script searches
recursively for:

```text
scripts/plotter/evaluation/VCO_duty_cycling/csv/**/duty_rms_cross_validation_*.csv
```

## Cross-Dataset Normalization

Different conductance datasets can have different intrinsic RMS levels. To
compare the impact of duty cycling, `plot_duty_rms_across_ranges.py` removes a
per-dataset offset:

```text
baseline_rms_nS = min(mean_rms_nS across duty cycles in one dataset)
relative_rms_nS = mean_rms_nS - baseline_rms_nS
```

This answers:

```text
How much extra RMS does this duty cycle have compared with the best duty cycle
on the same dataset?
```

It also computes:

```text
relative_rms_pct = 100 * relative_rms_nS / baseline_rms_nS
```

## Plotting Across Datasets

```bash
python3 scripts/plotter/evaluation/VCO_duty_cycling/plot_duty_rms_across_ranges.py
```

Default inputs:

```text
scripts/plotter/evaluation/VCO_duty_cycling/csv/**/duty_rms_cross_validation_*.csv
```

Generated CSVs:

```text
scripts/plotter/evaluation/VCO_duty_cycling/csv/duty_rms_normalized_by_range.csv
scripts/plotter/evaluation/VCO_duty_cycling/csv/duty_rms_relative_summary.csv
```

Generated figures:

```text
scripts/plotter/evaluation/VCO_duty_cycling/figs/duty_rms_relative_lines.png
scripts/plotter/evaluation/VCO_duty_cycling/figs/duty_rms_relative_boxplot.png
scripts/plotter/evaluation/VCO_duty_cycling/figs/duty_rms_relative_heatmap.png
```

The most useful report table is:

```text
duty_percent
n_ranges
median_relative_rms_nS
mean_relative_rms_nS
stdev_relative_rms_nS
q1_relative_rms_nS
q3_relative_rms_nS
```

The median and interquartile range are useful because one difficult dataset
can skew the mean.

## Backward-Compatible Timing Mode

The original timing-offset mode remains available:

```bash
python3 scripts/plotter/evaluation/VCO_duty_cycling/vcd_timing_rms.py \
  build/epfl_cheep_cheep_0.3.0/sim-verilator/logs/waves.fst \
  --start-ms 8 \
  --stop-ms 20 \
  --com-offset-ns 200000
```

In this mode:

```text
G_ref = rskin.G_nS at EN rising edge + ref_offset_ns
G_com = debug_section at EN falling edge + com_offset_ns
```

This mode is kept for debugging timing compensation and should not be confused
with the marker-based cross-validation RMS experiment.

## Reproducibility Checklist

For each conductance dataset:

1. Update `FILE_NAME`, `line_start`, and/or `line_end` in
   `hw/ip/analog_subsystem/resistor.sv`.

2. Set `data_set=<id>` in `run_duty_rms_sequences.sh`.
3. Run:

   ```bash
   scripts/plotter/evaluation/VCO_duty_cycling/run_duty_rms_sequences.sh
   ```

4. Move the produced cross-validation CSV into a folder named after the dataset,
   for example:

   ```text
   scripts/plotter/evaluation/VCO_duty_cycling/csv/data_600_700/duty_rms_cross_validation_7.csv
   ```

5. After all datasets are collected, run:

   ```bash
   python3 scripts/plotter/evaluation/VCO_duty_cycling/plot_duty_rms_across_ranges.py
   ```

6. Use the normalized CSV and figures for conclusions about duty-cycle impact.
