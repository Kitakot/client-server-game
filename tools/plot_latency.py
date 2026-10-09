"""Считает метрики серий и строит графики по docs/latency_samples.csv."""
import pandas as pd
import matplotlib.pyplot as plt

SERIES = ["baseline", "delay_50", "delay_100", "jitter", "loss_5", "combined"]

df = pd.read_csv("docs/latency_samples.csv", sep=";")

rows = []
for name in SERIES:
    series = df[df.experiment_id == name]
    rtt = series.rtt_ms.dropna()
    timeouts = int((series.status == "timeout").sum())
    rows.append({
        "series": name,
        "sent": len(series),
        "received": len(rtt),
        "timeout": timeouts,
        "min": rtt.min(),
        "max": rtt.max(),
        "mean": rtt.mean(),
        "median": rtt.median(),
        "srtt": series.srtt_ms.dropna().iloc[-1] if len(rtt) else float("nan"),
        # J = 1/(n-1) * sum |RTT_i - RTT_{i-1}|
        "jitter": rtt.diff().abs().mean(),
        "loss_%": 100.0 * timeouts / len(series) if len(series) else float("nan"),
    })
summary = pd.DataFrame(rows).set_index("series")
print(summary.round(3).to_markdown())

# 1. RTT по измерениям, тайm-ауты отмечены крестиками на оси.
fig, axes = plt.subplots(3, 2, figsize=(12, 9), sharex=True)
for ax, name in zip(axes.flat, SERIES):
    series = df[df.experiment_id == name]
    ok = series[series.status == "received"]
    lost = series[series.status == "timeout"]
    ax.plot(ok["sample"], ok.rtt_ms, marker=".", label="RTT")
    ax.plot(ok["sample"], ok.srtt_ms, linestyle="--", label="SRTT")
    ax.scatter(lost["sample"], [0] * len(lost), color="red", marker="x", label="timeout")
    ax.set_title(name)
    ax.set_ylabel("мс")
    ax.grid(alpha=0.3)
axes.flat[0].legend()
fig.supxlabel("номер измерения")
fig.tight_layout()
fig.savefig("docs/graphs/rtt_timeline.png", dpi=150)

# 2. Средний RTT, SRTT и потери по сериям.
fig, (left, right) = plt.subplots(1, 2, figsize=(12, 4))
summary[["mean", "srtt"]].plot.bar(ax=left, rot=30)
left.set_ylabel("мс")
left.set_title("Mean RTT и SRTT")
summary["loss_%"].plot.bar(ax=right, rot=30, color="tab:red")
right.set_ylabel("%")
right.set_title("Loss rate")
fig.tight_layout()
fig.savefig("docs/graphs/summary_bars.png", dpi=150)

# 3. Распределение RTT.
names = ["baseline", "jitter", "combined"]
fig, ax = plt.subplots(figsize=(7, 4))
ax.boxplot([df[df.experiment_id == n].rtt_ms.dropna() for n in names], labels=names)
ax.set_ylabel("RTT, мс")
ax.set_title("Распределение RTT")
ax.grid(alpha=0.3)
fig.tight_layout()
fig.savefig("docs/graphs/rtt_boxplot.png", dpi=150)