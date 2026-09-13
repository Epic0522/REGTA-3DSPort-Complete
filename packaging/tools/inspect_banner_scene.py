"""Print world-space wheel and wheel-dummy bounds from a built banner scene."""

from __future__ import annotations

import json

import bpy
from mathutils import Vector


def bounds(obj: bpy.types.Object) -> list[list[float]] | None:
    if obj.type != "MESH":
        return None
    points = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    return [
        [min(point[i] for point in points) for i in range(3)],
        [max(point[i] for point in points) for i in range(3)],
    ]


names = (
    "wheel_lf_dummy",
    "wheel_rf_dummy",
    "wheel_lb_dummy",
    "wheel_rb_dummy",
    "wheel_lf",
    "wheel_rf",
    "wheel_lb",
    "wheel_rb",
)
print(
    json.dumps(
        {
            name: {
                "type": bpy.data.objects[name].type,
                "parent": bpy.data.objects[name].parent.name
                if bpy.data.objects[name].parent
                else None,
                "local_location": list(bpy.data.objects[name].location),
                "local_rotation": list(bpy.data.objects[name].rotation_euler),
                "local_scale": list(bpy.data.objects[name].scale),
                "world_location": list(bpy.data.objects[name].matrix_world.translation),
                "world_bounds": bounds(bpy.data.objects[name]),
                "materials": [
                    {
                        "name": slot.material.name,
                        "diffuse": list(slot.material.diffuse_color),
                        "surface_render_method": getattr(
                            slot.material, "surface_render_method", None
                        ),
                        "backface_culling": slot.material.use_backface_culling,
                    }
                    if slot.material
                    else None
                    for slot in bpy.data.objects[name].material_slots
                ],
                "polygons_by_material": {
                    str(index): sum(
                        polygon.material_index == index
                        for polygon in bpy.data.objects[name].data.polygons
                    )
                    for index in range(
                        len(bpy.data.objects[name].material_slots)
                    )
                }
                if bpy.data.objects[name].type == "MESH"
                else None,
            }
            for name in names
        },
        indent=2,
    )
)
