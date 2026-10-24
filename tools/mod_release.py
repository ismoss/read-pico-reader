#!/usr/bin/env python3
"""中文：个人二改的最小 OTA feed：生成、防降级守卫与发布后线上自检。
English: Minimal self-hosted OTA feed for personal mods: build, downgrade guard and live verification.

上游 package_flash/stage_public_flash 把官方域名硬编码在校验断言里，且会连带发布整个官方
刷机站；二改发布只需要 update.json 与固件 bin 两个文件，独立成工具避免改动上游脚本。
Upstream package/stage tools hardcode the official origins in their assertions and ship the
whole official flasher site; a mod release only needs update.json plus the app image, so this
stays separate instead of patching those scripts.
"""
import hashlib
import json
import re
import sys
import urllib.request
from pathlib import Path

ORIGIN = "https://ismoss.github.io/read-pico-reader"
FEED = ORIGIN + "/update.json"
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+-rc\d+$")


def rc_number(version: str) -> int:
    # 设备端版本解析只认 X.Y.Z-rcN；二改发布占用 rc2xx 段，高于官方 rc99 且不与其 rc1xx 冲突。
    # The device parser only accepts X.Y.Z-rcN; mods claim the rc2xx band, above official rc99
    # and clear of a future official rc1xx run.
    match = VERSION_RE.match(version)
    assert match, f"version {version!r} must look like X.Y.Z-rcN"
    return int(version.rsplit("-rc", 1)[1])


def load_image(dist: Path, version: str) -> bytes:
    data = (dist / "Read_Pico.bin").read_bytes()
    assert data[0] == 0xE9, "not an ESP image"
    assert int.from_bytes(data[32:36], "little") == 0xABCD5432, "missing app description"
    embedded = data[48:80].split(b"\0")[0].decode("ascii")
    assert embedded == version, f"image embeds {embedded!r}, requested {version!r}"
    return data


def build(dist: Path, out: Path, version: str, notes: str) -> None:
    assert rc_number(version) >= 200, "self-hosted releases use the rc2xx band"
    data = load_image(dist, version)
    feed = {
        "schema": 1, "version": version, "project": "Read_Pico", "board": "RDP-G01-W",
        "layout": "pico-dual-4m-v1", "minimum_base_version": "0.3.3-rc72",
        "url": f"{ORIGIN}/Pico-update-{version}.bin",
        "size": len(data), "sha256": hashlib.sha256(data).hexdigest(), "notes": notes,
    }
    out.mkdir(parents=True, exist_ok=True)
    (out / f"Pico-update-{version}.bin").write_bytes(data)
    (out / "Pico-update.bin").write_bytes(data)
    (out / "update.json").write_text(json.dumps(feed, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (out / ".nojekyll").touch()
    (out / "index.html").write_text(
        "<!doctype html><meta charset=\"utf-8\"><title>kiikoread mod feed</title>"
        f"<p>Read_Pico mod OTA feed. Latest: {version}. <a href=\"update.json\">update.json</a></p>",
        encoding="utf-8")


def fetch_no_redirect(url: str, timeout: int):
    # OTA 客户端禁用跟随重定向；自检也按同样的规则，任何 302 都算失败。
    # The OTA client never follows redirects; verify the same way and fail on any 302.
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *args, **kwargs):
            return None
    opener = urllib.request.build_opener(NoRedirect)
    return opener.open(url, timeout=timeout)


def guard(version: str) -> None:
    try:
        live = json.load(fetch_no_redirect(FEED, 20))
    except Exception:
        print("no live feed yet; first publication")
        return
    assert rc_number(version) > rc_number(live["version"]), \
        f"live {live['version']} is not older than {version}; refusing a downgrade"


def verify(version: str) -> None:
    with fetch_no_redirect(FEED, 20) as response:
        assert response.geturl() == FEED, f"feed redirected to {response.geturl()}"
        feed = json.load(response)
    assert feed["version"] == version, feed["version"]
    assert feed["url"] == f"{ORIGIN}/Pico-update-{version}.bin", feed["url"]
    assert feed["project"] == "Read_Pico" and feed["board"] == "RDP-G01-W" and feed["layout"] == "pico-dual-4m-v1"
    with fetch_no_redirect(feed["url"], 120) as response:
        assert response.geturl() == feed["url"], f"image redirected to {response.geturl()}"
        data = response.read()
    assert len(data) == feed["size"], (len(data), feed["size"])
    assert hashlib.sha256(data).hexdigest() == feed["sha256"], "sha256 mismatch"
    print(f"PASS: live feed {version}, no redirects, size and sha256 verified")


if __name__ == "__main__":
    command = sys.argv[1]
    if command == "build":
        build(Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4], sys.argv[5])
    elif command == "guard":
        guard(sys.argv[2])
    elif command == "verify":
        verify(sys.argv[2])
    else:
        raise SystemExit(f"unknown command {command!r}")
