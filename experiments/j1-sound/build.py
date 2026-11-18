"""在实验目录生成固定基线的独立副本，编译并归档实验固件。"""
from datetime import datetime
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BASELINE = "755b2cc"
PROJECTS = {
    "ref": ("dummy-ref-core-fw", "Core-STM32F4-fw", "ref"),
    "42": ("dummy-42motor-fw", "Ctrl-Step-STM32-fw", "42motor"),
}


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True, cwd=ROOT)


def prepare():
    source = HERE / "build/source"
    source.mkdir(parents=True, exist_ok=True)
    archive = HERE / "build/baseline.tar"
    with archive.open("wb") as output:
        subprocess.run(
            ["git", "archive", "--format=tar", BASELINE,
             "dummy/dummy-ref-core-fw", "dummy/dummy-42motor-fw",
             "host-pc/backend/tests/firmware"], cwd=ROOT, stdout=output, check=True)
    with tarfile.open(archive) as bundle:
        bundle.extractall(source, filter="data")
    # 复制独立实验文件，不改写原目录或对源码做文本替换。
    for item in (HERE / "overlay").rglob("*"):
        if item.is_file():
            target = source / item.relative_to(HERE / "overlay")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(item, target)
    return source


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package(component, number):
    project, binary, prefix = PROJECTS[component]
    name = f"{prefix}{datetime.now():%m%d}{number:04d}"
    destination = ROOT / "out" / component / name
    destination.mkdir(parents=True, exist_ok=True)
    files = {}
    for suffix in ("bin", "hex", "elf"):
        original = HERE / "build" / component / f"{binary}.{suffix}"
        target = destination / f"{name}.{suffix}"
        if target.exists() and digest(target) != digest(original):
            raise RuntimeError("归档编号已存在且内容不同，请使用新编号")
        shutil.copyfile(original, target)
        files[target.name] = {"bytes": target.stat().st_size, "sha256": digest(target)}
    data = (destination / f"{name}.bin").read_bytes()
    stack = int.from_bytes(data[:4], "little")
    reset = int.from_bytes(data[4:8], "little")
    ram_end = 0x20020000 if component == "ref" else 0x20005000
    if not 0x20000000 < stack <= ram_end or not reset & 1 or not 0x08000000 < reset < 0x08000000 + len(data):
        raise RuntimeError("启动向量检查未通过")
    manifest = {"experiment": "J1 短音可行性和音量测试", "version": "1.2.1-exp1",
                "baseline": BASELINE, "component": component, "files": files,
                "initial_stack": hex(stack), "reset_vector": hex(reset),
                "hardware_tested": False}
    artifacts = HERE / "artifacts"
    artifacts.mkdir(exist_ok=True)
    (artifacts / f"{name}.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"归档完成：{destination}")


def main():
    parser = argparse.ArgumentParser(description="编译独立 J1 发声实验")
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--component", choices=("ref", "42", "all"), default="all")
    parser.add_argument("--number", type=int, default=21)
    args = parser.parse_args()
    if not 0 <= args.number <= 9999:
        parser.error("编译编号必须为 0 到 9999")
    source = prepare()
    if args.prepare_only:
        return
    for component in PROJECTS if args.component == "all" else (args.component,):
        project, _, _ = PROJECTS[component]
        build = HERE / "build" / component
        run("cmake", "-S", source / "dummy" / project, "-B", build, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", "-DJ1_SOUND_EXPERIMENT=ON")
        run("cmake", "--build", build, "-j", "6")
        package(component, args.number)


if __name__ == "__main__":
    main()
