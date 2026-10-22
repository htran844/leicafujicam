#!/usr/bin/env python3
"""Build assets/poses/poses.json from the PoseGuide open dataset (MIT).

Source: https://github.com/mergeos-bounties/PoseGuide (data/poses, data/scenes).
Keeps English text and merges Vietnamese from tools/pose-data/vi_translations.json
when present (shape: {"poses": {id: {"name": ..., "tips": [...], "cameraCues": [...]}},
"scenes": {id: {"name": ...}}}).
"""
import json, glob, os, sys

SRC = sys.argv[1] if len(sys.argv) > 1 else "/tmp/poseguide/PoseGuide-master"
ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "app/src/main/assets/poses/poses.json")
VI_PATH = os.path.join(os.path.dirname(__file__), "vi_translations.json")

vi = {"poses": {}, "scenes": {}}
if os.path.exists(VI_PATH):
    vi = json.load(open(VI_PATH, encoding="utf-8"))

poses = []
joint_keys = set()
for path in sorted(glob.glob(os.path.join(SRC, "data/poses/*.json"))):
    d = json.load(open(path))
    joints = {k: [round(float(v[0]), 4), round(float(v[1]), 4)] for k, v in d["joints"].items()}
    joint_keys |= set(joints)
    t = vi["poses"].get(d["id"], {})
    poses.append({
        "id": d["id"],
        "name": {"en": d["name"], **({"vi": t["name"]} if "name" in t else {})},
        "standing": bool(d.get("standing", True)),
        "tags": d.get("tags", []),
        "tips": {"en": d.get("tips", []), **({"vi": t["tips"]} if "tips" in t else {})},
        "cameraCues": {"en": d.get("camera_cues", []), **({"vi": t["cameraCues"]} if "cameraCues" in t else {})},
        "joints": joints,
    })

scenes = []
for path in sorted(glob.glob(os.path.join(SRC, "data/scenes/*.json"))):
    d = json.load(open(path))
    t = vi["scenes"].get(d["id"], {})
    scenes.append({
        "id": d["id"],
        "name": {"en": d["name"], **({"vi": t["name"]} if "name" in t else {})},
        "tags": d.get("tags", []),
        "mood": d.get("mood", []),
        "expectedPoses": d.get("expected_poses", []),
        "composition": d.get("composition", {}),
    })

out = {"poses": poses, "scenes": scenes}
os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump(out, open(OUT, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print(f"poses={len(poses)} scenes={len(scenes)} joint_keys={sorted(joint_keys)}")
print("vi coverage: poses", sum(1 for p in poses if "vi" in p["name"]), "/", len(poses))
