# Copyright 2026 EPFL contributors
# SPDX-License-Identifier: Apache-2.0
#
# File: vcd_gsr_timing_rms.py
# Description: Extract GSR reference/computed samples from VCD/FST waveforms and
#              compute RMS timing error around VCOp enable edges.

import argparse
import csv
import math
import subprocess
from dataclasses import dataclass
from typing import Dict, Iterable, Iterator, List, Optional, Tuple


DEFAULT_EN_SIGNAL = "TOP.tb_system.u_cheep_top.u_analog_subsystem.u_VCOp.EN"
DEFAULT_G_REF_SIGNAL = "TOP.tb_system.u_cheep_top.u_analog_subsystem.rskin.G_nS"
DEFAULT_G_COM_SIGNAL = (
    "TOP.tb_system.u_cheep_top.u_core_v_mini_mcu.memory_subsystem_i."
    "ram1_i.tc_ram_i.debug_section"
)


TIME_SCALE_TO_NS = {
    "s": 1.0e9,
    "ms": 1.0e6,
    "us": 1.0e3,
    "ns": 1.0,
    "ps": 1.0e-3,
    "fs": 1.0e-6,
}


@dataclass
class SignalTrace:
    path: str
    values: List[Tuple[int, object]]


@dataclass
class Sample:
    edge_time_ns: float
    fall_time_ns: float
    g_ref_time_ns: float
    g_com_time_ns: float
    g_ref_nS: float
    g_com_nS: int

    @property
    def error_nS(self) -> float:
        return float(self.g_com_nS) - self.g_ref_nS


@dataclass
class DutyWindow:
    rise_time_ns: float
    fall_time_ns: float
    next_rise_time_ns: float

    @property
    def on_time_ns(self) -> float:
        return self.fall_time_ns - self.rise_time_ns

    @property
    def off_time_ns(self) -> float:
        return self.next_rise_time_ns - self.fall_time_ns

    @property
    def period_ns(self) -> float:
        return self.next_rise_time_ns - self.rise_time_ns


@dataclass
class Stats:
    count: int
    mean: float
    stdev: float
    min: float
    max: float


