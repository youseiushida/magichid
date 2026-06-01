# =====================================================================================
#  tests/run.ps1  --  host-side regression tests (NO ESP32). Builds the pure firmware
#  logic with host g++ + doctest and checks it against the spec oracles:
#     policy  ->  mh_policy.h    vs  spec/PROTOCOL.md (3.4 / 4)
#     codec   ->  mh_protocol.h  vs  spec/protocol_vectors.txt
#  Optionally also runs the (disposable) Python reference-client codec against the SAME
#  vectors, if Python + reference-client are present. Exit code: 0 = all pass.
# =====================================================================================
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
  $cxx = (Get-Command g++ -ErrorAction SilentlyContinue).Source
  if (-not $cxx) { Write-Host "g++ not found on PATH" -ForegroundColor Red; exit 2 }

  $bin = Join-Path $env:TEMP "mh_tests.exe"
  Write-Host "[build] $cxx -std=c++17 -I. tests/*.cpp"
  & $cxx -std=c++17 -O1 -I. tests/test_main.cpp tests/test_policy.cpp tests/test_parity.cpp tests/test_horipad.cpp -o $bin
  if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED" -ForegroundColor Red; exit 1 }

  & $bin
  $rc = $LASTEXITCODE

  $py = (Get-Command python -ErrorAction SilentlyContinue).Source
  if ($py -and (Test-Path "reference-client/magichid_bridge/protocol.py")) {
    Write-Host "`n[python reference-client parity]"
    & $py tools/test_protocol_parity.py
    if ($LASTEXITCODE -ne 0) { $rc = 1 }
  } else {
    Write-Host "`n(python/reference-client absent -- skipping optional Python parity)" -ForegroundColor DarkGray
  }

  if ($rc -ne 0) { Write-Host "`nTESTS FAILED" -ForegroundColor Red; exit 1 }
  Write-Host "`nALL TESTS PASSED" -ForegroundColor Green
} finally { Pop-Location }
