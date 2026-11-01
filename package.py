#!/usr/bin/env python3
# DeepImage Viewer — build the release installers (x64, ARM64) and portable zips
import os, struct, sys, zipfile
VERSION = '1.0.2'
root = os.path.dirname(os.path.abspath(__file__))
fonts = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'fonts')
dist = os.path.join(root, 'dist')
os.makedirs(dist, exist_ok=True)
BIN = ('DeepImage.exe', 'dvmodel.dll', 'DeepImageThumbs.dll')
FONTS = ('Pretendard-Regular.otf', 'Pretendard-Medium.otf', 'Pretendard-SemiBold.otf')
for arch in ('x64', 'arm64'):
    b = os.path.join(root, 'build', arch)
    payload = os.path.join(dist, 'payload.zip')
    with zipfile.ZipFile(payload, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in BIN:
            z.write(os.path.join(b, f), f'{arch}/{f}')
        for f in FONTS:
            z.write(os.path.join(fonts, f), f'common/fonts/{f}')
        z.write(os.path.join(root, 'licenses', 'LICENSES.txt'), 'common/LICENSES.txt')
    stub = open(os.path.join(b, 'DeepImageSetup.exe'), 'rb').read()
    data = open(payload, 'rb').read()
    out = os.path.join(dist, f'DeepImage-Viewer-{VERSION}-Setup-{arch}.exe')
    with open(out, 'wb') as f:
        f.write(stub); f.write(data); f.write(b'DVPAYLD1'); f.write(struct.pack('<Q', len(stub)))
    os.remove(payload)
    print(out, os.path.getsize(out))
    # portable zip (no Explorer integration)
    pz = os.path.join(dist, f'DeepImage-Viewer-{VERSION}-Portable-{arch}.zip')
    with zipfile.ZipFile(pz, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in ('DeepImage.exe', 'dvmodel.dll'):
            z.write(os.path.join(b, f), f'DeepImage Viewer/{f}')
        for f in FONTS:
            z.write(os.path.join(fonts, f), f'DeepImage Viewer/fonts/{f}')
        z.write(os.path.join(root, 'licenses', 'LICENSES.txt'), 'DeepImage Viewer/LICENSES.txt')
    print(pz, os.path.getsize(pz))
