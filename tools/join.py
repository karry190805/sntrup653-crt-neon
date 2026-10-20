# -*- coding: utf-8 -*-
"""校验 src/ 的分块视图能否还原出原文件（去掉每块的注释头后按序拼接）。

用法：
    python tools/join.py
"""
import hashlib
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MARK = b"/* ==== SPLIT:HEADER END ==== */\n"


def main():
    with open(os.path.join(HERE, "blocks.json"), encoding="utf-8") as f:
        meta = json.load(f)
    blocks = meta["blocks"]
    view_dir = os.path.join(ROOT, meta["view_dir"])

    parts = []
    for b in blocks:
        data = open(os.path.join(view_dir, b["file"]), "rb").read()
        idx = data.find(MARK)
        if idx < 0:
            raise SystemExit("missing header marker in %s" % b["file"])
        parts.append(data[idx + len(MARK):])
    joined = b"\n".join(parts)

    raw = open(os.path.join(ROOT, meta["canonical_path"]), "rb").read()
    ok = joined == raw
    print("blocks : %d" % len(blocks))
    print("source : %s" % hashlib.sha256(raw).hexdigest())
    print("joined : %s" % hashlib.sha256(joined).hexdigest())
    print("result : %s" % ("OK" if ok else "MISMATCH"))
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
