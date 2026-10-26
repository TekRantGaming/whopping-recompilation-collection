# Renders whopping_logo.html to whopping_logo.png (transparent) with headless Edge, then writes the PNG into
# src/logo_png.h for the launcher.
$here = $PSScriptRoot
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$png = Join-Path $here 'whopping_logo.png'
$prof = Join-Path $env:TEMP 'whopping-logo-edge'
$p = Start-Process $edge -ArgumentList '--headless=new', '--disable-gpu', '--hide-scrollbars', "--user-data-dir=$prof",
  '--window-size=1400,440', '--default-background-color=00000000', '--virtual-time-budget=6000', "--screenshot=$png",
  ("file:///" + ((Join-Path $here 'whopping_logo.html') -replace '\\', '/')) -PassThru -WindowStyle Hidden
$p.WaitForExit(60000) | Out-Null
$bytes = [IO.File]::ReadAllBytes($png)
$sb = New-Object Text.StringBuilder
[void]$sb.AppendLine('// The collection''s wordmark (tools/logo/whopping_logo.html, rendered by tools/logo/render_logo.ps1). Generated.')
[void]$sb.AppendLine('#pragma once')
[void]$sb.AppendLine('')
[void]$sb.AppendLine("inline constexpr unsigned char kWhoppingLogoPng[$($bytes.Length)] = {")
for ($i = 0; $i -lt $bytes.Length; $i += 24) {
  $n = [Math]::Min(24, $bytes.Length - $i)
  [void]$sb.AppendLine('    ' + (($bytes[$i..($i + $n - 1)] | ForEach-Object { '0x{0:X2}' -f $_ }) -join ',') + ',')
}
[void]$sb.AppendLine('};')
[IO.File]::WriteAllText((Join-Path (Split-Path -Parent (Split-Path -Parent $here)) 'src\logo_png.h'), $sb.ToString(), (New-Object Text.UTF8Encoding $false))
"logo: $($bytes.Length) bytes"
