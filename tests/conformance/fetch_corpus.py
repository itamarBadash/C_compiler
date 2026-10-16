import hashlib
import os
import shutil
import sys
import tarfile
import urllib.request
import zipfile

CORPUS = [
    ("c-testsuite",
     "https://github.com/c-testsuite/c-testsuite/archive/5c7275656d751de0e68b2d340a95b5681858ed07.zip",
     "86117d3fcdc69ab827caee50d6bf2a1d08aa8e8a0fd963fbbdb38dec4eea3d28"),
    ("lua", "https://www.lua.org/ftp/lua-5.4.9.tar.gz",
     "2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6"),
    ("lua-tests", "https://www.lua.org/tests/lua-5.4.9-tests.tar.gz",
     "7d971845f545ffc09fbb3128a86b2c6524161c70d0fdf0154a16e8c00c343fca"),
    ("sqlite", "https://www.sqlite.org/2026/sqlite-amalgamation-3540000.zip",
     "68e913b0fe8ec6b4e5f391c594a28872884e80d6cd042a1e50c957be46b5aa4d"),
]


def sha256(path):
    hash = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            hash.update(chunk)
    return hash.hexdigest()


def unpack(archive, folder):
    staging = folder + ".unpacking"
    shutil.rmtree(staging, ignore_errors=True)
    if archive.endswith(".zip"):
        with zipfile.ZipFile(archive) as contents:
            contents.extractall(staging)
    else:
        with tarfile.open(archive) as contents:
            contents.extractall(staging, filter="data")
    top = os.listdir(staging)
    shutil.rmtree(folder, ignore_errors=True)
    os.rename(os.path.join(staging, top[0]) if len(top) == 1 else staging, folder)
    shutil.rmtree(staging, ignore_errors=True)


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit("usage: python fetch_corpus.py <destination> [<folder holding the downloads>]")
    destination = os.path.abspath(sys.argv[1])
    os.makedirs(destination, exist_ok=True)
    for name, url, expected in CORPUS:
        file_name = url.rsplit("/", 1)[1]
        archive = os.path.join(sys.argv[2], file_name) if len(sys.argv) == 3 else None
        if archive is None or not os.path.exists(archive):
            archive = os.path.join(destination, file_name)
            print("downloading", url, flush=True)
            urllib.request.urlretrieve(url, archive)
        if sha256(archive) != expected:
            sys.exit("%s does not match its pinned SHA-256" % archive)
        unpack(archive, os.path.join(destination, name))
        if os.path.dirname(archive) == destination:
            os.remove(archive)
        print(name, "is in", os.path.join(destination, name), flush=True)


main()
