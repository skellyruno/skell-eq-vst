# Read-only check: list installed Skelly EQ VST3 bundles and recent trace.
$roots = @(
  "$env:COMMONPROGRAMFILES\VST3",
  "$env:APPDATA\VST3",
  "$env:LOCALAPPDATA\VST3",
  "$env:USERPROFILE\Documents\VST3"
) | Select-Object -Unique
Write-Host "Searching common VST3 locations for Skelly EQ..."
foreach ($root in $roots) {
  if (Test-Path $root) {
    Get-ChildItem -Path $root -Directory -Recurse -Filter '*Skelly*.vst3' -ErrorAction SilentlyContinue |
      Select-Object FullName, LastWriteTime | Format-List
  }
}
$trace = Join-Path $env:APPDATA 'SkellyAudio\SkellyEQ-v1.4-trace.txt'
if (Test-Path $trace) {
  Write-Host "`nRecent V1.4 trace ($trace):"
  Get-Content $trace -Tail 35
} else {
  Write-Host "`nNo V1.4 trace yet: $trace"
}
