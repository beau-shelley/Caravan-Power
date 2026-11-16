# Copies the Wi-Fi this PC is connected to into firmware/hub/src/secrets.h
# (WIFI_SSID / WIFI_PASS), so a bench hub joins the same network.
# The password is read from Windows' saved profile and written straight into
# the git-ignored secrets.h; it is never printed.
#   powershell -ExecutionPolicy Bypass -File firmware\hub\tools\wifi-from-pc.ps1
$ErrorActionPreference = 'Stop'
$secrets = Join-Path $PSScriptRoot '..\src\secrets.h'
if (-not (Test-Path $secrets)) { Copy-Item (Join-Path $PSScriptRoot '..\src\secrets.example.h') $secrets }

$ssidLine = netsh wlan show interfaces | Where-Object { $_ -match '^\s*SSID\s*:' } | Select-Object -First 1
if (-not $ssidLine) { throw 'This PC is not connected to Wi-Fi.' }
$ssid = ($ssidLine -split ':', 2)[1].Trim()

$keyLine = netsh wlan show profile name="$ssid" key=clear | Where-Object { $_ -match '^\s*Key Content\s*:' } | Select-Object -First 1
if (-not $keyLine) { throw "No saved password for '$ssid' (open network, or run this in an admin PowerShell)." }
$key = ($keyLine -split ':', 2)[1].Trim()

function CString([string]$s) { '"' + $s.Replace('\', '\\').Replace('"', '\"') + '"' }
$text = Get-Content $secrets -Raw
$text = [regex]::Replace($text, '(?m)^#define WIFI_SSID .*$', { param($m) '#define WIFI_SSID ' + (CString $ssid) })
$text = [regex]::Replace($text, '(?m)^#define WIFI_PASS .*$', { param($m) '#define WIFI_PASS ' + (CString $key) })
[IO.File]::WriteAllText((Resolve-Path $secrets), $text)
"secrets.h now uses Wi-Fi '$ssid' (password filled in, $($key.Length) characters)."
