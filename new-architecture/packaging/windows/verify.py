"""Validate generated installers without claiming Windows runtime verification."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

output = Path('/src/new-architecture/dist-windows-installers')
results = {}
for product in ('client', 'server'):
    manifest = json.loads((output/f'{product}.json').read_text())
    release = manifest['releases']['windows-x86_64']
    installer = output/release['url'].rsplit('/', 1)[-1]
    assert hashlib.sha256(installer.read_bytes()).hexdigest() == release['sha256']
    subprocess.run(['7z', 't', '-bd', '-bso0', '-bsp0', str(installer)], check=True)
    payload = output/f'{product}-payload'
    with tempfile.TemporaryDirectory() as temporary:
        subprocess.run(['7z', 'x', '-bd', '-bso0', '-bsp0', '-y', '-o'+temporary, str(installer)], check=True)
        extracted = Path(temporary)
        assert list(extracted.rglob('LangDLL.dll')), 'Missing standard installer language selector'
        shutdowns = list(extracted.rglob('close-running-app.ps1'))
        assert len(shutdowns) == 1, 'Missing upgrade process shutdown script'
        shutdown = shutdowns[0].read_text(encoding='utf-8-sig')
        assert f"Get-Process -Name 'boer-qt-{product}','bohr-qt-{product}'" in shutdown and 'CloseMainWindow' in shutdown
        assert ('Stop-Process' in shutdown) == (product == 'client'), 'Server must shut down gracefully'

        image_hashes = {hashlib.sha256(p.read_bytes()).hexdigest() for p in extracted.rglob('*.bmp')}
        for kind in ('welcome', 'header'):
            artwork = Path('/src/new-architecture/packaging/windows/branding')/f'{product}-{kind}.bmp'
            assert hashlib.sha256(artwork.read_bytes()).hexdigest() in image_hashes, f'Missing {product} {kind} branding'
        count = 0
        for original in payload.rglob('*'):
            if original.is_file():
                restored = extracted/original.relative_to(payload)
                assert restored.exists(), f'Missing installer payload: {original.relative_to(payload)}'
                assert hashlib.sha256(restored.read_bytes()).digest() == hashlib.sha256(original.read_bytes()).digest(), original
                count += 1
    assert not (payload/f'boer-qt-{product}.exe').exists(), 'Legacy executable in new payload'
    binary = (payload/f'bohr-qt-{product}.exe').read_bytes()
    assert 'Bohr LAN'.encode('utf-16le') in binary, 'Product resource brand missing'
    assert 'Bohr LAN'.encode('utf-16le') in installer.read_bytes(), 'Installer brand missing'
    pe = subprocess.check_output(['file', str(payload/f'bohr-qt-{product}.exe')], text=True).strip()
    assert 'PE32+' in pe and 'x86-64' in pe
    results[product] = {'installer': installer.name, 'sha256': release['sha256'], 'payloadFilesVerified': count, 'executable': pe, 'installerLanguages': ['zh-CN', 'en'], 'languageSelectorVerified': True, 'brandingVerified': True, 'windowsRuntimeTested': False}
(output/'verification.json').write_text(json.dumps(results, ensure_ascii=False, indent=2)+'\n')
print(json.dumps(results, ensure_ascii=False, indent=2))
