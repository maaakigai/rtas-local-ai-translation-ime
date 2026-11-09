"""Build the local RTAS glossary from Digital Extremes' public manifests."""

import json
import lzma
import os
import re
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path
from urllib.request import urlopen


HEADER = "# RTAS Warframe dictionary v1"
INDEX = "https://origin.warframe.com/PublicExport/index_{}.txt.lzma"
MANIFEST = "https://content.warframe.com/PublicExport/Manifest/"
NAME = re.compile(r"(Export[A-Za-z0-9]+)_(en|ja)\.json![A-Za-z0-9_+\-]+")
REQUIRED = {"ExportWarframes", "ExportWeapons", "ExportUpgrades", "ExportKeys",
            "ExportResources", "ExportRegions", "ExportRelicArcane"}
READINGS = {
    "arcane": "アルケイン", "blueprint": "ブループリント", "bramma": "ブランマ",
    "chamber": "チャンバー", "crosshairs": "クロスヘア", "diffusion": "ディフュージョン",
    "excalibur": "エクスカリバー", "galvanized": "ガルバナイズド", "hek": "ヘック",
    "hell": "ヘル", "kuva": "クバ", "lich": "リッチ", "mod": "モッド",
    "nukor": "ヌコア", "prime": "プライム", "requiem": "レクイエム",
    "scope": "スコープ", "shot": "ショット", "umbra": "アンブラ", "zarr": "ザール",
}
# The Wiki identifies gaps. English and Japanese official pages verify each label.
LICH_WIKI = "https://wiki.warframe.com/w/Kuva_Lich"
OLD_BLOOD = ("https://www.warframe.com/en/news/the-old-blood-8",
             "https://www.warframe.com/ja/news/the-old-blood-8")
LICH_GUIDE = ("https://www.warframe.com/en/news/lich-sistemi-nedir",
              "https://www.warframe.com/ja/news/lich-sistemi-nedir")
SUPPLEMENTS = (
    ("Kuva Lich", "クバ・リッチ", "くばりっち", OLD_BLOOD),
    ("Kuva Larvling", "クバ ラルブリング", "くばらるぶりんぐ", LICH_GUIDE),
    ("Parazon", "Parazon", "ぱらぞん", OLD_BLOOD),
    ("Sisters of Parvos", "Parvosシスター", "ぱるぼすしすたー", LICH_GUIDE),
    ("Requiem Ultimatum", "レクイエム・アルティメイタム", "れくいえむあるてぃめいたむ",
     ("https://www.warframe.com/en/news/hermanas-de-parvos",
      "https://www.warframe.com/ja/news/sorelle-di-parvos")),
)


def download(url, limit):
    with urlopen(url, timeout=20) as response:
        data = response.read(limit + 1)
    if not data or len(data) > limit:
        raise ValueError(f"Unexpected response size: {url}")
    return data


def read_index(language):
    blob = download(INDEX.format(language), 1024 * 1024)
    if len(blob) < 14 or blob[0] >= 225:
        raise ValueError(f"Invalid {language} index header")
    size = int.from_bytes(blob[5:13], "little")
    dictionary_size = int.from_bytes(blob[1:5], "little")
    if not 0 < size <= 1024 * 1024 or not 0 < dictionary_size <= 64 * 1024 * 1024:
        raise ValueError(f"Invalid {language} index size")
    # DE's streams declare an output size and can omit the end marker.
    properties = blob[0]
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=[{
        "id": lzma.FILTER_LZMA1, "dict_size": dictionary_size,
        "lc": properties % 9, "lp": properties // 9 % 5, "pb": properties // 45,
    }])
    decoded = decoder.decompress(blob[13:], max_length=size)
    if len(decoded) != size:
        raise ValueError(f"Truncated {language} index")
    result = {}
    for line in decoded.decode("utf-8-sig").splitlines():
        match = NAME.fullmatch(line.strip())
        if match and match[2] == language:
            result[match[1]] = match[0]
    if not REQUIRED <= result.keys():
        raise ValueError(f"Incomplete {language} index")
    return result


