# -*- coding: utf-8 -*-
"""把核心源文件按 tools/blocks.json 切成 src/ 下的分块视图，并自校验。

切分只做拷贝，不改一个字节；每块前面加一段注释头标块号、论文小节和行号，
生成后去掉注释头重新拼接，与原文件比对 SHA-256。

用法：
    python tools/split.py            # 切分 + 校验
    python tools/split.py --check    # 只校验现有 src/
"""
import argparse
import hashlib
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MARK = b"/* ==== SPLIT:HEADER END ==== */\n"

HEADER = """\
/* ---------------------------------------------------------------------------
 * {bid}  {title}  —— 论文 {paper}
 * 源文件第 {start}--{end} 行（共 {n} 行）
 *
 * 本文件是 crypto_core_multsntrup653.c 的分块视图，由 tools/split.py 原样切分
 * 生成，不是独立编译单元。按 B01…B{last:02d} 顺序去掉本注释头后拼接即得原文件，
 * 可用 tools/join.py 校验 SHA-256。
 * ------------------------------------------------------------------------- */
"""


def load():
    with open(os.path.join(HERE, "blocks.json"), encoding="utf-8") as f:
        meta = json.load(f)
    blocks = meta["blocks"]
    for i, b in enumerate(blocks):
        b["end"] = (blocks[i + 1]["start"] - 1) if i + 1 < len(blocks) else meta["total_lines"]
    return meta, blocks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    meta, blocks = load()
    src_path = os.path.join(ROOT, meta["canonical_path"])
    view_dir = os.path.join(ROOT, meta["view_dir"])
    raw = open(src_path, "rb").read()
    lines = raw.split(b"\n")

    if not args.check:
        os.makedirs(view_dir, exist_ok=True)
        for b in blocks:
            chunk = b"\n".join(lines[b["start"] - 1:b["end"]])
            head = HEADER.format(bid=b["id"], title=b["title"], paper=b["paper"],
                                 start=b["start"], end=b["end"],
                                 n=b["end"] - b["start"] + 1,
                                 last=len(blocks)).encode("utf-8")
            with open(os.path.join(view_dir, b["file"]), "wb") as f:
                f.write(head + MARK + chunk)
        # 清掉上一版遗留的块文件
        keep = {b["file"] for b in blocks}
        for name in os.listdir(view_dir):
            if name.endswith(".c") and name not in keep:
                os.remove(os.path.join(view_dir, name))

    # 校验：去掉注释头，按序拼接，比对 SHA-256
    parts = []
    for b in blocks:
        data = open(os.path.join(view_dir, b["file"]), "rb").read()
        idx = data.find(MARK)
        if idx < 0:
            raise SystemExit("missing header marker in %s" % b["file"])
        parts.append(data[idx + len(MARK):])
    joined = b"\n".join(parts)
    ok = joined == raw
    print("source  : %s" % meta["canonical_path"])
    print("lines   : %d   blocks: %d" % (meta["total_lines"], len(blocks)))
    print("sha256  : %s (source)" % hashlib.sha256(raw).hexdigest()[:32])
    print("sha256  : %s (joined)" % hashlib.sha256(joined).hexdigest()[:32])
    print("result  : %s" % ("OK" if ok else "MISMATCH"))
    if not ok:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
