"""Графики ПР №5. Запуск: python tools/compare_replication.py [--show].

Зависимость: python -m pip install matplotlib
Учитываются исходящие сообщения send, включая повторы, без ACK и IP/UDP.
Каждый CSV — отдельный эксперимент; имя файла сохраняет варианты вроде loss5.
"""

import argparse
import csv
import math
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
FIELDS = {"mode", "event", "time_s", "client", "object_id", "kind", "bytes"}


def read_run(path):
    """Не выдавать незаконченные или повреждённые серии за полные измерения."""
    sends = []
    mode = None
    end = None
    started = False
    previous_time = 0.0
    with path.open(encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        if not FIELDS.issubset(reader.fieldnames or []):
            raise ValueError("неверный заголовок CSV")
        for line, row in enumerate(reader, 2):
            try:
                if end is not None:
                    raise ValueError("данные после завершения серии")
                if row["mode"] not in {"baseline", "quantized", "dirty", "optimized"}:
                    raise ValueError("неизвестный режим")
                if mode is not None and mode != row["mode"]:
                    raise ValueError("несколько режимов в одном файле")
                mode = row["mode"]
                time = float(row["time_s"])
                if not math.isfinite(time) or time < previous_time:
                    raise ValueError("некорректное или убывающее время")
                previous_time = time
                size = int(row["bytes"])
                if row["event"] == "start":
                    if started or sends or time != 0 or size != 0:
                        raise ValueError("некорректный маркер начала")
                    started = True
                elif row["event"] == "send":
                    client, kind = int(row["client"]), int(row["kind"])
                    if size <= 0 or client not in (0, 1) or kind not in (0, 1):
                        raise ValueError("некорректные параметры send")
                    sends.append((time, size, client, kind))
                elif row["event"] in ("end_ok", "end_unconfirmed"):
                    if time <= 0 or size != 0:
                        raise ValueError("некорректный маркер завершения")
                    end = (time, row["event"])
                else:
                    raise ValueError("неизвестное событие")
            except (ValueError, TypeError) as error:
                raise ValueError(f"строка {line}: {error}") from error
    if not sends or end is None:
        raise ValueError("нет отправок или маркера end_ok/end_unconfirmed; завершите эксперимент")
    if sends[-1][0] >= end[0]:
        raise ValueError("отправка вне интервала измерения")
    return {"name": path.stem, "mode": mode, "sends": sends,
            "duration": end[0], "status": end[1]}


def bin_rates(run, width):
    count = math.ceil(run["duration"] / width)
    totals = [0] * count
    for time, size, _, _ in run["sends"]:
        totals[min(int(time / width), count - 1)] += size
    edges = [min(i * width, run["duration"]) for i in range(count + 1)]
    rates = [size / (edges[i + 1] - edges[i]) / 1000
             for i, size in enumerate(totals)]
    return edges, rates


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, default=ROOT / "docs/replication")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/replication/graphs")
    parser.add_argument("--bin-seconds", type=float, default=1.0)
    parser.add_argument("--show", action="store_true", help="открыть окна графиков")
    args = parser.parse_args()
    if not math.isfinite(args.bin_seconds) or args.bin_seconds <= 0:
        parser.error("--bin-seconds должен быть положительным конечным числом")
    runs = []
    for path in sorted(args.input_dir.glob("*.csv")):
        try:
            runs.append(read_run(path))
        except (ValueError, OSError) as error:
            print(f"Пропуск {path.name}: {error}", file=sys.stderr)
    if not runs:
        parser.exit(1, "Нет завершённых серий для построения графиков.\n")
    order = {"baseline": 0, "quantized": 1, "dirty": 2, "optimized": 3}
    runs.sort(key=lambda run: (order[run["mode"]], run["name"]))
    try:
        import matplotlib
        if not args.show:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        parser.exit(1, "Установите зависимость: python -m pip install matplotlib\n")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    baseline = next((r for r in runs if r["name"] == "baseline" and
                     r["mode"] == "baseline" and r["status"] == "end_ok"), None)
    baseline_rate = (sum(s[1] for s in baseline["sends"]) / baseline["duration"]
                     if baseline else None)
    summary = []
    for run in runs:
        total = sum(s[1] for s in run["sends"])
        rate = total / run["duration"]
        saving = 100 * (1 - rate / baseline_rate) if baseline_rate else None
        summary.append({"series": run["name"], "mode": run["mode"],
                        "status": run["status"], "duration_s": run["duration"],
                        "packets": len(run["sends"]), "bytes": total,
                        "KB_per_s": rate / 1000,
                        "packets_per_s": len(run["sends"]) / run["duration"],
                        "saving_percent_vs_baseline": saving})
        print(f"{run['name']}: {rate / 1000:.3f} KB/s, "
              f"{len(run['sends'])} пакетов, {run['status']}"
              + (f", экономия {saving:.2f}%" if saving is not None else ""))
    with (args.output_dir / "summary.csv").open("w", encoding="utf-8-sig", newline="") as out:
        writer = csv.DictWriter(out, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)

    def save(fig, name):
        fig.text(0.5, 0.01, "Сервер → оба клиента; 1 KB = 1000 байт; без ACK и IP/UDP",
                 ha="center", fontsize=9)
        fig.tight_layout(rect=(0, 0.04, 1, 1))
        fig.savefig(args.output_dir / name, dpi=180)

    labels = [r["name"] + (" *" if r["status"] != "end_ok" else "") for r in runs]
    fig, axes = plt.subplots(2, 1, figsize=(11, 8), sharex=True)
    for run, label in zip(runs, labels):
        edges, rates = bin_rates(run, args.bin_seconds)
        axes[0].stairs(rates, edges, label=label)
        times, cumulative, total = [0.0], [0.0], 0
        for time, size, _, _ in run["sends"]:
            total += size
            times.append(time)
            cumulative.append(total / 1000)
        times.append(run["duration"])
        cumulative.append(total / 1000)
        axes[1].step(times, cumulative, where="post", label=label)
    axes[0].set(title=f"Трафик по интервалам {args.bin_seconds:g} с", ylabel="KB/s")
    axes[1].set(title="Накопленный объём отправленных данных", xlabel="Время, с", ylabel="KB")
    for ax in axes:
        ax.grid(alpha=0.25)
        ax.legend()
    save(fig, "traffic.png")

    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    for ax, key, title, unit in (
        (axes[0], "KB_per_s", "Средний исходящий трафик", "KB/s"),
        (axes[1], "packets_per_s", "Частота отправки сообщений", "пакетов/с"),
    ):
        bars = ax.bar(labels, [row[key] for row in summary])
        ax.bar_label(bars, fmt="%.3f", padding=3)
        ax.set(title=title, ylabel=unit)
        ax.tick_params(axis="x", rotation=25)
        ax.margins(y=0.18)
        ax.grid(axis="y", alpha=0.25)
    save(fig, "comparison.png")

    fig, ax = plt.subplots(figsize=(10, 5))
    static = [sum(s[1] for s in r["sends"] if s[3] == 0) / 1000 for r in runs]
    units = [sum(s[1] for s in r["sends"] if s[3] == 1) / 1000 for r in runs]
    ax.bar(labels, static, label="Статические объекты")
    ax.bar(labels, units, bottom=static, label="Юниты")
    ax.set(title="Объём данных по типам объектов", ylabel="KB за серию")
    ax.tick_params(axis="x", rotation=20)
    ax.legend()
    ax.grid(axis="y", alpha=0.25)
    save(fig, "object_types.png")
    print(f"Результаты: {args.output_dir.resolve()}")
    if any(r["status"] != "end_ok" for r in runs):
        print("* end_unconfirmed: конечное состояние не подтверждено всеми клиентами.")
    if args.show:
        plt.show()
    plt.close("all")


if __name__ == "__main__":
    main()
