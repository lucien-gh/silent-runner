param([Parameter(Mandatory=$true)][string]$Runner)
$ErrorActionPreference = 'Stop'
$testDirectory = Join-Path ([IO.Path]::GetTempPath()) ('silent-runner-shell-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testDirectory) | Out-Null
function Assert-True($Condition, $Message) {
    if (-not $Condition) { throw $Message }
}
function Run-Captured([string]$Executable, [string]$Arguments) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.Arguments = $Arguments
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    try {
        [void]$process.Start()
        $outTask = $process.StandardOutput.ReadToEndAsync()
        $errTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(5000)) { throw 'Process timeout' }
        return @{Code=$process.ExitCode; Output=$outTask.Result; Error=$errTask.Result}
    } finally { $process.Dispose() }
}
try {
    $result = Run-Captured $Runner '--version'
    Assert-True ($result.Code -eq 0 -and $result.Output.Trim() -eq 'SilentRunner 0.1.0' -and $result.Error -eq '') 'PowerShell pipe capture failed'
    $result = Run-Captured $Runner '--unknown'
    Assert-True ($result.Code -eq 2 -and $result.Error.Contains('Expected target first') -and $result.Output -eq '') 'PowerShell error capture failed'
    # Piping a GUI executable forces PowerShell to wait for its output.
    $version = & $Runner --version | Out-String
    Assert-True ($version.Trim() -eq 'SilentRunner 0.1.0' -and $LASTEXITCODE -eq 0) 'Native PowerShell pipeline failed'
    $stdoutPath = Join-Path $testDirectory 'version.txt'
    $stderrPath = Join-Path $testDirectory 'error.txt'
    $cmdScript = Join-Path $testDirectory 'check.cmd'
    $lines = @('@echo off', ('start "" /b /wait "{0}" --version > "{1}" 2> "{2}"' -f $Runner, $stdoutPath, $stderrPath), 'exit /b %errorlevel%')
    [IO.File]::WriteAllLines($cmdScript, $lines, [Text.Encoding]::Default)
    $cmd = Join-Path $env:SystemRoot 'System32\cmd.exe'
    $result = Run-Captured $cmd ('/d /s /c ""{0}""' -f $cmdScript)
    Assert-True ($result.Code -eq 0 -and [IO.File]::ReadAllText($stdoutPath).Trim() -eq 'SilentRunner 0.1.0') 'CMD version redirection failed'
    $lines[1] = 'start "" /b /wait "{0}" --unknown > "{1}" 2> "{2}"' -f $Runner, $stdoutPath, $stderrPath
    [IO.File]::WriteAllLines($cmdScript, $lines, [Text.Encoding]::Default)
    $result = Run-Captured $cmd ('/d /s /c ""{0}""' -f $cmdScript)
    Assert-True ($result.Code -eq 2 -and [IO.File]::ReadAllText($stderrPath).Contains('Expected target first')) 'CMD error and exit code failed'
    Write-Output ('Shell checks passed: PowerShell ' + $PSVersionTable.PSVersion + ' and CMD')
} finally {
    # Remove only this invocation's explicitly verified temporary directory.
    $resolved = [IO.Path]::GetFullPath($testDirectory)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid cleanup path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
