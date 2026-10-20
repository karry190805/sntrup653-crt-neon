# -*- coding: utf-8 -*-
"""生成核心代码分块索引（CODE_MAP.md）与论文用 LaTeX 片段。

用法：
    python tools/make_links.py                     # 自动读 git remote / HEAD
    python tools/make_links.py --owner alice --repo sntrup653-crt-neon --ref <commit>
    python tools/make_links.py --ref main          # 用分支名（不推荐放进论文）

链接永远指向同一个文件：packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c
（三个平台包中的该文件逐字节相同，见 packages/*/SOURCE_MANIFEST.sha256）。
"""
import argparse
import json
import os
import re
import subprocess
import sys

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


def guess_ref():
    return sh("git", "rev-parse", "HEAD") or "main"


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
    ap.add_argument("--file", default=None, help="覆盖默认的核心源文件路径")
    args = ap.parse_args()

    meta, blocks = load_blocks()
    path = args.file or meta["canonical_path"]
    owner, repo = args.owner, args.repo
    if not owner or not repo:
        g_owner, g_repo = guess_repo()
        owner = owner or g_owner or "OWNER"
        repo = repo or g_repo or "sntrup653-crt-neon"
    ref = args.ref or guess_ref()

    base = "https://github.com/%s/%s" % (owner, repo)
    blob = "%s/blob/%s/%s" % (base, ref, path)

    def url(b):
        return "%s#L%d-L%d" % (blob, b["start"], b["end"])

    # ---------- CODE_MAP.md ----------
    md = []
    md.append("# 核心代码分块索引\n")
    md.append("论文第 3 节的核心多项式乘法实现在同一个源文件里。三个平台交付包中的该文件逐字节相同"
              "（SHA-256 见各包的 `SOURCE_MANIFEST.sha256`），因此全部分块都指向同一份代码：\n")
    md.append("```text\n%s   （共 %d 行）\n```\n" % (path, meta["total_lines"]))
    md.append("链接格式为 GitHub 行号永久链接 `.../blob/<commit>/<file>#L<起>-L<止>`，"
              "当前固定到提交 `%s`；代码若再变动，重新运行 `python tools/make_links.py --ref <新提交>` 即可。\n" % ref)
    md.append("| 块 | 论文小节 | 内容 | 主要函数 / 宏 | 行号 | 链接 |")
    md.append("|---|---|---|---|---|---|")
    for b in blocks:
        sym = b.get("symbols", "").replace("|", "\\|")
        md.append("| %s | %s | %s | `%s` | L%d–L%d | [打开](%s) |"
                  % (b["id"], b["paper"], b["title"], sym, b["start"], b["end"], url(b)))
    md.append("")
    md.append("## 纯文本链接（可直接粘进邮件或投稿系统）\n")
    md.append("```text")
    md.append("%s" % base)
    for b in blocks:
        md.append("%s  %s  %s  L%d-L%d" % (b["id"], b["title"], url(b), b["start"], b["end"]))
    md.append("```")
    md.append("")
    md.append("## 复现入口\n")
    md.append("每个平台包都是自包含的，按 `README.md` 的“快速开始”一节解压后执行 "
              "`sudo bash ./run_on_target.sh` 即可重跑编译、正确性与计时。\n")
    with open(os.path.join(ROOT, "CODE_MAP.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(md))

    # ---------- LaTeX ----------
    os.makedirs(os.path.join(ROOT, "paper"), exist_ok=True)
    tex = []
    tex.append("% 由 tools/make_links.py 生成，请勿手改。")
    tex.append("% 依赖 hyperref（论文已通过 \\url 使用）。")
    tex.append("\\begin{table}[htbp]")
    tex.append("\\centering")
    tex.append("\\caption{核心多项式乘法的代码分块索引（\\texttt{crypto\\_core\\_multsntrup653.c}，共 %d 行）}"
               % meta["total_lines"])
    tex.append("\\label{tab:code-map}")
    tex.append("\\small")
    tex.append("\\begin{tabular}{@{}llrl@{}}")
    tex.append("\\toprule")
    tex.append("块 & 论文小节 & 行号 & 内容 \\\\")
    tex.append("\\midrule")
    for b in blocks:
        title = b["title"].replace("&", "\\&").replace("%", "\\%").replace("_", "\\_")
        tex.append("%s & %s & \\href{%s}{L%d--L%d} & %s \\\\"
                   % (b["id"], b["paper"].replace("§", "\\S"), url(b), b["start"], b["end"], title))
    tex.append("\\bottomrule")
    tex.append("\\end{tabular}")
    tex.append("\\end{table}")
    tex.append("")
    with open(os.path.join(ROOT, "paper", "code_map.tex"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(tex))

    avail = []
    avail.append("% 由 tools/make_links.py 生成：代码可用性段落，可直接粘进论文。")
    avail.append("本文实现、三个平台的交付包与全部评测脚本见")
    avail.append("\\url{%s}。" % base)
    avail.append("核心多项式乘法位于 \\texttt{%s}（共 %d 行），"
                 % (path.replace("_", "\\_"), meta["total_lines"]))
    avail.append("各分块与行号的对应关系见表~\\ref{tab:code-map}。")
    avail.append("")
    with open(os.path.join(ROOT, "paper", "code_availability.tex"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(avail))

    print("owner/repo : %s/%s" % (owner, repo))
    print("ref        : %s" % ref)
    print("blocks     : %d  (lines %d..%d)" % (len(blocks), blocks[0]["start"], blocks[-1]["end"]))
    print("wrote      : CODE_MAP.md, paper/code_map.tex, paper/code_availability.tex")


if __name__ == "__main__":
    main()