def waveform_lines(wave_path: str) -> Iterator[str]:
    if wave_path.endswith(".fst"):
        proc = subprocess.Popen(
            ["fst2vcd", wave_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        assert proc.stdout is not None
        try:
            for line in proc.stdout:
                yield line
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    proc.kill()
            stderr = proc.stderr.read() if proc.stderr is not None else ""
            ret = proc.wait()
            if ret not in (0, -15):
                raise RuntimeError(
                    "fst2vcd failed. Make sure GTKWave/fst2vcd is installed "
                    f"and the FST file is valid.\n{stderr.strip()}"
                )
        return

    with open(wave_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            yield line


def parse_waveform(
    wave_path: str,
    wanted_paths: Iterable[str],
    parse_until_ns: Optional[float] = None,
) -> Tuple[float, Dict[str, SignalTrace]]:
    wanted = set(wanted_paths)
    id_to_path: Dict[str, str] = {}
    scope: List[str] = []
    timescale_ns = 1.0
    current_time = 0
    traces = {path: SignalTrace(path=path, values=[]) for path in wanted}

    in_header = True
    for raw_line in waveform_lines(wave_path):
        line = raw_line.strip()
        if not line:
            continue

        if in_header:
            if line.startswith("$timescale"):
                tokens = line.split()
                if len(tokens) >= 3:
                    timescale_ns = float(tokens[1]) * TIME_SCALE_TO_NS[tokens[2]]
                continue

            if line.startswith("$scope"):
                tokens = line.split()
                if len(tokens) >= 3:
                    scope.append(tokens[2])
                continue

            if line.startswith("$upscope"):
                if scope:
                    scope.pop()
                continue

            if line.startswith("$var"):
                tokens = line.split()
                if len(tokens) >= 5:
                    code = tokens[3]
                    name = "".join(tokens[4:-1])
                    path = ".".join(scope + [name])
                    if path in wanted:
                        id_to_path[code] = path
                continue

            if line.startswith("$enddefinitions"):
                in_header = False
                continue

            continue

        if line[0] == "#":
            current_time = int(line[1:])
            if parse_until_ns is not None and (current_time * timescale_ns) > parse_until_ns:
                break
            continue

        if line[0] == "$":
            continue

        code, value = parse_value_change(line)
        path = id_to_path.get(code)
        if path is not None:
            traces[path].values.append((current_time, value))

    return timescale_ns, traces


def parse_value_change(line: str) -> Tuple[str, object]:
    prefix = line[0]

    if prefix in "01xXzZ":
        return line[1:], prefix

    if prefix in "bB":
        value_text, code = line[1:].split(None, 1)
        if any(ch in value_text for ch in "xXzZ"):
            return code, None
        return code, int(value_text, 2)

    if prefix in "rR":
        value_text, code = line[1:].split(None, 1)
        return code, float(value_text)

    raise ValueError(f"Unsupported VCD value change: {line}")


def value_at(trace: SignalTrace, tick: int) -> Optional[object]:
    values = trace.values
    if not values:
        return None

    lo = 0
    hi = len(values)
    while lo < hi:
        mid = (lo + hi) // 2
        if values[mid][0] <= tick:
            lo = mid + 1
        else:
            hi = mid

    if lo == 0:
        return None
    return values[lo - 1][1]


def is_logic_high(value: object) -> bool:
    return value == "1" or value == 1


def rising_edges(trace: SignalTrace) -> List[int]:
    edges: List[int] = []
    prev = "x"
    for tick, value in trace.values:
        if is_logic_high(value) and not is_logic_high(prev):
            edges.append(tick)
        prev = value
    return edges


def high_windows(trace: SignalTrace) -> List[Tuple[int, int]]:
    windows: List[Tuple[int, int]] = []
    prev = "x"
    rise_tick: Optional[int] = None

    for tick, value in trace.values:
        if is_logic_high(value) and not is_logic_high(prev):
            rise_tick = tick
        elif not is_logic_high(value) and is_logic_high(prev) and rise_tick is not None:
            windows.append((rise_tick, tick))
            rise_tick = None
        prev = value

    return windows


def duty_windows(
    trace: SignalTrace,
    timescale_ns: float,
    start_ns: Optional[float],
    stop_ns: Optional[float],
    max_samples: Optional[int],
) -> List[DutyWindow]:
    raw_windows = high_windows(trace)
    windows: List[DutyWindow] = []

    for idx in range(len(raw_windows) - 1):
        rise_tick, fall_tick = raw_windows[idx]
        next_rise_tick, _ = raw_windows[idx + 1]
        rise_ns = rise_tick * timescale_ns

        if start_ns is not None and rise_ns < start_ns:
            continue
        if stop_ns is not None and rise_ns > stop_ns:
            continue

        windows.append(
            DutyWindow(
                rise_time_ns=rise_ns,
                fall_time_ns=fall_tick * timescale_ns,
                next_rise_time_ns=next_rise_tick * timescale_ns,
            )
        )

        if max_samples is not None and len(windows) >= max_samples:
            break

    return windows


def summarize(values: List[float]) -> Stats:
    if not values:
        return Stats(count=0, mean=math.nan, stdev=math.nan, min=math.nan, max=math.nan)

    mean = sum(values) / len(values)
    if len(values) > 1:
        var = sum((value - mean) ** 2 for value in values) / (len(values) - 1)
        stdev = math.sqrt(var)
    else:
        stdev = 0.0

    return Stats(
        count=len(values),
        mean=mean,
        stdev=stdev,
        min=min(values),
        max=max(values),
    )


def expected_duty_cycles(
    refresh_rate_hz: Optional[float],
    duty_code: Optional[int],
    duty_percent: Optional[float],
    sys_fclk_hz: float,
    sim_accel_ratio: float,
) -> Tuple[Optional[float], Optional[float], Optional[float]]:
    if refresh_rate_hz is None:
        return None, None, None

    refresh_cycles = sys_fclk_hz / (sim_accel_ratio * refresh_rate_hz)

    if duty_code is not None:
        on_cycles = math.floor(refresh_cycles * duty_code / 255.0)
    elif duty_percent is not None:
        on_cycles = math.floor(refresh_cycles * duty_percent / 100.0)
    else:
        return refresh_cycles, None, None

    off_cycles = refresh_cycles - on_cycles
    return refresh_cycles, on_cycles, off_cycles


def print_stats_line(label: str, stats: Stats, expected: Optional[float]) -> None:
    if expected is None or math.isnan(expected):
        print(
            f"{label}: count={stats.count}, mean={stats.mean:.3f}, "
            f"stdev={stats.stdev:.3f}, min={stats.min:.3f}, max={stats.max:.3f}"
        )
        return

    err = stats.mean - expected
    err_pct = (100.0 * err / expected) if expected != 0.0 else math.nan
    print(
        f"{label}: expected={expected:.3f}, mean={stats.mean:.3f}, "
        f"error={err:+.3f} ({err_pct:+.3f}%), stdev={stats.stdev:.3f}, "
        f"min={stats.min:.3f}, max={stats.max:.3f}, count={stats.count}"
    )


def print_duty_windows(
    windows: List[DutyWindow],
    sys_fclk_hz: float,
    expected_on_cycles: Optional[float],
    expected_off_cycles: Optional[float],
    expected_period_cycles: Optional[float],
) -> None:
    cycle_per_ns = sys_fclk_hz / 1.0e9
    on_cycles = [window.on_time_ns * cycle_per_ns for window in windows]
    off_cycles = [window.off_time_ns * cycle_per_ns for window in windows]
    period_cycles = [window.period_ns * cycle_per_ns for window in windows]

    print("rise_time_ms,fall_time_ms,next_rise_time_ms,on_cycles,off_cycles,period_cycles")
    for window, on_cc, off_cc, period_cc in zip(windows, on_cycles, off_cycles, period_cycles):
        print(
            f"{window.rise_time_ns / 1.0e6:.6f},"
            f"{window.fall_time_ns / 1.0e6:.6f},"
            f"{window.next_rise_time_ns / 1.0e6:.6f},"
            f"{on_cc:.3f},"
            f"{off_cc:.3f},"
            f"{period_cc:.3f}"
        )

    print_stats_line("ON cycles", summarize(on_cycles), expected_on_cycles)
    print_stats_line("OFF cycles", summarize(off_cycles), expected_off_cycles)
    print_stats_line("PERIOD cycles", summarize(period_cycles), expected_period_cycles)


def write_duty_csv(path: str, windows: List[DutyWindow], sys_fclk_hz: float) -> None:
    cycle_per_ns = sys_fclk_hz / 1.0e9
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "rise_time_ns",
                "fall_time_ns",
                "next_rise_time_ns",
                "on_time_ns",
                "off_time_ns",
                "period_ns",
                "on_cycles",
                "off_cycles",
                "period_cycles",
            ]
        )
        for window in windows:
            writer.writerow(
                [
                    f"{window.rise_time_ns:.3f}",
                    f"{window.fall_time_ns:.3f}",
                    f"{window.next_rise_time_ns:.3f}",
                    f"{window.on_time_ns:.3f}",
                    f"{window.off_time_ns:.3f}",
                    f"{window.period_ns:.3f}",
                    f"{window.on_time_ns * cycle_per_ns:.6f}",
                    f"{window.off_time_ns * cycle_per_ns:.6f}",
                    f"{window.period_ns * cycle_per_ns:.6f}",
                ]
            )


def ticks_from_ns(ns: float, timescale_ns: float) -> int:
    return int(round(ns / timescale_ns))


def collect_samples(
    traces: Dict[str, SignalTrace],
    timescale_ns: float,
    en_signal: str,
    g_ref_signal: str,
    g_com_signal: str,
    ref_offset_ns: float,
    com_offset_ns: float,
    start_ns: Optional[float],
    stop_ns: Optional[float],
    max_samples: Optional[int],
) -> List[Sample]:
    en = traces[en_signal]
    g_ref = traces[g_ref_signal]
    g_com = traces[g_com_signal]
    ref_offset_ticks = ticks_from_ns(ref_offset_ns, timescale_ns)
    com_offset_ticks = ticks_from_ns(com_offset_ns, timescale_ns)
    start_tick = None if start_ns is None else ticks_from_ns(start_ns, timescale_ns)
    stop_tick = None if stop_ns is None else ticks_from_ns(stop_ns, timescale_ns)
    samples: List[Sample] = []

    for edge_tick, fall_tick in high_windows(en):
        if start_tick is not None and edge_tick < start_tick:
            continue
        if stop_tick is not None and edge_tick > stop_tick:
            continue

        ref_tick = edge_tick + ref_offset_ticks
        com_tick = fall_tick + com_offset_ticks
        ref_value = value_at(g_ref, ref_tick)
        com_value = value_at(g_com, com_tick)

        if ref_value is None or com_value is None:
            continue

        samples.append(
            Sample(
                edge_time_ns=edge_tick * timescale_ns,
                fall_time_ns=fall_tick * timescale_ns,
                g_ref_time_ns=ref_tick * timescale_ns,
                g_com_time_ns=com_tick * timescale_ns,
                g_ref_nS=float(ref_value),
                g_com_nS=int(com_value),
            )
        )

        if max_samples is not None and len(samples) >= max_samples:
            break

    return samples


def rms_error(samples: List[Sample]) -> float:
    if not samples:
        return math.nan
    return math.sqrt(sum(sample.error_nS ** 2 for sample in samples) / len(samples))


def write_csv(path: str, samples: List[Sample]) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "rise_time_ns",
                "fall_time_ns",
                "g_ref_time_ns",
                "g_com_time_ns",
                "G_ref_nS",
                "G_com_nS",
                "error_nS",
            ]
        )
        for sample in samples:
            writer.writerow(
                [
                    f"{sample.edge_time_ns:.3f}",
                    f"{sample.fall_time_ns:.3f}",
                    f"{sample.g_ref_time_ns:.3f}",
                    f"{sample.g_com_time_ns:.3f}",
                    f"{sample.g_ref_nS:.6f}",
                    sample.g_com_nS,
                    f"{sample.error_nS:.6f}",
                ]
            )


