"""从旧上位机中提取业务字节码，并直接写出 UTF-8 反编译源码。"""

import argparse
import ast
import hashlib
import importlib.metadata
import json
from pathlib import Path

from PyInstaller.archive.readers import CArchiveReader
from uncompyle6.main import decompile_file
from xdis import load_module
from xdis.disasm import disco


MODULES = (
    "main_NEW_NOTEBOOK",
    "ActionManager",
    "ActivateAPP",
    "mainwindow",
    "resources",
    "subform",
)
PYC_HEADER = bytes((0x42, 0x0D, 0x0D, 0x0A)) + bytes(12)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def recover(executable, output):
    archive = CArchiveReader(str(executable))
    pyz = archive.open_embedded_archive("PYZ-00.pyz")
    folders = {name: output / name for name in ("bytecode", "decompiled", "evidence")}
    for folder in folders.values():
        folder.mkdir(parents=True, exist_ok=True)

    report = {
        "schema_version": 1,
        "source_name": executable.name,
        "source_size": executable.stat().st_size,
        "source_sha256": digest(executable.read_bytes()),
        "python_bytecode": "3.7.0 (3394)",
        "tools": {
            name: importlib.metadata.version(name)
            for name in ("pyinstaller", "uncompyle6", "xdis")
        },
        "modules": [],
        "executable_launched": False,
        "hardware_commands_sent": False,
    }
    for name in MODULES:
        raw = archive.extract(name) if name == MODULES[0] else pyz.extract(name, raw=True)
        (folders["bytecode"] / (name + ".marshal")).write_bytes(raw)
        pyc = folders["bytecode"] / (name + ".pyc")
        pyc.write_bytes(PYC_HEADER + raw)
        source = folders["decompiled"] / (name + ".py")
        # 直接传入 UTF-8 文件流，避免终端管道转码损失中文。
        with source.open("w", encoding="utf-8", newline="\n") as stream:
            decompile_file(str(pyc), outstream=stream)
        if name in ("main_NEW_NOTEBOOK", "ActionManager"):
            version, timestamp, magic, code, is_pypy, source_size, *_ = load_module(str(pyc))
            with (folders["evidence"] / (name + ".dis.txt")).open("w", encoding="utf-8") as stream:
                disco(version, code, timestamp, out=stream, is_pypy=is_pypy,
                      magic_int=magic, source_size=source_size)
        text = source.read_text(encoding="utf-8")
        ast.parse(text, filename=str(source))
        report["modules"].append({
            "name": name,
            "marshal_bytes": len(raw),
            "marshal_sha256": digest(raw),
            "source_sha256": digest(source.read_bytes()),
            "syntax_valid": True,
            "replacement_characters": text.count(chr(0xFFFD)),
            "chinese_characters": sum(0x4E00 <= ord(char) <= 0x9FFF for char in text),
        })
        print(name + ": OK")

    inventory = {
        "executable_entries": list(archive.toc),
        "pyz_modules": list(pyz.toc),
    }
    (folders["evidence"] / "archive_inventory.json").write_text(
        json.dumps(inventory, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (folders["evidence"] / "recovery_manifest.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="提取旧版上位机业务源码，不运行目标程序。")
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    recover(arguments.executable.resolve(), arguments.output.resolve())
