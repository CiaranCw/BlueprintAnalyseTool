param(
  [string] $ProjectUProject = "D:\Projects\AClient\AClient.uproject",
  [int] $MaxMinutes = 12
)
$client = Join-Path $PSScriptRoot 'editor_live_client.ps1'
$deadline = (Get-Date).AddMinutes($MaxMinutes)
while ((Get-Date) -lt $deadline) {
  $r = & $client -ProjectUProject $ProjectUProject -Task status -TimeoutSeconds 20 2>$null
  if ($r -and $r.available) {
    $m = $null
    try { $m = [IO.File]::ReadAllText($r.manifest, (New-Object System.Text.UTF8Encoding($false))) | ConvertFrom-Json } catch {}
    $pv = if ($m -and $m.plugin_version) { $m.plugin_version } elseif ($m -and $m.editor_live.plugin_version) { $m.editor_live.plugin_version } else { 'unknown' }
    Write-Host ("EDITOR_LIVE_UP plugin_version=$pv manifest=" + $r.manifest)
    exit 0
  }
  Start-Sleep -Seconds 20
}
Write-Host "STILL_LOADING"
exit 24
