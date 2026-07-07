"""Generate HagVampire's first custom corpse blood-extraction animation.

Run through Blender:
  blender.exe --background --python tools/generate_cut_animation.py -- --root <HagVampire> --skyrim-data <Data>

The script extracts required vanilla assets from the local Skyrim install into
build/animation_source, imports the character skeleton through PyNifly, creates
a rough dagger throat-cut animation, then exports a Skyrim SE HKX.
"""

from __future__ import annotations

import argparse
import ctypes
import math
import os
import struct
import sys
import zlib
from pathlib import Path

import addon_utils
import bpy
from mathutils import Euler, Vector


ANIMATION_BSA = "Skyrim - Animations.bsa"
MESHES0_BSA = "Skyrim - Meshes0.bsa"
SKELETON_HKX = r"meshes\actors\character\character assets\skeleton.hkx"
DAGGER_NIF = r"meshes\animobjects\animobjectirondagger.nif"

CUSTOM_REL = Path("meshes/actors/character/animations/HagVampire/hagvampire_throat_cut.hkx")
IDLEKNEEL_REPLACER_REL = Path("meshes/actors/character/animations/idlekneelidle.hkx")
PREVIEW_BLEND_REL = Path("build/animation_preview/hagvampire_throat_cut.blend")


def parse_args() -> argparse.Namespace:
    args = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, help="HagVampire mod root")
    parser.add_argument("--skyrim-data", required=True, help="Skyrim Data folder")
    parser.add_argument("--lz4-dll", default=r"C:\dev\x64dbg\release\x64\lz4.dll")
    return parser.parse_args(args)


class BsaEntry:
    __slots__ = ("name", "size", "offset", "default_compressed")

    def __init__(self, name: str, size: int, offset: int, default_compressed: bool = False) -> None:
        self.name = name
        self.size = size
        self.offset = offset
        self.default_compressed = default_compressed


def bsa_entries(path: Path) -> list[BsaEntry]:
    data = path.read_bytes()
    cursor = 0

    def read(fmt: str):
        nonlocal cursor
        size = struct.calcsize(fmt)
        value = struct.unpack_from(fmt, data, cursor)
        cursor += size
        return value[0] if len(value) == 1 else value

    magic = data[cursor : cursor + 4]
    cursor += 4
    if magic != b"BSA\x00":
        raise RuntimeError(f"{path} is not a BSA archive")

    version = read("<I")
    if version != 105:
        raise RuntimeError(f"unsupported BSA version {version} in {path}")

    read("<I")  # folder record offset
    archive_flags = read("<I")
    folder_count = read("<I")
    file_count = read("<I")
    read("<I")  # total folder name length
    total_file_name_length = read("<I")
    read("<H")  # file flags
    read("<H")  # padding

    counts: list[int] = []
    for _ in range(folder_count):
        read("<Q")
        counts.append(read("<I"))
        read("<I")
        read("<Q")

    records: list[tuple[str, int, int]] = []
    for count in counts:
        name_len = read("<B")
        folder = data[cursor : cursor + name_len].rstrip(b"\x00").decode("ascii")
        cursor += name_len
        for _ in range(count):
            read("<Q")
            size = read("<I")
            offset = read("<I")
            records.append((folder, size, offset))

    names_blob = data[cursor : cursor + total_file_name_length]
    names = names_blob.rstrip(b"\x00").split(b"\x00")
    if len(names) < file_count:
        raise RuntimeError(f"BSA name table truncated in {path}")

    entries: list[BsaEntry] = []
    default_compressed = (archive_flags & 0x4) != 0
    for (folder, size, offset), name in zip(records, names):
        full_name = f"{folder}\\{name.decode('ascii')}".lstrip("\\").lower()
        entry = BsaEntry(full_name, size, offset)
        entry.default_compressed = default_compressed
        entries.append(entry)
    return entries


def lz4_decompress_block(block: bytes, max_output_size: int, lz4_dll: Path) -> bytes:
    dll = ctypes.CDLL(str(lz4_dll))
    fn = dll.LZ4_decompress_safe
    fn.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
    fn.restype = ctypes.c_int
    out = ctypes.create_string_buffer(max_output_size)
    written = fn(block, out, len(block), max_output_size)
    if written < 0:
        raise RuntimeError(f"LZ4_decompress_safe failed with {written}")
    return out.raw[:written]


