#!/usr/bin/env python3
# AIMer v3 comparison of N bench_full CSV outputs (1개=단일 표, 2개 이상=비교).
# 모든 빌드를 같은 지표 컬럼(사이클/회·시간/회·처리량·CV·min…max)으로 나란히 출력하고,
# 첫 번째 파일을 기준(baseline)으로 각 빌드의 speedup = 기준_median ÷ 대상_median 을 보여준다.
# 빌드가 3개 이상이면 마지막 두 빌드의 연속 비교(예: avx512 vs avx2)도 함께 표시.
#
# usage: compare.py <csv1> [csv2] [csv3 ...]
#   예) compare.py ref.csv avx2.csv avx512.csv
import os
import re
import sys
import unicodedata

COLS = ["build","variant","op","N","min","median","max","mean","std","cv","med_us","mean_us","ops"]
VAR_ORDER = ["AIMER-v3-128f","AIMER-v3-128s","AIMER-v3-192f","AIMER-v3-192s","AIMER-v3-256f","AIMER-v3-256s"]
OP_ORDER = ["keypair","sign","verify"]
OP_KO = {"keypair": "키생성", "sign": "서명", "verify": "검증"}

# ---- color (terminal only) ----
_USE = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None
def _c(code, s):
    return f"\033[{code}m{s}\033[0m" if _USE else s
def bold(s):  return _c("1", s)
def dim(s):   return _c("2", s)
def cyan(s):  return _c("36", s)
def green(s): return _c("32;1", s)
def yellow(s):return _c("33", s)

# ---- east-asian-width aware padding (한글=2칸, ANSI 무시) ----
_ANSI = re.compile(r"\033\[[0-9;]*m")
def dw(s):
    s = _ANSI.sub("", s)
    return sum(2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1 for ch in s)
def pad(s, w, right=False):
    g = w - dw(s)
    if g <= 0:
        return s
    return (" " * g + s) if right else (s + " " * g)

def load(path):
    rows, label = {}, None
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            p = line.split(",")
            if len(p) != len(COLS):
                continue
            r = dict(zip(COLS, p))
            label = r["build"]
            r["N"] = int(r["N"])
            for k in ["min","median","max","mean","std","cv","med_us","mean_us","ops"]:
                r[k] = float(r[k])
            rows[(r["variant"], r["op"])] = r
    return label, rows

def fmt_ops(v):
    return f"{v:,.0f}" if v >= 100 else f"{v:,.1f}"

# column widths (display cells)
W = dict(op=9, build=8, cyc=15, us=13, ops=12, cv=10, rng=27)

def cell_row(op_label, build, r):
    if not r:
        return "  " + pad(op_label, W["op"]) + pad(build, W["build"]) + dim("(측정값 없음)")
    cyc = pad(f"{r['median']:,.0f}", W["cyc"], right=True)
    us  = pad(f"{r['med_us']:,.1f} µs", W["us"], right=True)
    ops = pad(f"{fmt_ops(r['ops'])} /s", W["ops"], right=True)
    cv  = pad(f"{r['cv']:.2f} %", W["cv"], right=True)
    rng = f"{r['min']:,.0f} … {r['max']:,.0f}"
    return ("  " + pad(op_label, W["op"]) + pad(build, W["build"])
            + cyc + "  " + us + "  " + ops + "  " + cv + "   " + dim(rng))

def sub_row(r):
    # CSV에만 있던 값(평균 사이클 / 표준편차 / 평균 시간)을 보조줄로 표시
    if not r:
        return None
    indent = "  " + " " * W["op"] + " " * W["build"]
    s = (f"평균 {r['mean']:,.0f} cyc · σ {r['std']:,.0f} cyc · 평균시간 {r['mean_us']:,.1f} µs")
    return indent + dim(s)

