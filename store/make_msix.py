#!/usr/bin/env python3
"""DeepImage Viewer — Microsoft Store packages (MSIX per architecture + .msixbundle)

Usage: python3 store/make_msix.py [--identity store/identity.json] [--makemsix /path/to/makemsix]
identity.json (from Partner Center → Product identity):
  {"Name": "12345DMIND.DeepImageViewer", "Publisher": "CN=XXXXXXXX-XXXX-...", "PublisherDisplayName": "DMIND"}
"""
import argparse, json, os, re, shutil, subprocess, sys
from PIL import Image, ImageFilter

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(root, 'shell'))

ap = argparse.ArgumentParser()
ap.add_argument('--identity', default=os.path.join(root, 'store', 'identity.json'))
ap.add_argument('--makemsix', default='/home/claude/msix-packaging/.vs/bin/makemsix')
ap.add_argument('--fonts', default=os.path.join(root, 'fonts'))
args = ap.parse_args()

ident = {"Name": "DMIND.DeepImageViewer", "Publisher": "CN=DMIND", "PublisherDisplayName": "DMIND"}
if os.path.exists(args.identity):
    ident.update(json.load(open(args.identity, encoding='utf-8')))
else:
    print('WARNING: store/identity.json not found — using placeholder identity (Store upload will be rejected)')

ver = re.search(r'kVersion = L"([0-9.]+)"', open(os.path.join(root, 'app', 'common.h')).read()).group(1)
ver4 = ver + '.0'   # Store requires revision 0

# ---- thumbnail handler CLSIDs from the shared contract
shell_formats = []
for m in re.finditer(r'\{\{(0x[0-9a-f]+), (0x[0-9a-f]+), (0x[0-9a-f]+), \{([^}]*)\}\}, L"(\.[a-z0-9]+)", (nullptr|L"(\.[a-z0-9]+)"), L"([^"]+)", (true|false)\}',
                     open(os.path.join(root, 'shell', 'shell_contract.h')).read()):
    d1, d2, d3, rest, ext, _, extra, name, req = m.groups()
    b = [int(x, 16) for x in rest.split(',')]
    clsid = '{%08X-%04X-%04X-%02X%02X-%s}' % (int(d1, 16), int(d2, 16), int(d3, 16), b[0], b[1], ''.join('%02X' % x for x in b[2:]))
    shell_formats.append((clsid, [ext] + ([extra] if extra else []), name, req == 'true'))
assert len(shell_formats) >= 19, shell_formats

thumb_exts = {e for _, exts, _, req in shell_formats if not req for e in exts}
images = ['.jpg', '.jpeg', '.jpe', '.jfif', '.png', '.apng', '.gif', '.webp', '.bmp', '.dib', '.ico', '.tif', '.tiff', '.avif',
          '.heic', '.heif', '.jxr', '.wdp', '.dds', '.ppm', '.pgm', '.pnm', '.dng', '.cr2', '.cr3', '.nef', '.arw', '.orf', '.rw2', '.raf']
models_open_only = ['.gltf']  # glTF needs file access (not streams): open-with only in the Store build

def fta(name, display, exts, clsid=None):
    ft = ''.join(f'<uap:FileType>{e}</uap:FileType>' for e in exts)
    th = f'<desktop2:ThumbnailHandler Clsid="{clsid[1:-1]}"/>' if clsid else ''
    return (f'<uap:Extension Category="windows.fileTypeAssociation"><uap:FileTypeAssociation Name="{name}">'
            f'<uap:DisplayName>{display}</uap:DisplayName><uap:Logo>Assets\\FileLogo.png</uap:Logo>'
            f'<uap:SupportedFileTypes>{ft}</uap:SupportedFileTypes>{th}</uap:FileTypeAssociation></uap:Extension>')

exts_xml = [fta('images', 'DeepImage Viewer Image', images), fta('gltf', 'glTF 3D Model', models_open_only)]
classes = []
for clsid, exts, name, req in shell_formats:
    if req: continue
    key = exts[0][1:]
    exts_xml.append(fta(key, name.replace('DeepImage ', '') + ' file', exts, clsid))
    classes.append(f'<com:Class Id="{clsid[1:-1]}" Path="DeepImageThumbs.dll" ThreadingModel="STA" DisplayName="{name}"/>')
exts_xml.append('<com:Extension Category="windows.comServer"><com:ComServer><com:SurrogateServer DisplayName="DeepImage Viewer Thumbnails">'
                + ''.join(classes) + '</com:SurrogateServer></com:ComServer></com:Extension>')

def manifest(arch):
    return f'''<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
         xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
         xmlns:desktop2="http://schemas.microsoft.com/appx/manifest/desktop/windows10/2"
         xmlns:com="http://schemas.microsoft.com/appx/manifest/com/windows10"
         xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
         IgnorableNamespaces="uap desktop2 com rescap">
  <Identity Name="{ident['Name']}" Publisher="{ident['Publisher']}" Version="{ver4}" ProcessorArchitecture="{arch}"/>
  <Properties>
    <DisplayName>DeepImage Viewer</DisplayName>
    <PublisherDisplayName>{ident['PublisherDisplayName']}</PublisherDisplayName>
    <Logo>Assets\\StoreLogo.png</Logo>
  </Properties>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.17763.0" MaxVersionTested="10.0.26100.0"/>
  </Dependencies>
  <Resources>
    <Resource Language="ko-KR"/>
    <Resource Language="en-US"/>
  </Resources>
  <Applications>
    <Application Id="DeepImageViewer" Executable="DeepImage.exe" EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements DisplayName="DeepImage Viewer" Description="Images and 3D, in one place — DMIND"
                          BackgroundColor="transparent" Square150x150Logo="Assets\\Square150x150Logo.png" Square44x44Logo="Assets\\Square44x44Logo.png">
        <uap:DefaultTile Wide310x150Logo="Assets\\Wide310x150Logo.png" Square71x71Logo="Assets\\SmallTile.png" Square310x310Logo="Assets\\LargeTile.png"/>
      </uap:VisualElements>
      <Extensions>
        {chr(10).join("        " + x for x in exts_xml).strip()}
      </Extensions>
    </Application>
  </Applications>
  <Capabilities>
    <rescap:Capability Name="runFullTrust"/>
  </Capabilities>
</Package>
'''

