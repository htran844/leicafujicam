#!/usr/bin/env python3
"""Build assets/poses/poses.json from the PoseGuide open dataset (MIT).

Source: https://github.com/mergeos-bounties/PoseGuide (data/poses, data/scenes).
Keeps English text and merges Vietnamese from tools/pose-data/vi_translations.json
when present (shape: {"poses": {id: {"name": ..., "tips": [...], "cameraCues": [...]}},
"scenes": {id: {"name": ...}}}).
"""
import json, glob, os, sys

SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "poseguide-src")
ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "app/src/main/assets/poses/poses.json")
VI_PATH = os.path.join(os.path.dirname(__file__), "vi_translations.json")

vi = {"poses": {}, "scenes": {}}
if os.path.exists(VI_PATH):
    vi = json.load(open(VI_PATH, encoding="utf-8"))

GENDER_PATH = os.path.join(os.path.dirname(__file__), "gender_map.json")
gender_map = {"female": set(), "male": set()}
if os.path.exists(GENDER_PATH):
    raw_map = json.load(open(GENDER_PATH, encoding="utf-8"))
    gender_map = {
        "female": set(raw_map.get("female", [])),
        "male": set(raw_map.get("male", [])),
    }

EXTRA_PATH = os.path.join(os.path.dirname(__file__), "extra_poses.json")
extras = []
if os.path.exists(EXTRA_PATH):
    extras = json.load(open(EXTRA_PATH, encoding="utf-8")).get("poses", [])

SOCIAL_PATH = os.path.join(os.path.dirname(__file__), "social_poses.json")
if os.path.exists(SOCIAL_PATH):
    extras += json.load(open(SOCIAL_PATH, encoding="utf-8")).get("poses", [])

poses = []
joint_keys = set()
for path in sorted(glob.glob(os.path.join(SRC, "poses/*.json"))):
    d = json.load(open(path))
    joints = {k: [round(float(v[0]), 4), round(float(v[1]), 4)] for k, v in d["joints"].items()}
    joint_keys |= set(joints)
    t = vi["poses"].get(d["id"], {})
    gender = (
        "female" if d["id"] in gender_map["female"]
        else "male" if d["id"] in gender_map["male"]
        else "unisex"
    )
    poses.append({
        "id": d["id"],
        "name": {"en": d["name"], **({"vi": t["name"]} if "name" in t else {})},
        "gender": gender,
        "standing": bool(d.get("standing", True)),
        "tags": d.get("tags", []),
        "tips": {"en": d.get("tips", []), **({"vi": t["tips"]} if "tips" in t else {})},
        "cameraCues": {"en": d.get("camera_cues", []), **({"vi": t["cameraCues"]} if "cameraCues" in t else {})},
        "joints": joints,
    })

scenes = []
for path in sorted(glob.glob(os.path.join(SRC, "scenes/*.json"))):
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

# Append the original gender-specific poses authored for leicafujicam and
# register them at the end of their scenes' ranked lists.
scene_by_id = {s["id"]: s for s in scenes}
known_ids = {p["id"] for p in poses}
for extra in extras:
    pose = {k: v for k, v in extra.items() if k != "scenes"}
    assert pose["id"] not in known_ids, f"duplicate pose id {pose['id']}"
    assert len(pose["joints"]) == 13, f"{pose['id']} must have 13 joints"
    known_ids.add(pose["id"])
    poses.append(pose)
    for scene_id in extra.get("scenes", []):
        assert scene_id in scene_by_id, f"unknown scene {scene_id} for {pose['id']}"
        scene_by_id[scene_id]["expectedPoses"].append(pose["id"])

out = {"poses": poses, "scenes": scenes}
os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump(out, open(OUT, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
from collections import Counter
print(f"poses={len(poses)} scenes={len(scenes)} joint_keys={sorted(joint_keys)}")
print("gender:", dict(Counter(p.get('gender', 'unisex') for p in poses)))
print("vi coverage: poses", sum(1 for p in poses if "vi" in p["name"]), "/", len(poses))
