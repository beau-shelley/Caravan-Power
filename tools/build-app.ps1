# Build the Caravan Android app on this PC (instead of GitHub Actions) and hand it out.
#
#   powershell -ExecutionPolicy Bypass -File tools\build-app.ps1            # next build number
#   powershell -ExecutionPolicy Bypass -File tools\build-app.ps1 -Build 40  # pick the number
#
# Needs JDK 17 and the Android SDK (platform 35, build-tools 35.0.0); ANDROID_HOME
# or android\local.properties (sdk.dir=...) tells Gradle where the SDK is.
#
# The build number becomes the app's version (0.2.<n>, versionCode 1000+n) and must
# be higher than the last one, or the phone won't install it over the old app.
# If the GitHub CLI is signed in (gh auth login), the APK is published as release
# app-build-<n>, so the app's own updater (Setup > Update) offers it. Otherwise
# it's copied to OneDrive\caravan-monitor.apk to install from the phone.
param([int]$Build = 0)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$repo = "beau-shelley/Caravan-Power"
$gh = $null
if (Get-Command gh -ErrorAction SilentlyContinue) {
  gh auth status 2>$null | Out-Null
  if ($LASTEXITCODE -eq 0) { $gh = "gh" }
}

# Last build number: the newest app-build-<n> release, or the last local build.
$stamp = Join-Path $root "android\.last-build"
if ($Build -le 0) {
  $last = 0
  if (Test-Path $stamp) { $last = [int](Get-Content $stamp) }
  if ($gh) {
    $tags = gh release list --repo $repo --limit 100 --json tagName --jq '.[].tagName' | Where-Object { $_ -match '^app-build-(\d+)$' }
    foreach ($t in $tags) { $n = [int]($t -replace 'app-build-', ''); if ($n -gt $last) { $last = $n } }
  }
  $Build = [Math]::Max($last, 36) + 1
}
Write-Host "Building app 0.2.$Build ..."

Push-Location (Join-Path $root "android")
try {
  $env:GITHUB_RUN_NUMBER = "$Build"
  .\gradlew.bat --no-daemon assembleDebug
  if ($LASTEXITCODE -ne 0) { throw "Gradle build failed" }
} finally {
  Remove-Item Env:GITHUB_RUN_NUMBER -ErrorAction SilentlyContinue
  Pop-Location
}
Set-Content $stamp $Build

$sha = (git -C $root rev-parse HEAD).Trim()
$apk = Join-Path $root "android\caravan-monitor-$($sha.Substring(0,7)).apk"
Copy-Item (Join-Path $root "android\app\build\outputs\apk\debug\app-debug.apk") $apk -Force
Write-Host "APK: $apk"

$published = $false
if ($gh) {
  # The release must point at a commit GitHub has, so only publish pushed commits.
  $onRemote = (git -C $root branch -r --contains $sha) -ne $null
  if ($onRemote) {
    $subject = (git -C $root log -1 --format=%s).Trim()
    gh release create "app-build-$Build" $apk --repo $repo --target $sha `
      --title "Caravan app build $Build" --notes "$subject`n`nBuilt on this PC from commit $($sha.Substring(0,7))."
    if ($LASTEXITCODE -eq 0) { $published = $true; Write-Host "Published release app-build-$Build: the app offers it under Setup > Update." }
  } else {
    Write-Host "Commit $($sha.Substring(0,7)) isn't pushed, so no release."
  }
}
if (-not $published) {
  $od = $env:OneDrive
  if (-not $od) { $od = Join-Path $env:USERPROFILE "OneDrive" }
  $dest = Join-Path $od "caravan-monitor.apk"
  Copy-Item $apk $dest -Force
  Write-Host "Copied to $dest - install it from the OneDrive app on the phone."
}