def print_samples(samples: List[Sample]) -> None:
    print("rise_time_ms,fall_time_ms,G_ref_nS,G_com_nS,error_nS")
    for sample in samples:
        print(
            f"{sample.edge_time_ns / 1.0e6:.6f},"
            f"{sample.fall_time_ns / 1.0e6:.6f},"
            f"{sample.g_ref_nS:.3f},"
            f"{sample.g_com_nS},"
            f"{sample.error_nS:+.3f}"
        )
    print(f"RMS_nS={rms_error(samples):.6f}")


def parse_sweep(sweep: str) -> Tuple[float, float, float]:
    parts = [float(part) for part in sweep.split(":")]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("sweep must be start:stop:step in ns")
    if parts[2] == 0.0:
        raise argparse.ArgumentTypeError("sweep step must be non-zero")
    return parts[0], parts[1], parts[2]


def sweep_offsets(args, timescale_ns: float, traces: Dict[str, SignalTrace]) -> None:
    start, stop, step = args.sweep_com_offset_ns
    best = None
    offset = start

    print("com_offset_after_fall_ns,RMS_nS,num_samples")
    while (step > 0.0 and offset <= stop) or (step < 0.0 and offset >= stop):
        samples = collect_samples(
            traces=traces,
            timescale_ns=timescale_ns,
            en_signal=args.en_signal,
            g_ref_signal=args.g_ref_signal,
            g_com_signal=args.g_com_signal,
            ref_offset_ns=args.ref_offset_ns,
            com_offset_ns=offset,
            start_ns=args.start_ms * 1.0e6 if args.start_ms is not None else None,
            stop_ns=args.stop_ms * 1.0e6 if args.stop_ms is not None else None,
            max_samples=args.max_samples,
        )
        rms = rms_error(samples)
        print(f"{offset:.3f},{rms:.6f},{len(samples)}")
        if samples and (best is None or rms < best[1]):
            best = (offset, rms, len(samples))
        offset += step

    if best is not None:
        print(f"best_com_offset_ns={best[0]:.3f},best_RMS_nS={best[1]:.6f},num_samples={best[2]}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Measure timing-aligned GSR RMS error from VCD or FST waveforms."
    )
    parser.add_argument("waveform", help="Input VCD or FST file")
    parser.add_argument(
        "--measure-duty-cycles",
        action="store_true",
        help="Measure EN ON/OFF window lengths instead of GSR RMS alignment",
    )
    parser.add_argument("--en-signal", default=DEFAULT_EN_SIGNAL)
    parser.add_argument("--g-ref-signal", default=DEFAULT_G_REF_SIGNAL)
    parser.add_argument("--g-com-signal", default=DEFAULT_G_COM_SIGNAL)
    parser.add_argument("--ref-offset-ns", type=float, default=0.0)
    parser.add_argument(
        "--com-offset-ns",
        type=float,
        default=200000,
        help="Sample computed G at EN falling edge plus this offset",
    )
    parser.add_argument("--start-ms", type=float, default=8)
    parser.add_argument("--stop-ms", type=float, default=42)
    parser.add_argument("--max-samples", type=int, default=None)
    parser.add_argument(
        "--parse-extra-ms",
        type=float,
        default=5.0,
        help="Extra waveform time to parse after --stop-ms for the matching falling edge and computed sample",
    )
    parser.add_argument("--csv", default=None, help="Optional CSV output path")
    parser.add_argument(
        "--refresh-rate-hz",
        type=float,
        default=None,
        help="Expected VCO refresh rate for duty-cycle stats",
    )
    parser.add_argument(
        "--duty-code",
        type=int,
        default=None,
        help="Expected duty-cycle code D in [1,255]",
    )
    parser.add_argument(
        "--duty-percent",
        type=float,
        default=None,
        help="Expected duty cycle in percent; ignored when --duty-code is set",
    )
    parser.add_argument(
        "--sys-fclk-hz",
        type=float,
        default=10_000_000.0,
        help="System clock used to convert waveform time to cycles",
    )
    parser.add_argument(
        "--sim-accel-ratio",
        type=float,
        default=100.0,
        help="Simulation acceleration ratio used in freq_to_cc",
    )
    parser.add_argument(
        "--sweep-com-offset-ns",
        type=parse_sweep,
        default=None,
        metavar="START:STOP:STEP",
        help="Sweep computed-sample offset in ns and report RMS for each point",
    )
    args = parser.parse_args()

    wanted = [args.en_signal, args.g_ref_signal, args.g_com_signal]
    if args.measure_duty_cycles:
        wanted = [args.en_signal]
    parse_until_ns = None
    if args.stop_ms is not None:
        parse_until_ns = args.stop_ms * 1.0e6
        parse_until_ns += args.parse_extra_ms * 1.0e6
        parse_until_ns += max(args.ref_offset_ns, args.com_offset_ns, 0.0)
        if args.sweep_com_offset_ns is not None:
            parse_until_ns += max(args.sweep_com_offset_ns[0], args.sweep_com_offset_ns[1], 0.0)

    timescale_ns, traces = parse_waveform(args.waveform, wanted, parse_until_ns=parse_until_ns)

    missing = [path for path, trace in traces.items() if not trace.values]
    if missing:
        print("Missing or empty waveform signals:")
        for path in missing:
            print(f"  {path}")
        return 1

    if args.measure_duty_cycles:
        expected_period_cc, expected_on_cc, expected_off_cc = expected_duty_cycles(
            refresh_rate_hz=args.refresh_rate_hz,
            duty_code=args.duty_code,
            duty_percent=args.duty_percent,
            sys_fclk_hz=args.sys_fclk_hz,
            sim_accel_ratio=args.sim_accel_ratio,
        )
        windows = duty_windows(
            trace=traces[args.en_signal],
            timescale_ns=timescale_ns,
            start_ns=args.start_ms * 1.0e6 if args.start_ms is not None else None,
            stop_ns=args.stop_ms * 1.0e6 if args.stop_ms is not None else None,
            max_samples=args.max_samples,
        )
        print_duty_windows(
            windows=windows,
            sys_fclk_hz=args.sys_fclk_hz,
            expected_on_cycles=expected_on_cc,
            expected_off_cycles=expected_off_cc,
            expected_period_cycles=expected_period_cc,
        )
        if args.csv is not None:
            write_duty_csv(args.csv, windows, args.sys_fclk_hz)
        return 0

    if args.sweep_com_offset_ns is not None:
        sweep_offsets(args, timescale_ns, traces)
        return 0

    samples = collect_samples(
        traces=traces,
        timescale_ns=timescale_ns,
        en_signal=args.en_signal,
        g_ref_signal=args.g_ref_signal,
        g_com_signal=args.g_com_signal,
        ref_offset_ns=args.ref_offset_ns,
        com_offset_ns=args.com_offset_ns,
        start_ns=args.start_ms * 1.0e6 if args.start_ms is not None else None,
        stop_ns=args.stop_ms * 1.0e6 if args.stop_ms is not None else None,
        max_samples=args.max_samples,
    )

    print_samples(samples)
    if args.csv is not None:
        write_csv(args.csv, samples)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
