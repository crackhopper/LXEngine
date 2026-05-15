#!/usr/bin/env python3
"""Import a curated CC0 low-poly model subset into assets/models/builtin.

The importer intentionally uses only sources with a commercial-use friendly
CC0 license. It keeps source credits under assets/models/builtin/_sources and
emits one asset.yaml manifest next to each imported model.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import re
import shutil
import sys
import textwrap
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
VENDOR_DIR = REPO_ROOT / ".cache" / "assets" / "vendor"
OUT_ROOT = REPO_ROOT / "assets" / "models" / "builtin"
MAX_TRIANGLES = 1000
MAX_MODEL_BYTES = 256 * 1024


@dataclass(frozen=True)
class SourcePack:
    key: str
    display_name: str
    url: str
    page_url: str
    source_obj_dir: str = "Models/OBJ format"


@dataclass(frozen=True)
class ImportItem:
    pack: str
    source_name: str
    category: str
    display_name: str
    asset_id: str


PACKS = {
    "furniture": SourcePack(
        key="furniture",
        display_name="Kenney Furniture Kit",
        url="https://kenney.nl/media/pages/assets/furniture-kit/e56d2a9828-1677580847/kenney_furniture-kit.zip",
        page_url="https://kenney.nl/assets/furniture-kit",
    ),
    "city_commercial": SourcePack(
        key="city_commercial",
        display_name="Kenney City Kit Commercial",
        url="https://kenney.nl/media/pages/assets/city-kit-commercial/16eb35d771-1753115042/kenney_city-kit-commercial_2.1.zip",
        page_url="https://kenney.nl/assets/city-kit-commercial",
    ),
    "city_suburban": SourcePack(
        key="city_suburban",
        display_name="Kenney City Kit Suburban",
        url="https://kenney.nl/media/pages/assets/city-kit-suburban/167f6dbc31-1745479373/kenney_city-kit-suburban_20.zip",
        page_url="https://kenney.nl/assets/city-kit-suburban",
    ),
    "city_roads": SourcePack(
        key="city_roads",
        display_name="Kenney City Kit Roads",
        url="https://kenney.nl/media/pages/assets/city-kit-roads/4b84d0ea8d-1741864740/kenney_city-kit-roads.zip",
        page_url="https://kenney.nl/assets/city-kit-roads",
    ),
    "nature": SourcePack(
        key="nature",
        display_name="Kenney Nature Kit",
        url="https://kenney.nl/media/pages/assets/nature-kit/8334871c74-1677698939/kenney_nature-kit.zip",
        page_url="https://kenney.nl/assets/nature-kit",
    ),
    "survival": SourcePack(
        key="survival",
        display_name="Kenney Survival Kit",
        url="https://kenney.nl/media/pages/assets/survival-kit/750a1eb3f9-1712149243/kenney_survival-kit.zip",
        page_url="https://kenney.nl/assets/survival-kit",
    ),
    "food": SourcePack(
        key="food",
        display_name="Kenney Food Kit",
        url="https://kenney.nl/media/pages/assets/food-kit/719eef5f43-1719418518/kenney_food-kit.zip",
        page_url="https://kenney.nl/assets/food-kit",
    ),
    "characters": SourcePack(
        key="characters",
        display_name="Kenney Blocky Characters",
        url="https://kenney.nl/media/pages/assets/blocky-characters/72bdc6be4c-1749547469/kenney_blocky-characters_20.zip",
        page_url="https://kenney.nl/assets/blocky-characters",
    ),
}


ITEMS = [
    ImportItem("furniture", "bedSingle", "indoor/furniture", "Bed Single", "indoor_furniture_bed_single"),
    ImportItem("furniture", "bookcaseOpen", "indoor/furniture", "Bookcase Open", "indoor_furniture_bookcase_open"),
    ImportItem("furniture", "chair", "indoor/furniture", "Chair", "indoor_furniture_chair"),
    ImportItem("furniture", "desk", "indoor/furniture", "Desk", "indoor_furniture_desk"),
    ImportItem("furniture", "loungeSofa", "indoor/furniture", "Lounge Sofa", "indoor_furniture_lounge_sofa"),
    ImportItem("furniture", "computerScreen", "indoor/props", "Computer Screen", "indoor_props_computer_screen"),
    ImportItem("survival", "box", "indoor/props", "Supply Box", "indoor_props_supply_box"),
    ImportItem("survival", "barrel", "indoor/props", "Barrel", "indoor_props_barrel"),
    ImportItem("food", "apple", "indoor/props", "Apple", "indoor_props_apple"),
    ImportItem("city_commercial", "low-detail-building-a", "outdoor/buildings", "Low Detail Building A", "outdoor_buildings_low_detail_a"),
    ImportItem("city_commercial", "low-detail-building-wide-a", "outdoor/buildings", "Low Detail Wide Building", "outdoor_buildings_low_detail_wide"),
    ImportItem("city_commercial", "low-detail-building-b", "outdoor/buildings", "Low Detail Building B", "outdoor_buildings_low_detail_b"),
    ImportItem("city_roads", "road-straight", "outdoor/roads", "Road Straight", "outdoor_roads_straight"),
    ImportItem("city_roads", "road-bend-square", "outdoor/roads", "Road Bend", "outdoor_roads_bend"),
    ImportItem("city_roads", "road-crossroad", "outdoor/roads", "Road Crossroad", "outdoor_roads_crossroad"),
    ImportItem("nature", "tree-oak", "outdoor/nature", "Oak Tree", "outdoor_nature_oak_tree"),
    ImportItem("nature", "tree-pine", "outdoor/nature", "Pine Tree", "outdoor_nature_pine_tree"),
    ImportItem("nature", "rock-large", "outdoor/nature", "Large Rock", "outdoor_nature_large_rock"),
    ImportItem("characters", "character-a", "characters", "Blocky Character A", "characters_blocky_a"),
    ImportItem("characters", "character-b", "characters", "Blocky Character B", "characters_blocky_b"),
]


def snake_case(text: str) -> str:
    text = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", text)
    text = re.sub(r"[^A-Za-z0-9]+", "_", text)
    return text.strip("_").lower()


def download_pack(pack: SourcePack) -> Path:
    VENDOR_DIR.mkdir(parents=True, exist_ok=True)
    path = VENDOR_DIR / Path(pack.url).name
    if path.exists() and path.stat().st_size > 0:
        return path
    print(f"download {pack.display_name}")
    with urllib.request.urlopen(pack.url) as response, path.open("wb") as out:
        shutil.copyfileobj(response, out)
    return path


def obj_candidates(zip_file: zipfile.ZipFile, pack: SourcePack, source_name: str) -> list[str]:
    base = f"{pack.source_obj_dir}/{source_name}"
    exact = f"{base}.obj"
    names = zip_file.namelist()
    if exact in names:
        return [exact]
    target = snake_case(source_name)
    candidates = [
        name
        for name in names
        if name.startswith(pack.source_obj_dir + "/")
        and name.lower().endswith(".obj")
        and snake_case(Path(name).stem) == target
    ]
    if candidates:
        return sorted(candidates)
    relaxed = [
        name
        for name in names
        if name.startswith(pack.source_obj_dir + "/")
        and name.lower().endswith(".obj")
        and target in snake_case(Path(name).stem)
    ]
    return sorted(relaxed)


def face_triangle_count(parts: list[str]) -> int:
    vertices = parts[1:]
    if len(vertices) < 3:
        return 0
    return len(vertices) - 2


def triangulate_obj(text: str) -> tuple[str, int]:
    out: list[str] = []
    triangles = 0
    for raw in text.splitlines():
        if raw.startswith("mtllib "):
            continue
        parts = raw.strip().split()
        if parts and parts[0] == "f":
            face = parts[1:]
            if len(face) < 3:
                continue
            for i in range(1, len(face) - 1):
                out.append(f"f {face[0]} {face[i]} {face[i + 1]}")
                triangles += 1
            continue
        out.append(raw)
    out.append("")
    return "\n".join(out), triangles


def write_yaml(path: Path, data: dict[str, object]) -> None:
    def scalar(value: object) -> str:
        if isinstance(value, bool):
            return "true" if value else "false"
        if isinstance(value, (int, float)):
            return str(value)
        return json.dumps(str(value), ensure_ascii=False)

    lines: list[str] = []
    for key, value in data.items():
        if isinstance(value, dict):
            lines.append(f"{key}:")
            for child_key, child_value in value.items():
                lines.append(f"  {child_key}: {scalar(child_value)}")
        else:
            lines.append(f"{key}: {scalar(value)}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def copy_license(zip_file: zipfile.ZipFile, pack: SourcePack, source_dir: Path) -> None:
    source_dir.mkdir(parents=True, exist_ok=True)
    for name in zip_file.namelist():
        lower = name.lower()
        if lower.endswith("license.txt") or lower.endswith("credits.txt"):
            target = source_dir / Path(name).name
            target.write_bytes(zip_file.read(name))
    source_md = source_dir / "SOURCE.md"
    source_md.write_text(
        textwrap.dedent(
            f"""\
            # {pack.display_name}

            - Author: Kenney
            - Source page: {pack.page_url}
            - Download: {pack.url}
            - License: Creative Commons CC0 1.0 Universal
            - Commercial use: allowed
            - Imported at: {_dt.datetime.now(_dt.UTC).date().isoformat()}

            These files are imported as a curated low-poly subset for LXEngine's
            built-in editor assets. Keep this source note and the original
            license/credits files when redistributing the imported models.
            """
        ),
        encoding="utf-8",
    )


def import_assets(dry_run: bool) -> int:
    if not dry_run and OUT_ROOT.exists():
        shutil.rmtree(OUT_ROOT)
    report = {"imported": [], "rejected": []}
    by_pack: dict[str, zipfile.ZipFile] = {}

    try:
        for pack_key in sorted({item.pack for item in ITEMS}):
            pack = PACKS[pack_key]
            archive = download_pack(pack)
            by_pack[pack_key] = zipfile.ZipFile(archive)
            if not dry_run:
                copy_license(by_pack[pack_key], pack, OUT_ROOT / "_sources" / pack.key)

        for item in ITEMS:
            pack = PACKS[item.pack]
            zip_file = by_pack[item.pack]
            candidates = obj_candidates(zip_file, pack, item.source_name)
            if not candidates:
                report["rejected"].append(
                    {"assetId": item.asset_id, "reason": "source OBJ not found"}
                )
                continue
            source_path = candidates[0]
            obj_text = zip_file.read(source_path).decode("utf-8", errors="replace")
            obj_out, triangles = triangulate_obj(obj_text)
            model_bytes = len(obj_out.encode("utf-8"))
            if triangles > MAX_TRIANGLES:
                report["rejected"].append(
                    {
                        "assetId": item.asset_id,
                        "source": source_path,
                        "reason": f"triangleCount {triangles} > {MAX_TRIANGLES}",
                    }
                )
                continue
            if model_bytes > MAX_MODEL_BYTES:
                report["rejected"].append(
                    {
                        "assetId": item.asset_id,
                        "source": source_path,
                        "reason": f"modelBytes {model_bytes} > {MAX_MODEL_BYTES}",
                    }
                )
                continue
            mesh_uri = (
                f"assets/models/builtin/{item.category}/{item.asset_id}/model.obj"
            )
            entry = {
                "assetId": item.asset_id,
                "displayName": item.display_name,
                "category": item.category,
                "meshUri": mesh_uri,
                "defaultMaterialUri": "assets/materials/blinnphong_lit.material",
                "sourcePack": pack.display_name,
                "sourceUrl": pack.page_url,
                "license": "CC0-1.0",
                "commercialUse": True,
                "triangleCount": triangles,
                "modelBytes": model_bytes,
            }
            report["imported"].append(entry)
            if dry_run:
                continue
            asset_dir = OUT_ROOT / item.category / item.asset_id
            asset_dir.mkdir(parents=True, exist_ok=True)
            (asset_dir / "original.obj").write_text(obj_text, encoding="utf-8")
            (asset_dir / "model.obj").write_text(obj_out, encoding="utf-8")
            write_yaml(asset_dir / "asset.yaml", entry)

        if not dry_run:
            write_yaml(
                OUT_ROOT / "README.asset.yaml",
                {
                    "description": "LXEngine built-in model asset manifest root",
                    "licensePolicy": "Only commercial-use friendly CC0 assets are imported here.",
                    "maxTrianglesPerModel": MAX_TRIANGLES,
                    "maxModelBytes": MAX_MODEL_BYTES,
                },
            )
            (OUT_ROOT / "import_report.json").write_text(
                json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                encoding="utf-8",
            )
    finally:
        for zip_file in by_pack.values():
            zip_file.close()

    print(
        f"imported {len(report['imported'])}, rejected {len(report['rejected'])}"
    )
    if report["rejected"]:
        for rejected in report["rejected"]:
            print("reject", rejected, file=sys.stderr)
    return 0 if report["imported"] else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    return import_assets(args.dry_run)


if __name__ == "__main__":
    raise SystemExit(main())
