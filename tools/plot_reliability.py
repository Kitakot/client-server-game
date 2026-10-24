"""Метрики серий ПР №3 и графики по docs/reliability_samples.csv."""
import pandas as pd
import matplotlib.pyplot as plt

SERIES = ["baseline", "loss_5", "loss_10", "loss_20", "delay_100_loss_5", "jitter_loss_10"]
# Потери задаются в Clumsy в каждую сторону: и для приказа, и для ACK.
LOSS = {"baseline": 0, "loss_5": 5, "loss_10": 10, "loss_20": 20}

df = pd.read_csv("docs/reliability_samples.csv", sep=";")

rows = []
for name in SERIES:
    s = df[df.experiment_id == name]
    delivered = s[s.status == "delivered"]
    rows.append({
        "Серия": name,
        "Отправлено": len(s),
        "Доставлено с 1-й попытки": int((delivered.attempts == 1).sum()),
        "Retransmit total": int((s.attempts - 1).sum()),
        "Failed": int((s.status == "failed").sum()),
        "Avg attempts": s.attempts.mean(),
        "Avg RTO, мс": s.rto_ms.mean(),
        "Time-to-ACK, мс": delivered.time_to_ack_ms.mean(),
        "Final RTO, мс": s.sort_values("finished_ms").rto_ms.iloc[-1] if len(s) else float("nan"),
    })
summary = pd.DataFrame(rows).set_index("Серия")
print(summary.round(2).to_markdown())

# 1. Среднее число попыток от процента потерь. Приказ доходит, только если
# дошли и он, и ACK: вероятность успеха попытки (1 - p)^2, ожидаемое число
# попыток без ограничения сверху — 1 / (1 - p)^2.
fig, ax = plt.subplots(figsize=(7, 4))
loss = [LOSS[n] for n in LOSS]
ax.plot(loss, [summary.loc[n, "Avg attempts"] for n in LOSS], marker="o", label="эксперимент")
theory_x = list(range(0, 21))
ax.plot(theory_x, [1 / (1 - p / 100) ** 2 for p in theory_x], linestyle="--", label="теория 1/(1-p)²")
ax.set_xlabel("потери в каждую сторону, %")
ax.set_ylabel("среднее число попыток")
ax.set_title("Число попыток от процента потерь")
ax.grid(alpha=0.3)
ax.legend()
fig.tight_layout()
fig.savefig("docs/graphs/attempts_vs_loss.png", dpi=150)

# 2. RTO во времени в серии с джиттером.
s = df[df.experiment_id == "jitter_loss_10"].sort_values("finished_ms")
fig, ax = plt.subplots(figsize=(9, 4))
ax.plot(s.finished_ms / 1000, s.rto_ms, marker=".", label="RTO")
ax.plot(s.finished_ms / 1000, s.srtt_ms, linestyle="--", label="SRTT")
acked = s[s.status == "delivered"]
ax.scatter(acked.finished_ms / 1000, acked.time_to_ack_ms, s=12, color="tab:green", label="время до ACK")
failed = s[s.status == "failed"]
ax.scatter(failed.finished_ms / 1000, [0] * len(failed), color="red", marker="x", label="failed")
ax.set_xlabel("время от начала серии, с")
ax.set_ylabel("мс")
ax.set_title("RTO во времени, jitter_loss_10")
ax.grid(alpha=0.3)
ax.legend()
fig.tight_layout()
fig.savefig("docs/graphs/rto_jitter_loss_10.png", dpi=150)