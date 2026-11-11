# =====================================================================
# pack_for_ubuntu.ps1 -- build a source tarball ready for Ubuntu import
#
# Usage (from repo root):
#   powershell -ExecutionPolicy Bypass -File tools\pack_for_ubuntu.ps1
#   powershell -ExecutionPolicy Bypass -File tools\pack_for_ubuntu.ps1 -OutDir D:\share
#
# Output:
#   <OutDir>\gec6818-gesture-audio-src-<timestamp>.tar.gz
#   <OutDir>\...tar.gz.sha256
#
# Excludes: .git / build / dist / exe / o / wav / logs / bmp
# Keeps   : src include tools client docs Makefile agentLogs
# =====================================================================
[CmdletBinding()]
param(
    [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
# -OutDir may be relative (to repo root) or absolute (e.g. D:\vmShares).
# Join-Path rejects an absolute second segment, so branch before joining.
if ([System.IO.Path]::IsPathRooted($OutDir)) {
    $out = $OutDir
} else {
    $out = Join-Path $root $OutDir
}New-Item -ItemType Directory -Force -Path $out | Out-Null

$stamp  = Get-Date -Format "yyyyMMdd-HHmmss"
$name   = "gec6818-gesture-audio-src-$stamp.tar.gz"
$target = Join-Path $out $name

Push-Location $root
try {
    $tarArgs = @(
        "--exclude=.git",
        "--exclude=./.git",
        "--exclude=./build",
        "--exclude=./dist",
        "--exclude=*.exe",
        "--exclude=*.o",
        "--exclude=*.d",
        "--exclude=*.wav",
        "--exclude=*.mp3",
        "--exclude=*.log",
        "--exclude=*.bmp",
        "-czf", $target, "."
    )
    & tar @tarArgs
    if ($LASTEXITCODE -ne 0) { throw "tar failed (exit $LASTEXITCODE)" }
}
finally {
    Pop-Location
}

$hash = (Get-FileHash -Path $target -Algorithm SHA256).Hash.ToLower()
$shaFile = "${target}.sha256"
"$hash  $name" | Set-Content -Path $shaFile -Encoding ASCII

$sizeMb = [math]::Round((Get-Item $target).Length / 1MB, 2)
Write-Host ""
Write-Host "Package : $target ($sizeMb MB)"
Write-Host "SHA256  : $hash"
Write-Host ""
Write-Host "Import into Ubuntu (pick one):"
Write-Host "  A. scp   : scp `"$target`" <user>@<VM_IP>:~/"
Write-Host "  B. HTTP  : on Windows  py -3.11 -m http.server 8000 --directory `"$out`""
Write-Host "             in VM       wget http://<host-VMnet8-IP>:8000/$name"
Write-Host "  C. shared folder: copy package into VMware shared folder, read from /mnt/hgfs/<name>"
Write-Host ""
Write-Host "Inside VM:"
Write-Host "  mkdir -p ~/gec6818 && tar -xzf ~/$name -C ~/gec6818"
Write-Host "  cd ~/gec6818 && bash tools/armbuild.sh"
Write-Host ""
