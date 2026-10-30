"""SF2 game-version and game-address tables (the generator + runtime share these conventions).

Same shape as LAS Recomp's tools/game_addresses.py, but self-contained: the versions are
in code instead of runtime/game_addresses/versions.h. ADDRESS is empty until a version is
selected (select_build_id in tools/recompr/gen.py): no named addresses exist yet, and
patches.py stays empty until the runtime is in place.
"""
import re
import sys
from types import SimpleNamespace


class UnknownGameVersion(Exception):
    pass


# {key: (label, main build ID as 32 upper-case hex digits — NSO0 build ID, zero-filled)}
# SF2 uses 16-byte build IDs; Nso.build_id strips the zero padding.
VERSIONS = {
    'update_v131072': ('Shadow Fight 2 (update, v1.1.0)', 'E15FC665EC42684AA6EDD1AE03624D96'),
}


def version_for_build_id(build_id):
    """The version key for a main build ID (hex, any case; Nso.build_id strips trailing zero bytes)."""
    want = build_id.upper()
    padded = want.ljust(64, '0')  # (the NSO header's 32-byte field, zero-filled)
    for key, (label, bid) in VERSIONS.items():
        if padded[:32] == bid and not padded[32:].strip('0'):
            return key
    supported = ', '.join(f'{label} ({bid[:16]})' for label, bid in VERSIONS.values())
    raise UnknownGameVersion(f'These game files are an unsupported version of the game (main build ID '
                             f'{want[:16] or "missing"}). Supported: {supported}.')


# Game addresses, per version (filled in place by select_version; modules that imported it
# see the new values). Definitions live in a table in code until there are enough of them
# to deserve patch tables; patches.py expands @NAME@ references with addr_text().
_TABLES = {
    'update_v131072': {},
}

ADDRESS = SimpleNamespace()
SELECTED = None


def select_version(key):
    global SELECTED
    if key not in VERSIONS:
        raise UnknownGameVersion(f'Unknown game version {key}')
    ADDRESS.__dict__.clear()
    ADDRESS.__dict__.update(_TABLES[key])
    SELECTED = key
    return key


def select_build_id(build_id):
    return select_version(version_for_build_id(build_id))


def addr_text(code):
    """Expand @NAME@ as hex digits inside generated-C addresses, symbols or labels."""
    return re.sub(r'@([A-Z][A-Z0-9_]*)@', lambda m: f'{getattr(ADDRESS, m[1]):x}', code)
