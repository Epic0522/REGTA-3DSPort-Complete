"""Import one GTA vehicle in Blender and print its hierarchy/materials."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import bpy


def arguments() -> tuple[Path, Path, list[Path]]:
    separator = sys.argv.index("--")
    args = sys.argv[separator + 1 :]
    if len(args) < 3:
        raise SystemExit("expected DRAGONFF_ROOT DFF TXD... after --")
    return Path(args[0]).resolve(), Path(args[1]).resolve(), [
        Path(value).resolve() for value in args[2:]
    ]


def main() -> None:
    dragonff_root, dff_path, txd_paths = arguments()
    sys.path.insert(0, str(dragonff_root.parent))
    import DragonFF  # type: ignore[import-not-found]
    from DragonFF.ops.dff_importer import import_dff  # type: ignore[import-not-found]
    from DragonFF.ops.txd_importer import import_txd  # type: ignore[import-not-found]

    DragonFF.register()
    txd_images = {}
    for path in txd_paths:
        imported = import_txd(
            {"file_name": str(path), "skip_mipmaps": True, "pack": True}
        )
        txd_images.update(imported.images)

    import_dff(
        {
            "file_name": str(dff_path),
            "txd_images": txd_images,
            "image_ext": "PNG",
            "connect_bones": False,
            "use_mat_split": False,
            "remove_doubles": False,
            "create_backfaces": False,
            "group_materials": False,
            "import_normals": True,
            "materials_naming": "DEFAULT",
            "import_breakable": False,
            "hide_damage_parts": True,
        }
    )

    report = {
        "objects": [
            {
                "name": obj.name,
                "type": obj.type,
                "parent": obj.parent.name if obj.parent else None,
                "location": list(obj.location),
                "rotation": list(obj.rotation_quaternion),
                "materials": [slot.material.name if slot.material else None for slot in obj.material_slots],
            }
            for obj in bpy.context.scene.objects
        ],
        "materials": [
            {
                "name": material.name,
                "diffuse": list(material.diffuse_color),
                "textures": [
                    node.image.name
                    for node in material.node_tree.nodes
                    if node.type == "TEX_IMAGE" and node.image is not None
                ]
                if material.use_nodes and material.node_tree
                else [],
            }
            for material in bpy.data.materials
        ],
        "images": [image.name for image in bpy.data.images],
    }
    print("VEHICLE_IMPORT_REPORT_BEGIN")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    print("VEHICLE_IMPORT_REPORT_END")


if __name__ == "__main__":
    main()