def header():
    h = ("  " + pad("연산", W["op"]) + pad("빌드", W["build"])
         + pad("사이클/회", W["cyc"], right=True) + "  "
         + pad("시간/회", W["us"], right=True) + "  "
         + pad("처리량", W["ops"], right=True) + "  "
         + pad("CV(안정성)", W["cv"], right=True) + "   "
         + dim("min … max"))
    return bold(h)

def main():
    files = sys.argv[1:]
    if not files:
        print("usage: compare.py <csv1> [csv2] [csv3 ...]   (1개=단일 표, 2+개=비교)", file=sys.stderr)
        sys.exit(2)
    builds = [load(f) for f in files]          # [(label, rows), ...]
    labels = [b[0] for b in builds]
    base_label, base = builds[0]
    n = len(builds)

    variants = [v for v in VAR_ORDER if any(any(k[0] == v for k in d) for _, d in builds)]
    width = 2 + W["op"] + W["build"] + W["cyc"] + 2 + W["us"] + 2 + W["ops"] + 2 + W["cv"] + 3 + W["rng"]
    rule = "─" * width

    nval = "?"
    for _, d in builds:
        if d:
            nval = next(iter(d.values()))["N"]
            break

    print()
    if n == 1:
        print(bold(f"빌드 = {base_label}   ·   N = {nval}   (단일 빌드 — 비교 대상 없음)"))
    else:
        print(bold(f"기준 = {base_label}   ·   비교 = {', '.join(labels[1:])}   ·   N = {nval}"
                   f"   ·   speedup = 기준÷대상 (>1이면 대상이 빠름)"))
    print(dim("연산: keypair=키생성 · sign=서명 · verify=검증   |   메인줄=중앙값(median) · CV=변동계수(낮을수록 안정) · σ=표준편차"))

    summary = {}  # (variant, op) -> {label: speedup vs base}
    for v in variants:
        print()
        print(cyan(bold(f"┌── {v} " + "─" * max(0, width - len(v) - 6))))
        print(header())
        print(dim(rule))
        for op in OP_ORDER:
            base_r = base.get((v, op))
            for i, (lbl, d) in enumerate(builds):
                r = d.get((v, op))
                print(cell_row(op if i == 0 else "", lbl, r))
                if r:
                    print(sub_row(r))
            if n >= 2 and base_r and base_r["median"] > 0:
                parts, sm = [], {}
                for lbl, d in builds[1:]:
                    r = d.get((v, op))
                    if r and r["median"] > 0:
                        sp = base_r["median"] / r["median"]
                        sm[lbl] = sp
                        parts.append(f"{lbl} {sp:.2f}×")
                summary[(v, op)] = sm
                line = f"⮕  vs {base_label}:  " + "    ".join(parts)
                if n >= 3:
                    (pl, pd), (ll, ld) = builds[-2], builds[-1]
                    pr, lr = pd.get((v, op)), ld.get((v, op))
                    if pr and lr and lr["median"] > 0:
                        line += f"      [{ll} vs {pl}: {pr['median'] / lr['median']:.2f}×]"
                print("  " + pad("", W["op"]) + pad("", W["build"]) + green(line))
            print(dim("  " + "·" * (width - 2)))

    if n < 2:
        print()
        return

    # ---- 요약표: speedup vs 기준 ----
    print()
    print(bold(f"요약 — speedup vs {base_label} (배, >1이면 빠름)"))
    cmp_labels = labels[1:]
    vw, ow, cw = 15, 10, 11
    head = "  " + pad("변형", vw) + pad("연산", ow) + "".join(pad(l, cw, right=True) for l in cmp_labels)
    print(bold(head))
    print(dim("  " + "─" * (vw + ow + cw * len(cmp_labels))))
    for v in variants:
        for op in OP_ORDER:
            sm = summary.get((v, op), {})
            line = "  " + pad(v, vw) + pad(op, ow)
            for l in cmp_labels:
                sp = sm.get(l)
                cell = pad(f"{sp:.2f}×" if sp else "-", cw, right=True)
                line += green(cell) if sp else cell
            print(line)
    print()

if __name__ == "__main__":
    main()
