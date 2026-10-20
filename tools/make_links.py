# -*- coding: utf-8 -*-
"""生成核心代码分块索引 CODE_MAP.md，这是本工具唯一的产物。

用法：
    python3 tools/make_links.py                     # 自动读 git remote / HEAD
    python3 tools/make_links.py --owner alice --repo sntrup653-crt-neon --ref <commit>
    python3 tools/make_links.py --ref main          # 用分支名（不推荐用于引用）
"""
import argparse
import json
import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def sh(*args):
    try:
        out = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
        return out.stdout.strip() if out.returncode == 0 else ""
    except OSError:
        return ""


def guess_repo():
    url = sh("git", "config", "--get", "remote.origin.url")
    m = re.search(r"github\.com[:/]+([^/]+)/([^/\s]+?)(?:\.git)?$", url)
    if m:
        return m.group(1), m.group(2)
    return "", ""


def load_blocks():
    with open(os.path.join(HERE, "blocks.json"), encoding="utf-8") as f:
        meta = json.load(f)
    blocks = meta["blocks"]
    for i, b in enumerate(blocks):
        b["end"] = (blocks[i + 1]["start"] - 1) if i + 1 < len(blocks) else meta["total_lines"]
    return meta, blocks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--owner")
    ap.add_argument("--repo")
    ap.add_argument("--ref")
    args = ap.parse_args()

    meta, blocks = load_blocks()
    owner = args.owner or ""
    repo = args.repo or ""
    if not owner or not repo:
        g_owner, g_repo = guess_repo()
        owner = owner or g_owner or "OWNER"
        repo = repo or g_repo or "sntrup653-crt-neon"
    ref = args.ref or sh("git", "rev-parse", "HEAD") or "main"

    base = "https://github.com/%s/%s" % (owner, repo)
    core = "%s/blob/%s/%s" % (base, ref, meta["canonical_path"])

    def url(b):
        return "%s/blob/%s/%s/%s" % (base, ref, meta["view_dir"], b["file"])

    md = []
    md.append("# 核心代码分块索引\n")
    md.append("源文件 `%s`（%d 行）按算法层次切分为 %d 块，`%s/` 下每块一个文件。"
              "切分只加注释头、不改代码，`tools/join.py` 去掉注释头即可拼回并比对 SHA-256；"
              "引用用整文件链接，行号不会漂移。三个平台包中的源文件逐字节相同。\n"
              % (meta["canonical_path"], meta["total_lines"], len(blocks), meta["view_dir"]))
    md.append("| 块 | 论文小节 | 内容 | 主要函数 / 宏 | 原文件行号 | 分块文件 |")
    md.append("|---|---|---|---|---|---|")
    for b in blocks:
        sym = b.get("symbols", "").replace("|", "\\|")
        md.append("| %s | %s | %s | `%s` | L%d–L%d | [`%s`](%s) |"
                  % (b["id"], b["paper"], b["title"], sym, b["start"], b["end"], b["file"], url(b)))
    md.append("")
    md.append("## 纯文本链接（可直接粘进邮件或投稿系统）\n")
    md.append("```text")
    md.append(base)
    for b in blocks:
        md.append("%s  %-32s %s" % (b["id"], b["file"], url(b)))
    md.append("%s#L1-L%d   （整份源文件）" % (core, meta["total_lines"]))
    md.append("```")
    md.append("")
    md.append("## 复现入口\n")
    md.append("每个平台包都是自包含的，按 `README.md` 的“快速开始”一节执行 "
              "`sudo bash ./run_on_target.sh` 即可重跑编译、正确性与计时。\n")
    with open(os.path.join(ROOT, "CODE_MAP.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(md))

    print("owner/repo : %s/%s" % (owner, repo))
    print("ref        : %s" % ref)
    print("blocks     : %d  (lines %d..%d)" % (len(blocks), blocks[0]["start"], blocks[-1]["end"]))
    print("wrote      : CODE_MAP.md")


if __name__ == "__main__":
    main()
