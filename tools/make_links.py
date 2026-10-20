# -*- coding: utf-8 -*-
"""生成核心代码分块索引 CODE_MAP.md，这是仓库里唯一需要提交的产物。

用法：
    python tools/make_links.py                     # 自动读 git remote / HEAD
    python tools/make_links.py --owner alice --repo sntrup653-crt-neon --ref <commit>
    python tools/make_links.py --ref main          # 用分支名（不推荐放进论文）

加 --tex-dir DIR 可以另外生成论文用的 LaTeX 片段（索引表与代码可用性段落），
写到 DIR 里而不写进仓库：
    python tools/make_links.py --tex-dir ../论文片段

链接指向 src/ 下的分块文件（整文件，行号不会漂移）。
src/ 由 tools/split.py 从 packages/sntrup653-crt-cortex-a53/optimized/
crypto_core_multsntrup653.c 原样切分生成，tools/join.py 可校验还原一致。
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


def tex_escape(name):
    return name.replace("_", "\\_\\allowbreak{}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--owner")
    ap.add_argument("--repo")
    ap.add_argument("--ref")
    ap.add_argument("--tex-dir", help="额外生成论文用 LaTeX 片段的目录（不写进仓库）")
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

    # ---------- CODE_MAP.md ----------
    md = []
    md.append("# 核心代码分块索引\n")
    md.append("仓库里同一份核心实现有两种视角：\n")
    md.append("- **可编译、被测评的那一份**：`%s`（%d 行）" % (meta["canonical_path"], meta["total_lines"]))
    md.append("- **按算法层次切开的阅读视图**：`%s/` 下的 %d 个文件（B01–B%02d）"
              % (meta["view_dir"], len(blocks), len(blocks)))
    md.append("")
    md.append("`src/` 由 `tools/split.py` 从上面那份源文件原样切分（只在每块前加注释头，不改代码），"
              "`tools/join.py` 去掉注释头后拼接并比对 SHA-256，可复现原文件。"
              "所以引用某个块时用 `src/` 的文件链接即可，行号不会漂移。\n")
    md.append("三个平台包中的源文件逐字节相同（SHA-256 见各包 `SOURCE_MANIFEST.sha256`）。\n")
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

    # ---------- LaTeX（可选，写到仓库外） ----------
    tex_dir = os.path.abspath(args.tex_dir) if args.tex_dir else None
    if tex_dir:
        os.makedirs(tex_dir, exist_ok=True)
        tex = []
        tex.append("% 由 tools/make_links.py 生成，请勿手改。")
        tex.append("% 依赖 hyperref（论文已通过 \\url 使用）。")
        tex.append("\\begin{table}[htbp]")
        tex.append("\\centering")
        tex.append("\\caption{核心多项式乘法的代码分块索引（源文件共 %d 行，块号对应 \\texttt{src/} 下的一个文件）}"
                   % meta["total_lines"])
        tex.append("\\label{tab:code-map}")
        tex.append("\\footnotesize")
        tex.append("\\begin{tabular}{@{}llp{4.25cm}p{2.95cm}l@{}}")
        tex.append("\\toprule")
        tex.append("块 & 论文小节 & 内容 & 文件 & 行号 \\\\")
        tex.append("\\midrule")
        for b in blocks:
            title = b["title"].replace("&", "\\&").replace("%", "\\%")
            tex.append("%s & %s & \\raggedright %s & \\raggedright \\href{%s}{\\texttt{%s}} & L%d--L%d \\\\"
                       % (b["id"], b["paper"].replace("§", "\\S"), title, url(b),
                          tex_escape(b["file"]), b["start"], b["end"]))
        tex.append("\\bottomrule")
        tex.append("\\end{tabular}")
        tex.append("\\end{table}")
        tex.append("")
        with open(os.path.join(tex_dir, "code_map.tex"), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(tex))

        avail = []
        avail.append("% 由 tools/make_links.py 生成：代码可用性段落，可直接粘进论文。")
        avail.append("本文实现、三个平台的交付包与全部评测脚本见")
        avail.append("\\url{%s}。" % base)
        avail.append("核心多项式乘法共 %d 行，按算法层次切分为 %d 个分块文件，"
                     % (meta["total_lines"], len(blocks)))
        avail.append("逐块的行号与文件对应关系见表~\\ref{tab:code-map}。")
        avail.append("")
        with open(os.path.join(tex_dir, "code_availability.tex"), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(avail))

    print("owner/repo : %s/%s" % (owner, repo))
    print("ref        : %s" % ref)
    print("blocks     : %d  (lines %d..%d)" % (len(blocks), blocks[0]["start"], blocks[-1]["end"]))
    print("wrote      : CODE_MAP.md" + (", %s" % tex_dir if tex_dir else " (未生成 LaTeX；需要时加 --tex-dir)"))


if __name__ == "__main__":
    main()
