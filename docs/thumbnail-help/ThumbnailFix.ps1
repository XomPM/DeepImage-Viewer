# DeepImage Viewer - Explorer thumbnail diagnostics / cache reset
# Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
$ErrorActionPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$ko = (Get-Culture).TwoLetterISOLanguageName -eq 'ko'
function L($k, $e) { if ($ko) { $k } else { $e } }

Add-Type -Namespace DV -Name Assoc -MemberDefinition @'
[DllImport("shlwapi.dll", CharSet = CharSet.Unicode)]
public static extern int AssocQueryStringW(int flags, int str, string assoc, string extra, System.Text.StringBuilder outBuf, ref int outLen);
'@
$thumbCat = '{E357FCCD-A995-4576-B01F-234630154E96}'
function Get-Handler($ext) {
    $sb = New-Object System.Text.StringBuilder 128; $n = 128
    # ASSOCF_INIT_IGNOREUNKNOWN = 0x400, ASSOCSTR_SHELLEXTENSION = 16
    if ([DV.Assoc]::AssocQueryStringW(0x400, 16, $ext, $thumbCat, $sb, [ref]$n) -eq 0) { return $sb.ToString() }
    return $null
}
function Get-ClassName($clsid) {
    $v = (Get-ItemProperty "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid").'(default)'
    if ($v) { $v } else { $clsid }
}
function Test-64($clsid) { Test-Path "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid\InprocServer32" }

$exts = '.stl','.3mf','.obj','.ply','.step','.stp','.iges','.igs','.glb','.gltf','.fbx','.dae','.3ds','.amf','.off','.brep','.svg','.tga','.psd','.gcode','.bgcode'
$report = New-Object System.Collections.Generic.List[string]
$report.Add("DeepImage Viewer thumbnail report  $(Get-Date -Format s)")
$report.Add("Windows $([Environment]::OSVersion.Version)  $env:PROCESSOR_ARCHITECTURE")
$inst = "$env:LOCALAPPDATA\Programs\DeepImage Viewer"
$ver = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\DeepImageViewer').DisplayVersion
$report.Add("DeepImage Viewer: " + $(if ($ver) { $ver } else { L '설치 안 됨' 'not installed' }) + "   DLL: " + (Test-Path "$inst\DeepImageThumbs.dll"))
$report.Add('')

Write-Host ''
Write-Host (L '  DeepImage Viewer — 탐색기 썸네일 진단' '  DeepImage Viewer — Explorer thumbnail check') -ForegroundColor Cyan
Write-Host "  $($report[2])"
Write-Host ''
$problems = 0
foreach ($e in $exts) {
    $h = Get-Handler $e
    $prog = (Get-ItemProperty "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$e\UserChoice").ProgId
    if (-not $h) { $state = L '없음' 'none'; $color = 'Yellow'; $problems++ }
    elseif (-not (Test-64 $h)) { $state = (L '깨짐(32비트 전용) ' 'broken (32-bit only) ') + (Get-ClassName $h); $color = 'Red'; $problems++ }
    else {
        $name = Get-ClassName $h
        if ($name -like 'DeepImage*') { $state = $name; $color = 'Green' } else { $state = $name; $color = 'Yellow'; $problems++ }
    }
    $line = ('  {0,-8} {1}' -f $e, $state) + $(if ($prog) { "   [$prog]" } else { '' })
    Write-Host $line -ForegroundColor $color
    $report.Add($line)
}
Write-Host ''
if (-not $ver) { Write-Host (L '  → DeepImage Viewer 설치 파일로 설치하세요 ("탐색기 썸네일" 켜기).' '  → Install DeepImage Viewer with "Explorer thumbnails" enabled.') -ForegroundColor Yellow }
elseif ($problems) { Write-Host (L '  → 노란색/빨간색 항목이 있습니다. DeepImage Viewer 설치 파일을 다시 실행한 뒤 [C]로 캐시를 비우세요.' '  → Some extensions are not handled by DeepImage. Re-run the installer, then clear the cache with [C].') -ForegroundColor Yellow }
else { Write-Host (L '  → 연결은 정상입니다. 썸네일이 안 보이면 [C]로 캐시를 비우세요.' '  → Registration looks fine. If thumbnails are still missing, clear the cache with [C].') -ForegroundColor Green }

$out = Join-Path $PSScriptRoot 'ThumbnailReport.txt'
$report | Set-Content -Encoding UTF8 $out
Write-Host (L "  보고서: $out" "  Report: $out")
Write-Host ''
Write-Host (L '  [C] 썸네일 캐시 비우기 (탐색기가 잠깐 다시 시작됩니다)   [Q] 끝내기' '  [C] Clear thumbnail cache (Explorer restarts briefly)   [Q] Quit')
$k = Read-Host '  >'
if ($k -match '^[cC]') {
    Stop-Process -Name explorer -Force
    Start-Sleep -Milliseconds 800
    Remove-Item "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\thumbcache_*.db" -Force
    Remove-Item "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\iconcache_*.db" -Force
    Start-Process explorer.exe
    Write-Host (L '  완료. 폴더를 다시 열어 확인하세요.' '  Done. Reopen the folder to check.') -ForegroundColor Green
    Start-Sleep -Seconds 2
}
