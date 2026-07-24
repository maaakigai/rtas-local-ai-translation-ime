#!/usr/bin/env python3
"""Convert JMdict/EDICT2 XML into a flattened TSV for runtime ingestion.

The script streams the XML to keep memory usage stable. Each JMdict entry is
collapsed into a single TSV row with pipe-delimited variants and glosses.
"""

from __future__ import annotations

import argparse
import json
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional, Sequence, Set

XML_LANG = "{http://www.w3.org/XML/1998/namespace}lang"


@dataclass
class BilingualEntry:
    headword: str
    kanji_forms: List[str]
    kana_forms: List[str]
    english_glosses: List[str]
    part_of_speech: Set[str]
    domains: Set[str]
    misc: Set[str]
    priority: Set[str]

    def to_tsv_row(self) -> str:
        return "\t".join(
            [
                self.headword,
                "|".join(self.kanji_forms),
                "|".join(self.kana_forms),
                "|".join(self.english_glosses),
                "|".join(sorted(self.part_of_speech)),
                "|".join(sorted(self.domains)),
                "|".join(sorted(self.misc)),
                "|".join(sorted(self.priority)),
            ]
        )


@dataclass
class Stats:
    total_entries: int = 0
    kept_entries: int = 0
    dropped_no_english: int = 0
    dropped_priority: int = 0

    def as_dict(self) -> dict:
        keep_ratio = (
            round(self.kept_entries / self.total_entries, 6)
            if self.total_entries
            else 0.0
        )
        return {
            "total_entries": self.total_entries,
            "kept_entries": self.kept_entries,
            "dropped_no_english": self.dropped_no_english,
            "dropped_priority": self.dropped_priority,
            "keep_ratio": keep_ratio,
        }


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Flatten JMdict XML into TSV rows (one per entry)."
    )
    parser.add_argument(
        "--input",
        required=True,
        type=Path,
        help="JMdict_e.xml path.",
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="Destination TSV path.",
    )
    parser.add_argument(
        "--max-glosses",
        type=int,
        default=None,
        help="Cap the number of gloss strings retained per entry.",
    )
    parser.add_argument(
        "--only-common",
        action="store_true",
        help="Drop entries that do not carry any priority markers.",
    )
    parser.add_argument(
        "--stats",
        type=Path,
        help="Optional JSON file for processing statistics.",
    )
    return parser.parse_args(argv)


def load_entries(
    xml_path: Path, max_glosses: Optional[int], only_common: bool, stats: Stats
) -> Iterable[BilingualEntry]:
    if not xml_path.exists():
        raise FileNotFoundError(f"JMdict XML missing: {xml_path}")

    context = ET.iterparse(str(xml_path), events=("end",))
    for event, elem in context:
        if elem.tag != "entry":
            continue

        stats.total_entries += 1
        entry = extract_entry(elem, max_glosses=max_glosses)

        if entry is None:
            stats.dropped_no_english += 1
        elif only_common and not entry.priority:
            stats.dropped_priority += 1
        else:
            stats.kept_entries += 1
            yield entry

        elem.clear()


def extract_entry(elem: ET.Element, max_glosses: Optional[int]) -> Optional[BilingualEntry]:
    kanji_forms = [clean_text(keb.text) for keb in elem.findall("./k_ele/keb")]
    kana_forms = [clean_text(reb.text) for reb in elem.findall("./r_ele/reb")]
    if not kana_forms:
        # JMdict guarantees at least one reading element; guard for malformed rows.
        kana_forms = [""]

    priorities: Set[str] = set()
    for k_ele in elem.findall("./k_ele"):
        priorities.update(clean_text(pri.text) for pri in k_ele.findall("ke_pri"))
    for r_ele in elem.findall("./r_ele"):
        priorities.update(clean_text(pri.text) for pri in r_ele.findall("re_pri"))

    english_glosses: List[str] = []
    pos_tags: Set[str] = set()
    domains: Set[str] = set()
    misc: Set[str] = set()

    for sense in elem.findall("sense"):
        gloss_candidates = [
            clean_text(gloss.text)
            for gloss in sense.findall("gloss")
            if gloss.text
            and gloss.attrib.get(XML_LANG, "eng").startswith("en")
        ]
        if not gloss_candidates:
            continue

        english_glosses.extend(gloss_candidates)

        for tag in sense.findall("pos"):
            if tag.text:
                pos_tags.add(clean_text(tag.text))
        for field_tag in sense.findall("field"):
            if field_tag.text:
                domains.add(clean_text(field_tag.text))
        for misc_tag in sense.findall("misc"):
            if misc_tag.text:
                misc.add(clean_text(misc_tag.text))

    if not english_glosses:
        return None

    if max_glosses is not None and len(english_glosses) > max_glosses:
        english_glosses = english_glosses[:max_glosses]

    headword = (
        kanji_forms[0]
        if kanji_forms
        else kana_forms[0]
        if kana_forms
        else english_glosses[0]
    )

    return BilingualEntry(
        headword=headword,
        kanji_forms=kanji_forms,
        kana_forms=kana_forms,
        english_glosses=english_glosses,
        part_of_speech=pos_tags,
        domains=domains,
        misc=misc,
        priority=priorities,
    )


def clean_text(text: Optional[str]) -> str:
    if not text:
        return ""
    return " ".join(text.split())


def process(args: argparse.Namespace) -> Stats:
    stats = Stats()
    args.output.parent.mkdir(parents=True, exist_ok=True)

    with args.output.open("w", encoding="utf-8", newline="\n") as out_f:
        out_f.write(
            "headword\tkanji_forms\tkana_forms\tenglish_glosses\t"
            "part_of_speech\tdomains\tmisc\tpriority\n"
        )
        for entry in load_entries(
            args.input, max_glosses=args.max_glosses, only_common=args.only_common, stats=stats
        ):
            out_f.write(entry.to_tsv_row())
            out_f.write("\n")

    return stats


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    stats = process(args)

    print(
        f"[bilingual] kept {stats.kept_entries}/{stats.total_entries} entries "
        f"({stats.kept_entries / stats.total_entries:.2%} kept)"
        if stats.total_entries
        else "[bilingual] no entries processed",
        file=sys.stderr,
    )
    print(
        f"[bilingual] dropped_no_english={stats.dropped_no_english}, "
        f"dropped_priority={stats.dropped_priority}",
        file=sys.stderr,
    )

    if args.stats:
        args.stats.parent.mkdir(parents=True, exist_ok=True)
        with args.stats.open("w", encoding="utf-8") as stats_f:
            json.dump(stats.as_dict(), stats_f, ensure_ascii=False, indent=2)

    return 0


if __name__ == "__main__":
    sys.exit(main())

