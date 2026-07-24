#!/usr/bin/env python3
"""Convert MeCab-style morphological dictionaries into a normalised TSV.

The script expects CSV rows that follow the standard 13-column layout used by
UniDic-lite, mecab-ipadic, and mecab-ipadic-NEologd:

0: surface
1: left_id
2: right_id
3: word_cost
4: pos
5: pos_detail_1
6: pos_detail_2
7: pos_detail_3
8: conjugation_type
9: conjugation_form
10: base_form
11: reading
12: pronunciation

Only a subset of the columns is persisted. The output TSV schema is:
surface<TAB>reading<TAB>base_form<TAB>pos<TAB>cost<TAB>features\n
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, List, Optional, Sequence


@dataclass
class MorphEntry:
    surface: str
    reading: str
    base_form: str
    pos: str
    cost: int
    features: List[str] = field(default_factory=list)

    def to_tsv_row(self) -> str:
        features_str = ";".join(self.features)
        return "\t".join(
            [
                self.surface,
                self.reading,
                self.base_form,
                self.pos,
                str(self.cost),
                features_str,
            ]
        )


@dataclass
class Stats:
    total_rows: int = 0
    kept_rows: int = 0
    dropped_by_pos: int = 0
    dropped_by_cost: int = 0
    dropped_by_reading: int = 0

    def as_dict(self) -> dict:
        return {
            "total_rows": self.total_rows,
            "kept_rows": self.kept_rows,
            "dropped_by_pos": self.dropped_by_pos,
            "dropped_by_cost": self.dropped_by_cost,
            "dropped_by_reading": self.dropped_by_reading,
            "keep_ratio": round(self.kept_rows / self.total_rows, 6)
            if self.total_rows
            else 0.0,
        }


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert MeCab CSV dictionaries into a compact TSV format."
    )
    parser.add_argument(
        "--input",
        nargs="+",
        required=True,
        type=Path,
        help="Input CSV files (UniDic/meCab layout).",
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="Destination TSV path. Parent directories are created automatically.",
    )
    parser.add_argument(
        "--keep-pos",
        nargs="*",
        default=None,
        help="Whitelist of coarse POS tags (column 4). If omitted, keep everything.",
    )
    parser.add_argument(
        "--min-cost",
        type=int,
        default=None,
        help="Drop entries with cost lower than this threshold.",
    )
    parser.add_argument(
        "--max-cost",
        type=int,
        default=None,
        help="Drop entries with cost higher than this threshold.",
    )
    parser.add_argument(
        "--require-reading",
        action="store_true",
        help="Skip entries that do not provide a katakana reading (column 11).",
    )
    parser.add_argument(
        "--stats",
        type=Path,
        help="Optional path to dump processing statistics as JSON.",
    )
    return parser.parse_args(argv)


def iter_rows(path: Path) -> Iterable[List[str]]:
    with path.open("r", encoding="utf-8", newline="") as fh:
        reader = csv.reader(fh)
        for row in reader:
            if not row or row[0].startswith("#"):
                continue
            yield row


def normalise_row(row: Sequence[str]) -> MorphEntry:
    # UniDic CSJ "lex.csv" ships 33 columns. The relevant layout is:
    # 0: surface, 3: cost, 4-7: POS (coarse/detail), 8: conj_type, 9: conj_form,
    # 10: reading (katakana), 11: lemma/base, 13: pronunciation.
    if len(row) >= 20:  # Detect the CSJ layout
        surface = row[0]
        cost = int(row[3])
        pos = row[4]
        base_form = row[11] or surface
        reading = row[10] or base_form or surface
        pos_details = [p for p in row[5:8] if p and p != "*"]
        conj_type = row[8]
        conj_form = row[9]
        pronunciation = row[13] if len(row) > 13 else ""
    else:
        # Fallback to standard 13-column UniDic / ipadic layout.
        padded = list(row) + [""] * (13 - len(row))
        surface = padded[0]
        cost = int(padded[3])
        pos = padded[4]
        base_form = padded[10] or surface
        reading = padded[11] or padded[12] or surface
        pos_details = [p for p in padded[5:8] if p and p != "*"]
        conj_type = padded[8]
        conj_form = padded[9]
        pronunciation = padded[12]

    features: List[str] = []
    if pos_details:
        features.append("pos_details=" + ",".join(pos_details))
    if conj_type and conj_type != "*":
        features.append(f"conj_type={conj_type}")
    if conj_form and conj_form != "*":
        features.append(f"conj_form={conj_form}")
    if pronunciation and pronunciation != reading:
        features.append(f"pron={pronunciation}")

    return MorphEntry(
        surface=surface,
        reading=reading,
        base_form=base_form,
        pos=pos,
        cost=cost,
        features=features,
    )


def should_keep(entry: MorphEntry, args: argparse.Namespace, stats: Stats) -> bool:
    if args.keep_pos and entry.pos not in args.keep_pos:
        stats.dropped_by_pos += 1
        return False

    if args.min_cost is not None and entry.cost < args.min_cost:
        stats.dropped_by_cost += 1
        return False

    if args.max_cost is not None and entry.cost > args.max_cost:
        stats.dropped_by_cost += 1
        return False

    if args.require_reading and not entry.reading:
        stats.dropped_by_reading += 1
        return False

    return True


def process_files(args: argparse.Namespace) -> Stats:
    stats = Stats()
    args.output.parent.mkdir(parents=True, exist_ok=True)

    with args.output.open("w", encoding="utf-8", newline="\n") as out_f:
        out_f.write("surface\treading\tbase_form\tpos\tcost\tfeatures\n")

        for csv_path in args.input:
            if not csv_path.exists():
                raise FileNotFoundError(f"Input CSV missing: {csv_path}")
            for row in iter_rows(csv_path):
                stats.total_rows += 1
                entry = normalise_row(row)
                if should_keep(entry, args, stats):
                    out_f.write(entry.to_tsv_row())
                    out_f.write("\n")
                    stats.kept_rows += 1

    return stats


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    stats = process_files(args)

    print(
        f"[morph] kept {stats.kept_rows}/{stats.total_rows} rows "
        f"({stats.kept_rows / stats.total_rows:.2%} kept)"
        if stats.total_rows
        else "[morph] no rows processed",
        file=sys.stderr,
    )
    print(
        f"[morph] dropped_by_pos={stats.dropped_by_pos}, "
        f"dropped_by_cost={stats.dropped_by_cost}, "
        f"dropped_by_reading={stats.dropped_by_reading}",
        file=sys.stderr,
    )

    if args.stats:
        args.stats.parent.mkdir(parents=True, exist_ok=True)
        with args.stats.open("w", encoding="utf-8") as stats_f:
            json.dump(stats.as_dict(), stats_f, ensure_ascii=False, indent=2)

    return 0


if __name__ == "__main__":
    sys.exit(main())
