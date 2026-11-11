# =====================================================================
# make_serial_upload.ps1 -- 生成可通过 SecureCRT "Send ASCII" 发送的脚本
#
# 用途：板子没有 rz/sz（Zmodem）时，用 base64 + heredoc 通过串口把文件传上去。
# 板端要求：有 base64；stty 可选（没有则回显较多，但能传）。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_serial_upload.ps1 `
#       -File D:\vmShares\upload\gapp.tgz
#   # 产物：D:\vmShares\upload\gapp.ascii.txt
#
# 板端步骤：
#   1) 在 SecureCRT 里确认提示符空闲；
#   2) Transfer -> Send ASCII，选 gapp.ascii.txt；
#   3) 发送完成后会看到 UPLOAD_DONE 与 /tmp/gapp.tgz 的大小；
#   4) gzip -d /tmp/gapp.tgz && tar xf /tmp/gapp.tar
# =====================================================================
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$File,
    [string]$OutFile
)

$ErrorActionPreference = "Stop"

$src = (Resolve-Path $File).Path
$name = Split-Path $src -Leaf
if (-not $OutFile) {
    $dir = Split-Path $src -Parent
    $base = [IO.Path]::GetFileNameWithoutExtension($name)
    $OutFile = Join-Path $dir "$base.ascii.txt"
}

$bytes = [IO.File]::ReadAllBytes($src)
$b64 = [Convert]::ToBase64String($bytes)

$sb = New-Object System.Text.StringBuilder
# 用 LF 作为行结束符，避免 CR 混进 base64 行
[void]$sb.Append("stty -echo 2>/dev/null || true`n")
[void]$sb.Append("cat > /tmp/$name.b64 <<'B64EOF'`n")
for ($i = 0; $i -lt $b64.Length; $i += 76) {
    $len = [Math]::Min(76, $b64.Length - $i)
    [void]$sb.Append($b64.Substring($i, $len))
    [void]$sb.Append("`n")
}
[void]$sb.Append("B64EOF`n")
[void]$sb.Append("stty echo 2>/dev/null || true`n")
[void]$sb.Append("base64 -d /tmp/$name.b64 > /tmp/$name 2>/dev/null || openssl base64 -d -in /tmp/$name.b64 -out /tmp/$name`n")
[void]$sb.Append("rm -f /tmp/$name.b64`n")
[void]$sb.Append("ls -l /tmp/$name`n")
[void]$sb.Append("echo UPLOAD_DONE`n")

[IO.File]::WriteAllText($OutFile, $sb.ToString(), [Text.Encoding]::ASCII)

$sizeKb = [math]::Round((Get-Item $OutFile).Length / 1KB, 1)
Write-Host "ASCII upload script: $OutFile ($sizeKb KB)"
Write-Host "Target file on board: /tmp/$name"
Write-Host ""
Write-Host "Next:"
Write-Host "  1) SecureCRT: Transfer -> Send ASCII, choose the file above"
Write-Host "  2) Wait for UPLOAD_DONE, then run:"
if ($name -match '\.tgz$') {
    Write-Host "     gzip -d /tmp/$name && tar xf /tmp/$($name -replace '\.tgz$', '.tar')"
} elseif ($name -match '\.tar\.gz$') {
    Write-Host "     tar xzf /tmp/$name"
} else {
    Write-Host "     ls -l /tmp/$name"
}
