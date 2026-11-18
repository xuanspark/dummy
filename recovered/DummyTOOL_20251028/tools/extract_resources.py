"""静态读取 Qt 资源常量，恢复图片和 QRC，不导入上位机。"""

import ast
import hashlib
import json
import struct
import zlib
from pathlib import Path
from xml.etree import ElementTree


ROOT = Path(__file__).resolve().parents[1]


def extract():
    tree = ast.parse((ROOT / "decompiled/resources.py").read_text(encoding="utf-8"))
    values = {
        node.targets[0].id: ast.literal_eval(node.value)
        for node in tree.body
        if isinstance(node, ast.Assign)
        and isinstance(node.targets[0], ast.Name)
        and isinstance(node.value, ast.Constant)
        and isinstance(node.value.value, bytes)
    }
    nodes = values["qt_resource_struct_v1"]
    names = values["qt_resource_name"]
    payloads = values["qt_resource_data"]
    output = ROOT / "assets"
    output.mkdir(exist_ok=True)
    qrc = ElementTree.Element("RCC")
    section = ElementTree.SubElement(qrc, "qresource", prefix="/")
    manifest = []
    visited = set()

    def walk(index, parent):
        if index in visited or index < 0 or (index + 1) * 14 > len(nodes):
            raise ValueError("资源节点无效")
        visited.add(index)
        offset = index * 14
        name_offset, flags = struct.unpack_from(">IH", nodes, offset)
        count = struct.unpack_from(">H", names, name_offset)[0]
        name = names[name_offset + 6:name_offset + 6 + count * 2].decode("utf-16-be")
        if name in (".", "..") or "/" in name or "\\" in name or ":" in name:
            raise ValueError("资源名称无效")
        relative = parent / name if index else parent
        if flags & 2:
            child_count, first_child = struct.unpack_from(">II", nodes, offset + 6)
            for child in range(first_child, first_child + child_count):
                walk(child, relative)
            return
        data_offset = struct.unpack_from(">I", nodes, offset + 10)[0]
        size = struct.unpack_from(">I", payloads, data_offset)[0]
        data = payloads[data_offset + 4:data_offset + 4 + size]
        if len(data) != size or flags & ~1:
            raise ValueError("资源数据无效或压缩方式不支持")
        if flags & 1:
            uncompressed_size = struct.unpack_from(">I", data)[0]
            data = zlib.decompress(data[4:])
            if len(data) != uncompressed_size:
                raise ValueError("资源解压长度不一致")
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        element = ElementTree.SubElement(section, "file", alias=relative.as_posix())
        element.text = relative.as_posix()
        manifest.append({
            "resource": ":/" + relative.as_posix(),
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })

    walk(0, Path())
    ElementTree.indent(qrc, space="    ")
    ElementTree.ElementTree(qrc).write(
        output / "resources.qrc", encoding="utf-8", xml_declaration=True
    )
    (ROOT / "evidence/assets_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    extract()
