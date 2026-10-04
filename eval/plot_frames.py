#!/usr/bin/env python3
"""Plot a per-frame CSV written by `tuner_engine_eval --frames-dir`.

  plot_frames.py FRAMES.csv [OUT.png]

Top: cents error of the pitched frames with a +-5 cent band.
Bottom: frame RMS (dB, left axis) and pitch confidence (right axis).
Needs matplotlib.
"""
import csv
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def main(argv):
    if len(argv) not in (2, 3):
        print(__doc__)
        return 2
    src = argv[1]
    out = argv[2] if len(argv) == 3 else src.rsplit(".", 1)[0] + ".png"

    with open(src) as f:
        rows = list(csv.DictReader(f))
    t = [float(r["time_ms"]) / 1000.0 for r in rows]
    rms = [float(r["rms_db"]) for r in rows]
    conf = [float(r["confidence"]) for r in rows]
    pitched = [r["has_pitch"] == "1" for r in rows]
    err = [float(r["error_cents"]) if p else float("nan") for r, p in zip(rows, pitched)]

    fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True, figsize=(10, 6),
                                   gridspec_kw={"height_ratios": [2, 1]})
    ax1.axhspan(-5, 5, color="tab:green", alpha=0.15, label="±5 c")
    ax1.plot(t, err, ".-", ms=3, lw=0.8, color="tab:blue")
    ax1.set_ylabel("error (cents)")
    lim = max([abs(e) for e in err if e == e] + [10.0])
    ax1.set_ylim(-min(lim * 1.1, 60), min(lim * 1.1, 60))
    ax1.legend(loc="upper right")
    ax1.set_title(src.rsplit("/", 1)[-1])

    ax2.plot(t, rms, color="tab:gray", lw=1.0)
    ax2.set_ylabel("RMS (dB)")
    ax2.set_xlabel("time (s)")
    ax3 = ax2.twinx()
    ax3.plot(t, conf, color="tab:orange", lw=1.0)
    ax3.set_ylabel("confidence")
    ax3.set_ylim(0, 1.05)

    fig.tight_layout()
    fig.savefig(out, dpi=120)
    print("wrote " + out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
