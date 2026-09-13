"""Print imported GTA banner vehicle mesh hierarchy for build diagnostics."""

from __future__ import annotations

import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_vehicle_banner as build


def main() -> None:
    separator = sys.argv.index("--")
    dragonff_root = Path(sys.argv[separator + 1]).resolve()
    output_dir = Path(sys.argv[separator + 2]).resolve()
    game = sys.argv[separator + 3]
    spec = build.SPECS[game]
    sys.path.insert(0, str(dragonff_root.parent))
    import DragonFF  # type: ignore[import-not-found]
    from DragonFF.ops.dff_importer import import_dff  # type: ignore[import-not-found]
    from DragonFF.ops.txd_importer import import_txd  # type: ignore[import-not-found]

    DragonFF.register()
    build.clear_scene()
    source_dir = output_dir / "source"
    txd_images = {}
    for name in (
        "generic.txd",
        spec.txd_name,
        spec.wheel_txd_name,
        "particle.txd",
    ):
        txd_images.update(build.import_txd_casefold(import_txd, source_dir / name))
    build.import_dff_scene(import_dff, source_dir / spec.dff_name, txd_images)
    build.remove_damage_and_low_detail()
    build.add_wheels(import_dff, source_dir, txd_images, spec)
    bpy.context.view_layer.update()
    for obj in sorted(bpy.context.scene.objects, key=lambda item: item.name):
        if obj.type != "MESH":
            continue
        parent = obj.parent.name if obj.parent else "-"
        materials = ",".join(
            slot.material.name if slot.material else "-"
            for slot in obj.material_slots
        )
        centre = obj.matrix_world.translation
        print(
            f"MESH {obj.name} parent={parent} "
            f"centre=({centre.x:.4f},{centre.y:.4f},{centre.z:.4f}) "
            f"verts={len(obj.data.vertices)} materials={materials}"
        )


if __name__ == "__main__":
    main()
