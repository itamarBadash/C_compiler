import hashlib
import os
import subprocess
import sys
import tarfile
import urllib.request
from compression import zstd

VERSION = "23.1.3"
NAME = "LLVM-%s-Linux-X64.tar.zst" % VERSION
URL = "https://github.com/llvm/llvm-project/releases/download/llvmorg-%s/%s" % (VERSION, NAME)
SHA256 = "14d2f701eb68fb799001bdea6231048f3990690fa2f406d563555ff8f744daba"
TOOLS = {"clang-23": "clang", "lld": "ld.lld"}


def digest(path):
    hash = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            hash.update(chunk)
    return hash.hexdigest()


def wsl_path(path):
    result = subprocess.run(["wsl", "wslpath", "-u", path.replace("\\", "/")], capture_output=True,
                            text=True, check=True)
    return result.stdout.strip()


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit("usage: python fetch_linux_llvm.py <destination> [<downloaded %s>]" % NAME)
    destination = os.path.abspath(sys.argv[1])
    bin_dir = os.path.join(destination, "bin")
    os.makedirs(bin_dir, exist_ok=True)
    archive = sys.argv[2] if len(sys.argv) == 3 else os.path.join(destination, NAME)
    downloaded = len(sys.argv) == 2
    if downloaded:
        print("downloading", URL, flush=True)
        urllib.request.urlretrieve(URL, archive)
    if digest(archive) != SHA256:
        sys.exit("%s does not match LLVM's published SHA-256" % archive)
    options = {zstd.DecompressionParameter.window_log_max: 31}
    found = set()
    with zstd.open(archive, "rb", options=options) as raw:
        with tarfile.open(fileobj=raw, mode="r|") as contents:
            for member in contents:
                parts = member.name.split("/")
                if len(parts) != 3 or parts[1] != "bin" or parts[2] not in TOOLS:
                    continue
                if not member.isfile():
                    continue
                target = os.path.join(bin_dir, TOOLS[parts[2]])
                source = contents.extractfile(member)
                with open(target, "wb") as f:
                    for chunk in iter(lambda: source.read(1 << 20), b""):
                        f.write(chunk)
                found.add(parts[2])
    if found != set(TOOLS):
        sys.exit("the archive is missing " + ", ".join(sorted(set(TOOLS) - found)))
    for name in TOOLS.values():
        subprocess.run(["wsl", "strip", wsl_path(os.path.join(bin_dir, name))], check=True)
    if downloaded:
        os.remove(archive)
    with open(os.path.join(destination, "VERSION"), "w") as f:
        f.write(VERSION + "\n")
    print("LLVM", VERSION, "for Linux is in", bin_dir)


main()
