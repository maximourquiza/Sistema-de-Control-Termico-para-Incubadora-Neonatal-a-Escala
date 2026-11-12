$ErrorActionPreference = 'Stop'
$nodeCommand = Get-Command node -ErrorAction SilentlyContinue
$nodePath = if ($nodeCommand) { $nodeCommand.Source } else { Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe' }
if (-not (Test-Path -LiteralPath $nodePath)) {
    throw 'No se encontró Node.js. Instalá Node.js y volvé a ejecutar este archivo.'
}
$serverPath = Join-Path $PSScriptRoot 'server.mjs'
$stdoutPath = Join-Path $env:TEMP ('thermal-web-' + [guid]::NewGuid().ToString('N') + '.log')
$stderrPath = $stdoutPath + '.err'
$server = Start-Process -FilePath $nodePath -ArgumentList @(('"' + $serverPath + '"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        Start-Sleep -Milliseconds 100
        if ($server.HasExited) {
            $serverError = Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue
            throw "El servidor no pudo iniciarse. $serverError"
        }
        if ((Get-Content -LiteralPath $stdoutPath -Raw -ErrorAction SilentlyContinue) -match 'Webapp disponible') { $ready = $true; break }
    }
    if (-not $ready) { throw 'El servidor no respondió al iniciar.' }
    $chromePaths = @(
        (Join-Path $env:ProgramFiles 'Google\Chrome\Application\chrome.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Google\Chrome\Application\chrome.exe'),
        (Join-Path $env:LOCALAPPDATA 'Google\Chrome\Application\chrome.exe')
    )
    $chrome = $chromePaths | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($chrome) { Start-Process -FilePath $chrome -ArgumentList 'http://localhost:8080' }
    else { Write-Host 'Abrí http://localhost:8080 en Chrome de escritorio.' }
    Write-Host 'Webapp: http://localhost:8080'
    Write-Host 'Cerrá PuTTY antes de conectar al HC-05. Dejá esta consola abierta.'
    Read-Host 'Presioná Enter para detener el servidor' | Out-Null
} finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id }
}