def lz4_frame_decompress(payload: bytes, expected_size: int, lz4_dll: Path) -> bytes:
    if payload[:4] != b"\x04\x22\x4d\x18":
        raise RuntimeError("payload is not an LZ4 frame")

    cursor = 4
    flg = payload[cursor]
    bd = payload[cursor + 1]
    cursor += 2
    if (flg >> 6) != 1:
        raise RuntimeError(f"unsupported LZ4 frame version in FLG={flg:#x}")
    block_independent = (flg & 0x20) != 0
    block_checksum = (flg & 0x10) != 0
    has_content_size = (flg & 0x08) != 0
    content_checksum = (flg & 0x04) != 0
    has_dict_id = (flg & 0x01) != 0
    if not block_independent:
        raise RuntimeError("dependent LZ4 blocks are not supported")

    if has_content_size:
        cursor += 8
    if has_dict_id:
        cursor += 4
    cursor += 1  # header checksum

    block_max_by_code = {
        4: 64 * 1024,
        5: 256 * 1024,
        6: 1024 * 1024,
        7: 4 * 1024 * 1024,
    }
    block_max = block_max_by_code.get((bd >> 4) & 0x7, 4 * 1024 * 1024)
    chunks: list[bytes] = []
    while True:
        if cursor + 4 > len(payload):
            raise RuntimeError("truncated LZ4 frame block header")
        raw_size = struct.unpack_from("<I", payload, cursor)[0]
        cursor += 4
        if raw_size == 0:
            break
        uncompressed = (raw_size & 0x80000000) != 0
        block_size = raw_size & 0x7FFFFFFF
        block = payload[cursor : cursor + block_size]
        cursor += block_size
        if len(block) != block_size:
            raise RuntimeError("truncated LZ4 frame block")
        chunks.append(block if uncompressed else lz4_decompress_block(block, block_max, lz4_dll))
        if block_checksum:
            cursor += 4
    if content_checksum:
        cursor += 4

    result = b"".join(chunks)
    if len(result) != expected_size:
        raise RuntimeError(f"LZ4 frame size mismatch: expected {expected_size}, got {len(result)}")
    return result


def extract_bsa_file(bsa_path: Path, internal_name: str, out_path: Path, lz4_dll: Path) -> None:
    want = internal_name.lower()
    entries = bsa_entries(bsa_path)
    entry = next((item for item in entries if item.name == want), None)
    if entry is None:
        raise RuntimeError(f"{internal_name} not found in {bsa_path}")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with bsa_path.open("rb") as stream:
        stream.seek(entry.offset)
        compression_toggle = (entry.size & 0x40000000) != 0
        compressed = bool(getattr(entry, "default_compressed", False)) ^ compression_toggle
        real_size = entry.size & 0x3FFFFFFF
        if compressed:
            uncompressed_size = struct.unpack("<I", stream.read(4))[0]
            payload = stream.read(real_size - 4)
            if payload[:4] == b"\x04\x22\x4d\x18":
                content = lz4_frame_decompress(payload, uncompressed_size, lz4_dll)
            else:
                try:
                    content = zlib.decompress(payload)
                except zlib.error:
                    content = zlib.decompress(payload, -15)
            if len(content) != uncompressed_size:
                raise RuntimeError(f"bad decompressed size for {internal_name}")
        else:
            content = stream.read(real_size)
    out_path.write_bytes(content)


def enable_pynifly() -> None:
    ok = addon_utils.enable("io_scene_nifly", default_set=False, persistent=False)
    if not ok:
        raise RuntimeError("failed to enable io_scene_nifly")


def clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def import_skeleton(skeleton_hkx: Path) -> bpy.types.Object:
    bpy.ops.import_scene.pynifly_hkx(filepath=str(skeleton_hkx), create_collection=False)
    armatures = [obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"]
    if not armatures:
        raise RuntimeError("PyNifly imported no armature from skeleton.hkx")
    armature = armatures[0]
    bpy.context.view_layer.objects.active = armature
    armature.select_set(True)
    return armature


def find_bone(armature: bpy.types.Object, *needles: str) -> str | None:
    lowered = [(name, name.lower()) for name in armature.pose.bones.keys()]
    for needle in needles:
        n = needle.lower()
        for name, low in lowered:
            if n == low or n in low:
                return name
    return None


def must_bone(armature: bpy.types.Object, label: str, *needles: str) -> str:
    name = find_bone(armature, *needles)
    if not name:
        sample = ", ".join(list(armature.pose.bones.keys())[:40])
        raise RuntimeError(f"could not find {label} bone; sample bones: {sample}")
    return name


def set_pose(
    armature: bpy.types.Object,
    frame: int,
    rotations: dict[str, tuple[float, float, float]],
    locations: dict[str, tuple[float, float, float]] | None = None,
) -> None:
    bpy.context.scene.frame_set(frame)
    locations = locations or {}
    for bone_name, rotation in rotations.items():
        pose_bone = armature.pose.bones.get(bone_name)
        if not pose_bone:
            continue
        pose_bone.rotation_mode = "XYZ"
        pose_bone.rotation_euler = Euler(tuple(math.radians(v) for v in rotation), "XYZ")
        pose_bone.keyframe_insert("rotation_euler", frame=frame)
    for bone_name, location in locations.items():
        pose_bone = armature.pose.bones.get(bone_name)
        if not pose_bone:
            continue
        pose_bone.location = Vector(location)
        pose_bone.keyframe_insert("location", frame=frame)


def reset_selected_bones(armature: bpy.types.Object, frame: int, bones: list[str]) -> None:
    bpy.context.scene.frame_set(frame)
    for bone_name in bones:
        pose_bone = armature.pose.bones.get(bone_name)
        if not pose_bone:
            continue
        pose_bone.rotation_mode = "XYZ"
        pose_bone.rotation_euler = Euler((0.0, 0.0, 0.0), "XYZ")
        pose_bone.location = Vector((0.0, 0.0, 0.0))
        pose_bone.keyframe_insert("rotation_euler", frame=frame)
        pose_bone.keyframe_insert("location", frame=frame)


def import_dagger_preview(dagger_nif: Path, armature: bpy.types.Object, hand_bone: str) -> None:
    before = set(bpy.context.scene.objects)
    bpy.ops.import_scene.pynifly(filepath=str(dagger_nif))
    imported = [obj for obj in bpy.context.scene.objects if obj not in before]
    for obj in imported:
        if obj.type not in {"MESH", "EMPTY"}:
            continue
        obj.name = f"HagPreviewKnife_{obj.name}"
        obj.parent = armature
        obj.parent_type = "BONE"
        obj.parent_bone = hand_bone
        obj.location = (0.0, 0.0, 0.0)
        obj.rotation_euler = (math.radians(90.0), 0.0, math.radians(-90.0))
        obj.scale = (1.0, 1.0, 1.0)


def author_animation(armature: bpy.types.Object) -> None:
    bones = {
        "root": must_bone(armature, "root", "npc root", "root"),
        "com": must_bone(armature, "com", "npc com", "com"),
        "pelvis": must_bone(armature, "pelvis", "pelvis", "pelv"),
        "spine": must_bone(armature, "spine", "spine [spn0]", "spn0", "spine"),
        "spine1": must_bone(armature, "spine1", "spine1", "spn1"),
        "spine2": must_bone(armature, "spine2", "spine2", "spn2"),
        "neck": must_bone(armature, "neck", "neck"),
        "head": must_bone(armature, "head", "head"),
        "r_upper": must_bone(armature, "right upper arm", "upperarm.r", "r upperarm", "ruar", "right upper"),
        "r_fore": must_bone(armature, "right forearm", "forearm.r", "r forearm", "rfar", "right fore"),
        "r_hand": must_bone(armature, "right hand", "hand.r", "r hand", "rhnd", "right hand"),
        "l_upper": must_bone(armature, "left upper arm", "upperarm.l", "l upperarm", "luar", "left upper"),
        "l_fore": must_bone(armature, "left forearm", "forearm.l", "l forearm", "lfar", "left fore"),
        "l_hand": must_bone(armature, "left hand", "hand.l", "l hand", "lhnd", "left hand"),
        "r_thigh": must_bone(armature, "right thigh", "thigh.r", "r thigh", "rthg", "right thigh"),
        "r_calf": must_bone(armature, "right calf", "calf.r", "r calf", "rcalf", "right calf"),
        "l_thigh": must_bone(armature, "left thigh", "thigh.l", "l thigh", "lthg", "left thigh"),
        "l_calf": must_bone(armature, "left calf", "calf.l", "l calf", "lcalf", "left calf"),
    }

    bpy.context.view_layer.objects.active = armature
    armature.select_set(True)
    bpy.ops.object.mode_set(mode="POSE")

    used_bones = list(dict.fromkeys(bones.values()))
    frames = [1, 18, 38, 62, 78, 96, 126, 150]
    for frame in frames:
        reset_selected_bones(armature, frame, used_bones)

    common_crouch = {
        bones["com"]: (-8, 0, 0),
        bones["pelvis"]: (-14, 0, 0),
        bones["spine"]: (18, 0, 0),
        bones["spine1"]: (22, 0, -5),
        bones["spine2"]: (16, 3, -8),
        bones["neck"]: (-8, 0, 8),
        bones["head"]: (-5, 0, 8),
        bones["r_thigh"]: (-42, 0, 8),
        bones["r_calf"]: (56, 0, 0),
        bones["l_thigh"]: (-34, 0, -8),
        bones["l_calf"]: (42, 0, 0),
    }

    set_pose(
        armature,
        18,
        {
            **common_crouch,
            bones["r_upper"]: (20, -26, -18),
            bones["r_fore"]: (18, 2, 16),
            bones["r_hand"]: (0, 8, -20),
            bones["l_upper"]: (22, 18, 24),
            bones["l_fore"]: (28, 0, -18),
            bones["l_hand"]: (0, -8, 12),
        },
        {bones["com"]: (0.0, -6.0, -10.0)},
    )

    set_pose(
        armature,
        38,
        {
            **common_crouch,
            bones["spine1"]: (28, 0, -12),
            bones["spine2"]: (22, 6, -16),
            bones["r_upper"]: (48, -44, -32),
            bones["r_fore"]: (54, 4, 20),
            bones["r_hand"]: (18, 36, -44),
            bones["l_upper"]: (34, 24, 20),
            bones["l_fore"]: (42, -8, -26),
            bones["l_hand"]: (0, -10, 18),
        },
        {bones["com"]: (0.0, -12.0, -18.0)},
    )

    set_pose(
        armature,
        62,
        {
            **common_crouch,
            bones["spine1"]: (34, 4, -18),
            bones["spine2"]: (30, 8, -22),
            bones["r_upper"]: (74, -62, -46),
            bones["r_fore"]: (68, 10, 22),
            bones["r_hand"]: (24, 58, -68),
            bones["l_upper"]: (42, 36, 28),
            bones["l_fore"]: (50, -14, -30),
            bones["l_hand"]: (6, -12, 18),
        },
        {bones["com"]: (0.0, -18.0, -24.0)},
    )

    set_pose(
        armature,
        78,
        {
            **common_crouch,
            bones["spine1"]: (38, 10, -28),
            bones["spine2"]: (32, 16, -34),
            bones["r_upper"]: (78, -70, -70),
            bones["r_fore"]: (84, 16, 36),
            bones["r_hand"]: (20, 88, -104),
            bones["l_upper"]: (46, 40, 36),
            bones["l_fore"]: (54, -16, -34),
            bones["l_hand"]: (6, -18, 18),
        },
        {bones["com"]: (0.0, -20.0, -24.0)},
    )

    set_pose(
        armature,
        96,
        {
            **common_crouch,
            bones["spine1"]: (30, 4, -14),
            bones["spine2"]: (24, 8, -14),
            bones["r_upper"]: (42, -32, -26),
            bones["r_fore"]: (38, -4, 8),
            bones["r_hand"]: (8, 28, -22),
            bones["l_upper"]: (32, 22, 20),
            bones["l_fore"]: (38, -6, -20),
            bones["l_hand"]: (0, -10, 12),
        },
        {bones["com"]: (0.0, -10.0, -16.0)},
    )

    reset_selected_bones(armature, 150, used_bones)
    bpy.ops.object.mode_set(mode="OBJECT")

    action = armature.animation_data.action if armature.animation_data else None
    if action:
        action.name = "HagVampire_ThroatCut"

    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = 150
    bpy.context.scene.render.fps = 30
    marker = bpy.context.scene.timeline_markers.new("HAG_CUT_REWARD", frame=78)
    marker.camera = None


def export_hkx(armature: bpy.types.Object, custom_out: Path, replacer_out: Path) -> None:
    custom_out.parent.mkdir(parents=True, exist_ok=True)
    replacer_out.parent.mkdir(parents=True, exist_ok=True)

    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.pynifly_hkx(filepath=str(custom_out), game="SKYRIM_SE", fps=30)
    replacer_out.write_bytes(custom_out.read_bytes())


def main() -> None:
    opts = parse_args()
    root = Path(opts.root)
    skyrim_data = Path(opts.skyrim_data)
    lz4_dll = Path(opts.lz4_dll)
    source = root / "build" / "animation_source"

    skeleton_hkx = source / SKELETON_HKX.replace("\\", "/")
    dagger_nif = source / DAGGER_NIF.replace("\\", "/")
    extract_bsa_file(skyrim_data / ANIMATION_BSA, SKELETON_HKX, skeleton_hkx, lz4_dll)
    extract_bsa_file(skyrim_data / MESHES0_BSA, DAGGER_NIF, dagger_nif, lz4_dll)

    enable_pynifly()
    clear_scene()
    armature = import_skeleton(skeleton_hkx)
    right_hand = must_bone(armature, "right hand", "hand.r", "r hand", "rhnd", "right hand")
    import_dagger_preview(dagger_nif, armature, right_hand)
    author_animation(armature)

    custom_out = root / "assets" / CUSTOM_REL
    replacer_out = root / "assets" / IDLEKNEEL_REPLACER_REL
    export_hkx(armature, custom_out, replacer_out)

    preview = root / PREVIEW_BLEND_REL
    preview.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(preview))
    print(f"HAGVAMPIRE_ANIMATION_CUSTOM {custom_out}")
    print(f"HAGVAMPIRE_ANIMATION_IDLEKNEEL_REPLACER {replacer_out}")
    print(f"HAGVAMPIRE_ANIMATION_PREVIEW {preview}")


if __name__ == "__main__":
    main()