# ---- visual assets from the app icon (transparent cube)
icon = Image.open(os.path.join(root, 'res', 'app_icon_1024.png')).convert('RGBA')
icon = icon.crop(icon.getbbox())
def fit(w, h, frac, sharpen=False):
    c = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    s = min(w, h) * frac
    k = s / max(icon.size)
    im = icon.resize((max(1, round(icon.width * k)), max(1, round(icon.height * k))), Image.LANCZOS)
    if sharpen: im = im.filter(ImageFilter.UnsharpMask(radius=0.5, percent=50, threshold=0))
    c.alpha_composite(im, ((w - im.width) // 2, (h - im.height) // 2))
    return c
assets = {}
for sc in (100, 125, 150, 200, 400):
    f = sc / 100
    assets[f'Square150x150Logo.scale-{sc}.png'] = fit(round(150 * f), round(150 * f), .62)
    assets[f'Wide310x150Logo.scale-{sc}.png'] = fit(round(310 * f), round(150 * f), .62)
    assets[f'SmallTile.scale-{sc}.png'] = fit(round(71 * f), round(71 * f), .70)
    assets[f'LargeTile.scale-{sc}.png'] = fit(round(310 * f), round(310 * f), .60)
    assets[f'Square44x44Logo.scale-{sc}.png'] = fit(round(44 * f), round(44 * f), .92, sc <= 125)
    assets[f'StoreLogo.scale-{sc}.png'] = fit(round(50 * f), round(50 * f), .92)
    assets[f'FileLogo.scale-{sc}.png'] = fit(round(44 * f), round(44 * f), .92, sc <= 125)
for t in (16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 256):
    im = fit(t, t, .96 if t <= 32 else .92, t <= 32)
    assets[f'Square44x44Logo.targetsize-{t}.png'] = im
    assets[f'Square44x44Logo.targetsize-{t}_altform-unplated.png'] = im
    assets[f'Square44x44Logo.targetsize-{t}_altform-lightunplated.png'] = im
    assets[f'FileLogo.targetsize-{t}.png'] = im

out = os.path.join(root, 'dist', 'store')
shutil.rmtree(out, ignore_errors=True)
os.makedirs(out)
env = dict(os.environ, LD_LIBRARY_PATH=os.path.join(os.path.dirname(os.path.dirname(args.makemsix)), 'lib'))
packages = []
for arch, b in (('x64', 'x64'), ('arm64', 'arm64')):
    d = os.path.join(out, 'layout-' + arch)
    os.makedirs(os.path.join(d, 'Assets'))
    os.makedirs(os.path.join(d, 'fonts'))
    os.makedirs(os.path.join(d, 'Help'))
    for f in ('DeepImage.exe', 'dvmodel.dll', 'DeepImageThumbs.dll'):
        shutil.copy(os.path.join(root, 'build', b, f), d)
    for f in ('Pretendard-Regular.otf', 'Pretendard-Medium.otf', 'Pretendard-SemiBold.otf'):
        shutil.copy(os.path.join(args.fonts, f), os.path.join(d, 'fonts'))
    shutil.copy(os.path.join(root, 'licenses', 'LICENSES.txt'), d)
    for f in ('THUMBNAIL-HELP.md', 'ThumbnailFix.cmd', 'ThumbnailFix.ps1'):
        shutil.copy(os.path.join(root, 'docs', 'thumbnail-help', f), os.path.join(d, 'Help'))
    for n, im in assets.items():
        im.save(os.path.join(d, 'Assets', n))
    open(os.path.join(d, 'AppxManifest.xml'), 'w', encoding='utf-8').write(manifest(arch))
    pkg = os.path.join(out, f'DeepImageViewer_{ver4}_{arch}.msix')
    r = subprocess.run([args.makemsix, 'pack', '-d', d, '-p', pkg], env=env, capture_output=True, text=True)
    print(r.stdout[-400:], r.stderr[-800:])
    if r.returncode: sys.exit('pack failed: ' + arch)
    packages.append(pkg)
# One .msix per architecture: Partner Center accepts several packages in a submission and serves the right one.
# (The open-source makemsix only writes flat bundles, so no .msixbundle here.)
for p in packages:
    t = os.path.join(out, 'verify')
    r = subprocess.run([args.makemsix, 'unpack', '-p', p, '-d', t, '-ss'], env=env, capture_output=True, text=True)
    shutil.rmtree(t, ignore_errors=True)
    if r.returncode: sys.exit('verification failed: ' + p + r.stdout[-600:])
for a in ('x64', 'arm64'): shutil.rmtree(os.path.join(out, 'layout-' + a))
for p in packages: print(p, os.path.getsize(p))

