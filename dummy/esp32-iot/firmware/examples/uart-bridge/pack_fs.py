#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Python 等效版 pack.c —— 为 Mongoose MG_ENABLE_PACKED_FS 生成 packed_fs.c。

用法（在 uart-bridge 目录里）:
    python pack_fs.py > main/packed_fs.c            # 或:
    python pack_fs.py -o main/packed_fs.c

默认打包 ./web_root/ 下所有文件为 /web_root/<path> 条目，未 gzip，
与仓库里原 packed_fs.c 的惯例一致。
"""
import argparse
import os
import sys

HEADER = (
    "#include <stddef.h>\n"
    "#include <string.h>\n"
    "#include <time.h>\n"
    "\n"
    "#if defined(__cplusplus)\nextern \"C\" {\n#endif\n"
    "const char *mg_unlist(size_t no);\n"
    "const char *mg_unpack(const char *, size_t *, time_t *);\n"
    "#if defined(__cplusplus)\n}\n#endif\n\n"
)

FOOTER = (
    "static int scmp(const char *a, const char *b) {\n"
    "  while (*a && (*a == *b)) a++, b++;\n"
    "  return *(const unsigned char *) a - *(const unsigned char *) b;\n"
    "}\n"
    "const char *mg_unlist(size_t no) {\n"
    "  return packed_files[no].name;\n"
    "}\n"
    "const char *mg_unpack(const char *name, size_t *size, time_t *mtime) {\n"
    "  const struct packed_file *p;\n"
    "  for (p = packed_files; p->name != NULL; p++) {\n"
    "    if (scmp(p->name, name) != 0) continue;\n"
    "    if (size != NULL) *size = p->size - 1;\n"
    "    if (mtime != NULL) *mtime = p->mtime;\n"
    "    return (const char *) p->data;\n"
    "  }\n"
    "  return NULL;\n"
    "}\n"
)


def emit_file(out, idx: int, rel_path: str, abs_path: str):
    """按 pack.c 的风格输出 v<idx>[] 字节数组。"""
    with open(abs_path, "rb") as f:
        data = f.read()
    out.write(f"static const unsigned char v{idx}[] = {{\n")
    ascii_buf = []
    chunk = 12
    for b in data:
        ch = chr(b) if (32 <= b <= 126 and b != ord('\\')) else '.'
        ascii_buf.append(ch)
        out.write(f" {b:3d},")
        if len(ascii_buf) == chunk:
            out.write(f" // {''.join(ascii_buf)}\n")
            ascii_buf = []
    # 末尾补 0（与 pack.c 行为一致，让文本文件成为 NUL 结尾字符串）
    tail = ''.join(ascii_buf)
    out.write(f" 0 // {tail}\n")
    out.write("};\n\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default="-",
                    help="输出文件 (默认 stdout)")
    ap.add_argument("-r", "--root", default="web_root",
                    help="要打包的根目录 (默认 web_root)")
    args = ap.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir = os.path.join(script_dir, args.root)
    if not os.path.isdir(root_dir):
        sys.stderr.write(f"error: {root_dir} not found\n")
        sys.exit(1)

    # 收集文件（按路径排序，稳定输出）
    files = []
    for dirpath, _, fns in os.walk(root_dir):
        for fn in sorted(fns):
            abs_p = os.path.join(dirpath, fn)
            rel_p = os.path.relpath(abs_p, script_dir).replace("\\", "/")
            files.append((rel_p, abs_p))
    files.sort()

    out = sys.stdout if args.output == "-" else open(args.output, "w",
                                                    encoding="utf-8",
                                                    newline="\n")
    try:
        out.write(HEADER)
        for i, (rel_p, abs_p) in enumerate(files, start=1):
            emit_file(out, i, rel_p, abs_p)

        out.write("static const struct packed_file {\n")
        out.write("  const char *name;\n")
        out.write("  const unsigned char *data;\n")
        out.write("  size_t size;\n")
        out.write("  time_t mtime;\n")
        out.write("} packed_files[] = {\n")
        for i, (rel_p, abs_p) in enumerate(files, start=1):
            mtime = int(os.path.getmtime(abs_p))
            out.write(f'  {{"/{rel_p}", v{i}, sizeof(v{i}), {mtime}}},\n')
        out.write("  {NULL, NULL, 0, 0}\n")
        out.write("};\n\n")
        out.write(FOOTER)
    finally:
        if out is not sys.stdout:
            out.close()

    sys.stderr.write(f"packed {len(files)} files -> {args.output}\n")


if __name__ == "__main__":
    main()
