#!/usr/bin/env python3
"""Regenerates Source/Ksp/RecallPlan.h and Source/Ksp/Globals.h from a KSCON checkout
(https://github.com/canadianblaken/keystep-con, MIT): web/src/1c-bulk.js and web/src/1b-globals.js.

    python Tools/gen_from_kscon.py <path to keystep-con>
"""
import json
import sys
from pathlib import Path


def js_array(path: Path):
    s = path.read_text(encoding="utf-8")
    return json.loads(s[s.index("["):s.rindex("]") + 1])


def recall_plan(groups) -> str:
    fmt = lambda v: "kI" if v == "I" else str(v)
    rows = []
    for lo, hi, item, dims, cnt, params in groups:
        d = "{ " + ", ".join("{ " + ", ".join(fmt(x) for x in dd) + " }" for dd in dims) + " }"
        rows.append(f"        {{ {lo}, {hi}, 0x{item:02X}, {d}, {cnt}, {{ {', '.join(str(p) for p in params)} }} }},")
    return "\n".join(rows)


def globals_rows(gl) -> str:
    return "\n".join(f'    {{ {x["id"]}, {json.dumps(x["g"])}, {json.dumps(x["n"])}, {x["d"]} }},' for x in gl)


def replace_table(header: Path, start_marker: str, end_marker: str, rows: str) -> None:
    s = header.read_text(encoding="utf-8")
    a = s.index(start_marker) + len(start_marker)
    b = s.index(end_marker, a)
    header.write_text(s[:a] + "\n" + rows + "\n" + s[b:], encoding="utf-8")


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    kscon = Path(sys.argv[1])
    src = Path(__file__).resolve().parent.parent / "Source" / "Ksp"
    replace_table(src / "RecallPlan.h", "static const std::vector<Group> g {", "    };",
                  recall_plan(js_array(kscon / "web/src/1c-bulk.js")))
    replace_table(src / "Globals.h", "inline constexpr GlobalSetting kGlobalSettings[] {", "};",
                  globals_rows(js_array(kscon / "web/src/1b-globals.js")))
    print("RecallPlan.h and Globals.h updated")


if __name__ == "__main__":
    main()
