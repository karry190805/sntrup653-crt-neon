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
    md.append("## 推荐阅读顺序\n")
    md.append("从 B19 的对外接口开始读，生产路径只有一条：\n")
    md.append("B19 入口函数 → B06 表初始化 → B07 输入分块 → B12 77 点 NEON 变换（正向）"
              " → B14 十六维局部乘法 → B12 逆变换 → B16 低位 73 项分支"
              " → B17 目标环折回 → B19 编码输出\n")
    md.append("B05、B09 是主路径反复调用的模 q 运算与 NEON 约减辅助函数，"
              "B15 提供系数索引与四通道点积。其余几块不在主路径上：\n")
    md.append("- B08 是标量 Good–Thomas 参考实现，只在非 NEON 构建和内部自检中调用。")
    md.append("- B10 是 7 点/11 点和差配对变换的基础函数，由 B12 调用。")
    md.append("- B11 是公开补零模式下的编译期特化，宏展开进 B12。")
    md.append("- B13 是十六维局部乘法的标量参考实现与 NEON 辅助函数，辅助函数由 B14 调用。")
    md.append("- B18 只在内部测试宏打开时使用，不属于生产路径。\n")
    md.append("## 三条路径\n")
    md.append("| 路径 | 涉及块 | 说明 |")
    md.append("|---|---|---|")
    md.append("| 生产 | B19 → B06/B07 → B12 → B14 → B16 → B17 | 交付包实际编译、计时的代码 |")
    md.append("| 参考 | B08、B13 的标量版本 | 与 NEON 版本对照，内部自检使用 |")
    md.append("| 测试 | B18、B07 的 `compare_blocks_detail` | 定义 "
              "`SNTRUP653_INTERNAL_TEST_HOOKS` 时才编译 |")
    md.append("")
    md.append("## 分块表\n")
    md.append("| 块 | 论文小节 | 内容 | 输入 → 输出 | 路径 | 主要函数 / 宏 | 原文件行号 | 分块文件 |")
    md.append("|---|---|---|---|---|---|---|---|")
    for b in blocks:
        sym = b.get("symbols", "").replace("|", "\\|")
        io_s = b.get("io", "—").replace("|", "\\|")
        md.append("| %s | %s | %s | %s | %s | `%s` | L%d–L%d | [`%s`](%s) |"
                  % (b["id"], b["paper"], b["title"], io_s, b.get("role", ""),
                     sym, b["start"], b["end"], b["file"], url(b)))
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
    md.append("三个平台包各自自包含。根目录 `README.md` 的“三种跑法”给出本地自检、"
              "板卡正确性与性能复现三条命令。\n")
    with open(os.path.join(ROOT, "CODE_MAP.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(md))

    print("owner/repo : %s/%s" % (owner, repo))
    print("ref        : %s" % ref)
    print("blocks     : %d  (lines %d..%d)" % (len(blocks), blocks[0]["start"], blocks[-1]["end"]))
    print("wrote      : CODE_MAP.md")


if __name__ == "__main__":
    main()