def names(file):
    data = json.loads(download(MANIFEST + file, 24 * 1024 * 1024).decode("utf-8-sig"), strict=False)
    result = {}

    def collect(category, value):
        if isinstance(value, list):
            for child in value:
                collect(category, child)
        elif isinstance(value, dict):
            identifier, name = value.get("uniqueName"), value.get("name")
            kind = category
            if "abilityUniqueName" in value:
                kind = "ExportAbilities"
                identifier, name = value.get("abilityUniqueName"), value.get("abilityName")
            if isinstance(identifier, str) and identifier and isinstance(name, str):
                name = " ".join(name.split())
                if name and len(name) <= 256 and all(ord(ch) >= 32 for ch in name + identifier):
                    result[f"{kind}:{identifier}"] = name
            for child in value.values():
                collect(category, child)

    for category, rows in data.items():
        collect(category, rows)
    return result


def reading_for(english, japanese):
    # The export contains display names, not phonetic readings.
    if not japanese or not japanese.isascii():
        return ""
    words = english.lower().split()
    return "".join(READINGS[word] for word in words) if all(word in READINGS for word in words) else ""


def update(destination):
    records = {}
    missing_japanese = 0
    missing_english = 0
    unpaired = {}
    with ThreadPoolExecutor(max_workers=6) as pool:
        en, ja = pool.map(read_index, ("en", "ja"))
        downloads = {file: pool.submit(names, file) for file in set(en.values()) | set(ja.values())}
        for category in sorted(en.keys() | ja.keys()):
            english = downloads[en[category]].result() if category in en else {}
            japanese = downloads[ja[category]].result() if category in ja else {}
            if category in REQUIRED and (not english or not japanese):
                raise ValueError(f"No names found in {category}")
            missing_english += len(japanese.keys() - english.keys())
            for identifier in japanese.keys() - english.keys():
                unpaired[identifier] = {"ja": japanese[identifier]}
            for identifier, name in english.items():
                translated = japanese.get(identifier, "")
                missing_japanese += not translated
                if not translated:
                    unpaired[identifier] = {"en": name}
                records[identifier] = (name, translated, reading_for(name, translated))

    if len(records) < 1000:
        raise ValueError("Incomplete export")
    supplement_sources = {}
    for english, japanese, reading, official in SUPPLEMENTS:
        identifier = f"supplement:{english}"
        records[identifier] = (english, japanese, reading)
        supplement_sources[identifier] = {"wiki": LICH_WIKI, "official": official}
    previous = {}
    if destination.is_file():
        with destination.open(encoding="utf-8") as stream:
            if stream.readline().rstrip() == HEADER:
                stream.readline()
                for line in stream:
                    parts = line.rstrip("\n").split("\t")
                    if len(parts) == 4:
                        previous[parts[3]] = tuple(parts[:3])
    metadata = {
        "updated_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "entries": len(records),
        "added": len(records.keys() - previous.keys()),
        "changed": sum(records[key] != previous[key] for key in records.keys() & previous.keys()),
        "removed": len(previous.keys() - records.keys()),
        "missing_ja": missing_japanese,
        "missing_en": missing_english,
        "unpaired": unpaired,
        "sources": {"en": en, "ja": ja},
        "supplement_sources": supplement_sources,
    }
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n", delete=False,
                                         dir=destination.parent, prefix="warframe-", suffix=".tmp") as stream:
            temporary = Path(stream.name)
            stream.write(f"{HEADER}\n# {json.dumps(metadata, ensure_ascii=False)}\n")
            for identifier, fields in sorted(records.items()):
                stream.write("\t".join((*fields, identifier)) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(f"{len(records)} entries")


if __name__ == "__main__":
    try:
        update(Path(sys.argv[1]))
    except (OSError, ValueError, TypeError, IndexError, lzma.LZMAError) as error:
        print(f"Warframe dictionary update failed: {error}", file=sys.stderr)
        sys.exit(1)
