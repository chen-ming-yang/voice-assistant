param(
  [string]$BaseUrl = "http://192.168.31.27:8080/",
  [string]$Text = "hello",
  [int]$NPredict = 256,
  [bool]$CachePrompt = $false,
  [int]$TimeoutSec = 10
)

# Keep console and HTTP payload UTF-8 to avoid Chinese text turning into '?'.
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)

$endpoint = $BaseUrl.TrimEnd('/')
if ($endpoint -notlike "*/completion") {
  $endpoint = $endpoint + "/completion"
}

$payload = @{ prompt = $Text; n_predict = $NPredict; cache_prompt = $CachePrompt }
$body = $payload | ConvertTo-Json -Compress
$bodyBytes = [System.Text.Encoding]::UTF8.GetBytes($body)

Write-Host ("POST " + $endpoint)
Write-Host ("Body: " + $body)

try {
  $resp = Invoke-RestMethod -Method Post -Uri $endpoint -ContentType "application/json; charset=utf-8" -Body $bodyBytes -TimeoutSec $TimeoutSec
  Write-Host "Raw response:"
  $resp | ConvertTo-Json -Depth 8

  $hasContent = $resp -and $resp.PSObject -and ($resp.PSObject.Properties.Name -contains "content")
  if ($hasContent) {
    Write-Host "Parsed content:"
    Write-Host $resp.content
  }

  if ($resp.PSObject.Properties.Name -contains "stop_type" -and $resp.stop_type -eq "limit") {
    Write-Warning "Response hit token limit. Increase -NPredict (current: $NPredict)."
  }
} catch {
  Write-Host ("Request failed: " + $_.Exception.Message) -ForegroundColor Red
  exit 1
}
