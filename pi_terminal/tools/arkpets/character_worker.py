#!/usr/bin/env python3
"""Download one catalog character and convert it ON THIS MACHINE, in a job dir.

No shell commands, privileged installs, SSH, services or executable model files.
Only the fixed Ark-Models repository and hash-pinned Maven runtime are accepted.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.error
import urllib.request

TOOLS = Path(__file__).resolve().parent
RAW = "https://raw.githubusercontent.com/isHarryh/Ark-Models/"


def emit(**data):
    print(json.dumps(data, ensure_ascii=False), flush=True)


def download(url, maximum=32*1024*1024):
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "https" or parsed.hostname not in {"raw.githubusercontent.com", "api.github.com", "repo.maven.apache.org"}:
        raise ValueError("Unexpected download URL")
    headers = {"User-Agent": "pi-terminal-Arkpets/1.0"}
    if parsed.hostname == "raw.githubusercontent.com":
        prefix = "/isHarryh/Ark-Models/"
        if not parsed.path.startswith(prefix):
            raise ValueError("Unexpected Ark-Models path")
        repository_path = parsed.path[len(prefix):]
        commit, separator, file_path = repository_path.partition("/")
        if not separator or not re.fullmatch(r"[a-f0-9]{40}", commit) or not file_path:
            raise ValueError("Unexpected Ark-Models revision/path")
        file_path = urllib.parse.unquote(file_path)
        if any(part in {"", ".", ".."} for part in file_path.split("/")):
            raise ValueError("Unexpected Ark-Models file path")
        fallback = "https://api.github.com/repos/isHarryh/Ark-Models/contents/" + urllib.parse.quote(file_path, safe="/") + "?ref=" + commit
    else:
        fallback = ""

    def read_response(request, limit):
        with urllib.request.urlopen(request, timeout=45) as reply:
            final = urllib.parse.urlparse(reply.url)
            if final.scheme != "https" or final.hostname not in {"raw.githubusercontent.com", "api.github.com", "repo.maven.apache.org"}:
                raise ValueError("Unexpected download redirect")
            chunks = []
            size = 0
            while block := reply.read(65536):
                size += len(block)
                if size > limit:
                    raise ValueError("Download exceeds size limit")
                chunks.append(block)
            return b"".join(chunks)

    try:
        return read_response(urllib.request.Request(url, headers=headers), maximum)
    except (urllib.error.URLError, TimeoutError, ConnectionError):
        if not fallback:
            raise
    # Some networks reset raw.githubusercontent.com while api.github.com stays
    # reachable. GitHub's raw media type serves the exact pinned repository file
    # through the API host, without following its download_url redirect.
    headers["Accept"] = "application/vnd.github.raw+json"
    return read_response(urllib.request.Request(fallback, headers=headers), maximum)


def safe_name(name):
    if not isinstance(name, str) or not name or len(name) > 200 or name in {".", ".."} or any(c in name for c in '/\\\x00'):
        raise ValueError("Unsafe model asset name")
    return name


def catalog(root):
    emit(status="正在读取角色目录")
    commit = json.loads(download("https://api.github.com/repos/isHarryh/Ark-Models/commits/main", 1024*1024))["sha"]
    if not re.fullmatch(r"[a-f0-9]{40}", commit):
        raise ValueError("Invalid repository revision")
    data = json.loads(download(RAW + commit + "/models_data.json", 8*1024*1024))
    data["commit"] = commit
    pending = root/".catalog-new.json"
    pending.write_text(json.dumps(data))
    pending.replace(root/"catalog.json")
    return data


def rows(data):
    return [{"id": k, "name": v.get("name", k) + " · " + v.get("skinGroupName", ""),
             "character": v.get("name", k), "appellation": v.get("appellation", ""),
             "modelName": v.get("skinGroupName", "默认服装"),
             "category": "illustration" if v.get("type")=="DynIllust" else "default" if v.get("style") == "BuildingDefault" else "skin",
             "search": k + " " + v.get("name", "") + " " + v.get("appellation", "")}
            for k, v in data["data"].items() if v.get("type") in {"Operator","DynIllust"}]


def run(command, timeout=240):
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True)
    try:
        output, _ = process.communicate(timeout=timeout)
        if process.returncode:
            raise RuntimeError("转换工具失败：" + output[-2000:])
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()


def install(args):
    if not args.accept_licenses:
        raise ValueError("请先确认角色素材和 Spine 工具授权说明")
    for program in ("java", "javac"):
        if not shutil.which(program):
            raise ValueError("本机需安装 JDK 17（java 和 javac）；程序不会自动 sudo 安装系统包")
    from PIL import Image  # Explicit prerequisite, not an automatic pip/system install.
    if not os.environ.get("DISPLAY") and not shutil.which("xvfb-run"):
        raise ValueError("转换需本机图形会话或 xvfb-run")
    arch = platform.machine().lower()
    if arch not in {"x86_64", "amd64", "aarch64", "arm64"}:
        raise ValueError("转换工具目前仅支持 Linux x86_64 / ARM64")
    if sys.platform != "linux":
        raise ValueError("本转换器当前仅支持 Linux")
    root = args.root
    data = json.loads((root/"catalog.json").read_text()) if (root/"catalog.json").exists() else catalog(root)
    entry = data["data"].get(args.model)
    if not entry or entry.get("type") not in {"Operator","DynIllust"}:
        raise ValueError("请从基建/动态立绘目录选择有效的 Spine 模型；不支持 Cubism model3.json")
    directory="models_illust" if entry["type"]=="DynIllust" else "models"
    model = safe_name(args.model)
    key = hashlib.sha256((data["commit"] + model + ":behaviors-v4:"+args.quality).encode()).hexdigest()
    dest = root/"characters"/key
    if (dest/"animations.json").exists():
        emit(folder=dest.as_uri(), status="已使用本机缓存角色")
        return
    runtime = root/"converter-jars"/arch
    runtime.mkdir(parents=True, exist_ok=True)
    emit(status="正在检查本机转换工具（缺少时自动下载）")
    for path, digest in json.loads((TOOLS/"runtime-lock.json").read_text()).items():
        if "-natives-linux" in path:
            arm = "-natives-linux-arm64" in path
            if arm != (arch in {"aarch64", "arm64"}):
                continue
        target = runtime/Path(path).name
        if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() == digest:
            continue
        content = download("https://repo.maven.apache.org/maven2/" + path)
        if hashlib.sha256(content).hexdigest() != digest:
            raise ValueError("转换工具 SHA256 不匹配，拒绝执行")
        target.write_bytes(content)
    (root/"characters").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".convert-", dir=root) as temp:
        work = Path(temp)
        assets = entry["assetList"]
        names = []
        for ext in (".atlas", ".skel", ".png"):
            value = assets[ext]
            names.extend(value if isinstance(value, list) else [value])
        if len(names) > 10:
            raise ValueError("Model has too many texture pages")
        emit(status="正在下载所选角色原始模型")
        for name in names:
            safe_name(name)
            if Path(name).suffix.lower() not in {".atlas", ".skel", ".png"}:
                raise ValueError("Unsupported asset extension")
            url = RAW + data["commit"] + "/" +directory+ "/" + urllib.parse.quote(model, safe="") + "/" + urllib.parse.quote(name, safe="")
            (work/name).write_bytes(download(url))
        atlas = work/safe_name(assets[".atlas"])
        skeleton = work/safe_name(assets[".skel"])
        # Atlas texture references must never resolve outside the model job.
        atlas_text = atlas.read_text()
        pages = [line.strip() for line in atlas_text.splitlines() if line.strip().lower().endswith(".png")]
        for page in pages:
            safe_name(page)
            if page not in names:
                raise ValueError("Atlas references an undeclared texture")
        total_pixels = 0
        for name in names:
            if name.endswith(".png"):
                with Image.open(work/name) as image:
                    total_pixels += image.width*image.height
                    if total_pixels > 16*1024*1024:
                        raise ValueError("Texture memory exceeds 64 MiB limit")
        # Normalize stems for the exporter, retaining original atlas page names.
        shutil.copyfile(atlas, work/"character.atlas")
        shutil.copyfile(skeleton, work/"character.skel")
        emit(status="正在目标机编译转换器并渲染动作，请稍候")
        cp = str(runtime/"*")
        run(["javac", "-cp", cp, "-d", str(work), str(TOOLS/"ExportSprites.java")])
        command = ["java", "-Xmx512m", "-cp", cp + os.pathsep + str(work), "ExportSprites", str(work/"character"), str(work/"frames"),
                   "illustration" if entry["type"]=="DynIllust" else "operator",args.quality]
        if not os.environ.get("DISPLAY"):
            command = ["xvfb-run", "-a", *command]
        run(command)
        emit(status="正在打包与验证动画图集")
        # Packing can process up to five full action sheets. The Pillow/NumPy
        # fast path is much quicker, but keep a generous bound for slower Pi SD
        # cards and retain a clear timeout instead of leaving an endless job.
        run([sys.executable, str(TOOLS/"pack_sprites.py"), str(work/"frames"), str(work/"packed")], timeout=900)
        info_path = work/"packed/animations.json"
        info = json.loads(info_path.read_text())
        info.update(character=entry.get("name", model), model=model, modelType=entry["type"], sourceCommit=data["commit"])
        total_bytes = 0
        for action in info["actions"]:
            with Image.open(work/"packed"/(action+".png")) as image:
                total_bytes += image.width*image.height*4
        if total_bytes > info["textureBudgetMiB"]*1024*1024:
            raise ValueError("Converted character exceeds selected quality texture budget")
        info_path.write_text(json.dumps(info, ensure_ascii=False, indent=2))
        # Atomic directory rename on same filesystem; no incomplete characters
        # become visible and no existing user data is overwritten.
        (work/"packed").rename(dest)
    emit(folder=dest.as_uri(), status="角色已在目标机转换完成")


def main():
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["catalog", "install"])
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--model", default="")
    parser.add_argument("--accept-licenses", action="store_true")
    parser.add_argument("--quality",choices=["economy","standard","high"],default="standard")
    args = parser.parse_args()
    args.root = args.root.resolve()
    args.root.mkdir(parents=True, exist_ok=True)
    try:
        lock = (args.root/"worker.lock").open("a")
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("另一个角色下载/转换任务正在运行，请稍后重试")
        if args.command == "catalog":
            emit(rows=rows(catalog(args.root)), status="请选择角色下载")
        else:
            install(args)
    except Exception as error:
        emit(error=str(error))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
