import re
from pathlib import Path


# ============================================================
# USER SETTINGS — CHANGE THESE ONLY
# ============================================================

_SCRIPT_DIR = Path(__file__).parent

INPUT_TXT_FILE = _SCRIPT_DIR / "sim.txt"

OUTPUT_FILE = _SCRIPT_DIR / "input_signal.h"

START_INDEX = 0        # inclusive (0-indexed; matches line_start=500000 in resistor.sv)
END_INDEX   = 1000         # exclusive (0-indexed; matches line_end=600000 in resistor.sv)

# Divide raw values by this factor before writing.
# conductance.txt is in pS; dividing by 1000 converts to nS,
# which keeps intermediate Q14 products within int32 range.
DIVISOR = 1

ARRAY_NAME = "signal"

C_TYPE = "static int"
# For embedded memory saving, you can use:
# C_TYPE = "const int"

LENGTH_NAME = "signal_length"

VALUES_PER_LINE = 10

ROUND_DECIMALS_TO_INT = True

INCLUDE_HEADER_GUARD = True
HEADER_GUARD_NAME = "INPUT_SIGNAL_H"

# ============================================================
# END USER SETTINGS
# ============================================================


def read_numbers(txt_path):
    text = Path(txt_path).read_text()

    # Extract integers or decimals from the file.
    values = re.findall(r"[-+]?\d*\.?\d+", text)

    if ROUND_DECIMALS_TO_INT:
        return [int(round(float(v))) for v in values]

    return [float(v) for v in values]


def format_c_array(values):
    signal_length = len(values)

    lines = []

    if INCLUDE_HEADER_GUARD:
        lines.append(f"#ifndef {HEADER_GUARD_NAME}")
        lines.append(f"#define {HEADER_GUARD_NAME}")
        lines.append("")

    lines.append(f"#define {LENGTH_NAME} {signal_length}")
    lines.append("")

    lines.append(f"{C_TYPE} {ARRAY_NAME}[{LENGTH_NAME}] =")
    lines.append("{")

    for i in range(0, signal_length, VALUES_PER_LINE):
        chunk = values[i:i + VALUES_PER_LINE]
        line = ", ".join(str(v) for v in chunk)

        if i + VALUES_PER_LINE < signal_length:
            line += ","

        lines.append("    " + line)

    lines.append("};")

    if INCLUDE_HEADER_GUARD:
        lines.append("")
        lines.append(f"#endif /* {HEADER_GUARD_NAME} */")

    lines.append("")
    return "\n".join(lines)


def main():
    numbers = read_numbers(INPUT_TXT_FILE)

    if START_INDEX < 0:
        raise ValueError("START_INDEX must be >= 0")

    if END_INDEX is not None and END_INDEX < START_INDEX:
        raise ValueError("END_INDEX must be greater than or equal to START_INDEX")

    selected = numbers[START_INDEX:END_INDEX]

    if not selected:
        raise ValueError("No samples selected. Check START_INDEX and END_INDEX.")

    if DIVISOR != 1:
        selected = [v // DIVISOR for v in selected]

    c_code = format_c_array(selected)

    Path(OUTPUT_FILE).write_text(c_code)

    print("Done.")
    print(f"Input file: {INPUT_TXT_FILE}")
    print(f"Output file: {OUTPUT_FILE}")
    print(f"Start index: {START_INDEX}")
    print(f"End index: {END_INDEX}")
    print(f"Number of samples written: {len(selected)}")


if __name__ == "__main__":
    main()
