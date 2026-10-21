"""Copy and fingerprint complete build-tree runtimes; no service/deploy actions."""
import hashlib
from pathlib import Path
import shutil


def runtime_digest(source, source_directory=None):
    source=Path(source)
    directory=Path(source_directory) if source_directory else source.resolve().parent
    digest=hashlib.sha256(source.read_bytes())
    for library in sorted((directory/'lib/orange-pi-terminal').glob('libterminal_*.so*')):
        if library.is_file() and not library.is_symlink():
            digest.update(library.name.encode());digest.update(library.read_bytes())
    return digest.hexdigest()


def copy_runtime(source, destination, source_directory=None):
    """Copy a build-tree executable with its sibling libraries, preserving links."""
    source=Path(source);destination=Path(destination)
    directory=Path(source_directory) if source_directory else source.resolve().parent
    if destination.exists():
        if runtime_digest(source,directory)!=runtime_digest(destination):
            raise RuntimeError(f'Refusing to overwrite a different runtime checkpoint: {destination}')
        return
    payload=source.read_bytes()
    libraries=directory/'lib/orange-pi-terminal'
    if b'libterminal_ui.so.1\0' in payload:
        for module in ('terminal_music','terminal_system','terminal_ui'):
            if not (libraries/f'lib{module}.so.1').is_file():
                raise RuntimeError(f'Cannot checkpoint incomplete runtime: {libraries}/lib{module}.so.1')
        target=destination.parent/'lib/orange-pi-terminal'
        target.mkdir(parents=True,exist_ok=True)
        for library in libraries.glob('libterminal_*.so*'):
            shutil.copy2(library,target/library.name,follow_symlinks=False)
    destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,destination);destination.chmod(0o755)
