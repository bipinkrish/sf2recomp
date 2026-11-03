"""Checksum-verified, staged downloads used by optional toolchains."""
from _sf2_tool_constants import DOWNLOADS as K
import hashlib
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import tempfile
import urllib.request
import zipfile


def open_url(url, timeout=K.N_60_39FA):
    import ssl
    context = ssl.create_default_context()
    try:
        import certifi
    except ImportError:
        pass  # Developer checkouts may use the operating system's trust store.
    else:
        context.load_verify_locations(certifi.where())
    request = urllib.request.Request(url, headers={K.S_USER_AGENT_7727: K.S_SF2RECOMP_712F})
    return urllib.request.urlopen(request, timeout=timeout, context=context)


def download(url, destination, checksum, algorithm=K.S_SHA256_9465):
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    def valid(path):
        with path.open(K.S_RB_81FE) as stream:
            return hashlib.file_digest(stream, algorithm).hexdigest() == checksum
    if destination.is_file() and valid(destination):
        return destination
    pending = destination.with_suffix(destination.suffix + K.S_PART_BC6F)
    try:
        print(f"Downloading {destination.name}...", flush=True)
        with open_url(url) as response, pending.open(K.S_WB_E655) as output:
            shutil.copyfileobj(response, output)
        if not valid(pending):
            raise ValueError(f"Checksum mismatch for {destination.name}")
        pending.replace(destination)
    finally:
        pending.unlink(missing_ok=True)
    return destination


def unpack(archive, destination, root=None):
    """Extract into a private staging directory, then publish one complete tree."""
    destination = Path(destination)
    if destination.exists():
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
        stage = Path(temporary) / K.S_PAYLOAD_6964
        stage.mkdir()
        if zipfile.is_zipfile(archive):
            with zipfile.ZipFile(archive) as source:
                for member in source.infolist():
                    name = PurePosixPath(member.filename)
                    if (name.is_absolute() or K.S_EMPTY_E059 in name.parts or K.S_EMPTY_6672 in member.filename
                            or K.S_EMPTY_4266 in member.filename or (member.external_attr >> K.N_16_B17E) & K.N_61440_E33A == K.N_40960_7D8E):
                        raise ValueError(K.S_UNSAFE_TOOLCHAIN_ARCHIVE_MEM_45B3)
                    source.extract(member, stage)
                    path = stage / member.filename
                    mode = member.external_attr >> K.N_16_B17E
                    if mode & K.N_73_9606 and path.is_file():
                        path.chmod(K.N_493_560A)
        else:
            with tarfile.open(archive) as source:
                source.extractall(stage, filter=K.S_DATA_7553)
        tree = stage / root if root else stage
        if root == K.S_EMPTY_15A7:
            children = list(stage.iterdir())
            if len(children) != 1 or not children[0].is_dir():
                raise ValueError(K.S_UNEXPECTED_TOOLCHAIN_ARCHIVE_036A)
            tree = children[0]
        tree.rename(destination)
    return destination
