"""The native LAN entry is enabled by default and controlled by the shipped INI."""
from pathlib import Path
import subprocess,tempfile,unittest
from test_settings import compile_harness
class NativeLanConfigTests(unittest.TestCase):
    def test_default_disable_reenable_and_invalid_value(self):
        source=r"""
#include <assert.h>
#include "settings.h"
int main(int argc,char **argv) {
    assert(g_settings.allow_sparring_lan_multiplayer && !g_settings.network_enabled);
    assert(settings_load_file(argv[1]) && !g_settings.allow_sparring_lan_multiplayer);
    assert(settings_load_file(argv[2]) && !g_settings.allow_sparring_lan_multiplayer);
    assert(settings_load_file(argv[3]) && g_settings.allow_sparring_lan_multiplayer);
    assert(!g_settings.network_enabled);
}
"""
        with tempfile.TemporaryDirectory(prefix='sf2-native-lan-config-') as d:
            root=Path(d);(root/'test.c').write_text(source);compile_harness(root/'test.c',root/'test')
            paths=[]
            for name,value in [('off','false'),('invalid','typo'),('on','true')]:
                p=root/(name+'.ini');p.write_text('[gameplay]\nallow_sparring_lan_multiplayer = '+value+'\n');paths.append(str(p))
            subprocess.run([str(root/'test'),*paths],check=True)
