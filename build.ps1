# Builds TokenRush into target\debug\.
#   .\build.ps1            tokenrush-tui.exe (needs dependencies\ftxui.hpp + ftxui.o)
#   .\build.ps1 -Tui       the same
#   .\build.ps1 -Sim       the balance simulator, sim.exe (see README)
#   .\build.ps1 -Rebuild   ignore the saved hashes and compile everything again
#
# Incremental: every .cpp becomes its own .o in target\debug\obj\, next to a .hash file. The hash
# covers the .cpp, every header it includes (g++ writes that list into a .d file while compiling)
# and the compiler flags. A file is only compiled again when that hash changes, so editing one
# .cpp recompiles one file, and editing a header recompiles exactly the files that include it.
# The core is compiled once and shared by the game and the simulator. An exe is only relinked
# when one of its objects changed.
#
# Every build's time is printed and appended to target\debug\build-times.log.
param([switch]$Tui, [switch]$Sim, [switch]$Rebuild)
$ErrorActionPreference = "Stop"
if (-not ($Tui -or $Sim)) { $Tui = $true }

$root = $PSScriptRoot
$src  = Join-Path $root "src"
$deps = Join-Path $root "dependencies"
$out  = Join-Path $root "target\debug"
$objDir = Join-Path $out "obj"
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

function Sources([string]$folder) {
    Get-ChildItem -Path (Join-Path $src $folder) -Filter *.cpp | ForEach-Object { $_.FullName }
}

# UNICODE/NOMINMAX: FTXUI requires them on Windows, so everything is built the same way. Every file
# gets the same flags (FTXUI's include path too), so the core's objects are shared by every exe.
$flags = @("-std=c++20", "-O2", "-Wall", "-Wextra", "-DUNICODE", "-D_UNICODE", "-DNOMINMAX", "-I$src", "-I$deps")

$log = Join-Path $out "build-times.log"
$inv = [System.Globalization.CultureInfo]::InvariantCulture   # "38.4", not "38,4" on a Turkish Windows

# ----- Hashing -----
$sha = [System.Security.Cryptography.SHA256]::Create()
$fileHashes = @{}   # path -> hash, so a header shared by many files is read once per build

function HashText([string]$text) {
    $bytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($text))
    return ([System.BitConverter]::ToString($bytes) -replace "-", "").ToLowerInvariant()
}

function HashFile([string]$path) {
    if (-not $fileHashes.ContainsKey($path)) {
        $fileHashes[$path] = if (Test-Path -LiteralPath $path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash } else { "missing" }
    }
    return $fileHashes[$path]
}

# The files a .d file lists: the .cpp first, then every header it pulled in. The format is
# make's: "obj.o: a.cpp b.hpp \<newline> c.hpp", with spaces in paths written as "\ ".
function DepsOf([string]$depFile) {
    $text = [System.IO.File]::ReadAllText($depFile) -replace "\\\r?\n", " "
    $text = $text -replace "^.*?\.o:\s", ""                   # drop the target (paths can have "C:" in them)
    return @($text -split "(?<!\\)\s+" | Where-Object { $_ } | ForEach-Object { $_ -replace "\\ ", " " })
}

# One hash for everything that goes into an object: the flags, then each input file's path and contents.
function ObjectHash([string]$depFile, [string[]]$compileFlags) {
    $parts = @($compileFlags -join " ")
    foreach ($f in (DepsOf $depFile)) { $parts += "$f=" + (HashFile $f) }
    return HashText ($parts -join "`n")
}

# ----- Compiling -----
$compiled = 0
$upToDate = 0

# "src\core\game.cpp" for a full path under the project (works on Windows PowerShell 5.1 too)
function Relative([string]$path) { return $path.Substring($root.Length) -replace '^[\\/]+', '' }

# target\debug\obj\core\game.o for src\core\game.cpp, obj\tools\sim.o for tools\sim.cpp
function ObjectPath([string]$source) {
    $rel = Relative $source
    if ($rel -match '^src[\\/]') { $rel = $rel.Substring(4) }
    return Join-Path $objDir ([System.IO.Path]::ChangeExtension($rel, ".o"))
}

# Compiles $source unless its saved hash still matches. Returns the object's path.
function Compile([string]$source) {
    $obj  = ObjectPath $source
    $dep  = [System.IO.Path]::ChangeExtension($obj, ".d")
    $hash = [System.IO.Path]::ChangeExtension($obj, ".hash")
    $compileFlags = $flags

    if (-not $Rebuild -and (Test-Path $obj) -and (Test-Path $dep) -and (Test-Path $hash)) {
        if ((Get-Content -Raw $hash).Trim() -eq (ObjectHash $dep $compileFlags)) { $script:upToDate++; return $obj }
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $obj) | Out-Null
    Remove-Item -Force -ErrorAction SilentlyContinue $hash   # a failed compile must not look up to date
    Write-Host ("  compiling " + (Relative $source))
    & g++ @compileFlags -MMD -MF $dep -c $source -o $obj
    if ($LASTEXITCODE -ne 0) { throw ("compile failed: " + (Relative $source)) }

    # Hash after compiling: the .d file now lists exactly the headers this compile used.
    $fileHashes.Clear()   # in case a file changed while we were compiling
    Set-Content -NoNewline -Path $hash -Value (ObjectHash $dep $compileFlags)
    $script:compiled++
    return $obj
}

# ----- Building one exe -----
# The seconds of the last successful build of $name, from the log (or $null).
function LastTime([string]$name) {
    if (-not (Test-Path $log)) { return $null }
    $last = Select-String -Path $log -Pattern ("\s" + [regex]::Escape($name) + "\s+([\d.]+)s\s+ok\b") | Select-Object -Last 1
    if ($last) { return [double]::Parse($last.Matches[0].Groups[1].Value, $inv) } else { return $null }
}

function Build([string]$name, [string[]]$sources, [string[]]$linkExtra) {
    $exe = Join-Path $out $name
    $previous = LastTime $name
    Write-Host "Building $name ..."
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $before = $script:compiled
    $status = "ok"

    try {
        $objects = @(foreach ($s in $sources) { Compile $s })

        # Relink only when an object (or an extra link input like ftxui.o) changed.
        $linkHashFile = Join-Path $objDir ($name + ".link.hash")
        $parts = @($linkExtra -join " ")
        foreach ($o in $objects) { $parts += "$o=" + (Get-Content -Raw ([System.IO.Path]::ChangeExtension($o, ".hash"))).Trim() }
        foreach ($l in $linkExtra) { if (Test-Path $l) { $parts += "$l=" + (HashFile $l) } }
        $linkHash = HashText ($parts -join "`n")
        $fresh = (Test-Path $exe) -and (Test-Path $linkHashFile) -and ((Get-Content -Raw $linkHashFile).Trim() -eq $linkHash)

        $count = $script:compiled - $before
        if ($fresh -and -not $Rebuild) {
            # (files can compile and still match the exe, e.g. after undoing a change)
            $note = if ($count -eq 0) { "up to date" } else { "$count compiled, same as the current exe" }
        } else {
            Remove-Item -Force -ErrorAction SilentlyContinue $linkHashFile
            Write-Host "  linking $name"
            & g++ @flags @objects @linkExtra -o $exe
            if ($LASTEXITCODE -ne 0) { throw "link failed: $name" }
            Set-Content -NoNewline -Path $linkHashFile -Value $linkHash
            $note = "{0} of {1} files compiled" -f $count, $objects.Count
        }
    } catch {
        $status = "failed"
        $note = $_.Exception.Message
    }

    $watch.Stop()
    $secs = [math]::Round($watch.Elapsed.TotalSeconds, 1).ToString("0.0", $inv)
    $stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    if ($note -ne "up to date") {   # only builds that did something are worth timing
        Add-Content -Path $log -Value ("{0}  {1,-18} {2,6}s  {3}  ({4})" -f $stamp, $name, $secs, $status, $note)
    }

    if ($status -ne "ok") { Write-Host "Build of $name failed after ${secs}s: $note"; exit 1 }
    $compare = if ($null -ne $previous) { "; last time " + $previous.ToString("0.0", $inv) + "s" } else { "" }
    Write-Host "Built $exe in ${secs}s ($note$compare)"
}

$total = [System.Diagnostics.Stopwatch]::StartNew()

$core = @(Sources "core")
$shell = @(Sources "shell")   # the typed commands, run by the TUI's Console tab

if ($Tui) {
    $ftxuiObj = Join-Path $deps "ftxui.o"
    if (-not (Test-Path $ftxuiObj) -or -not (Test-Path (Join-Path $deps "ftxui.hpp"))) {
        Write-Host "Skipping tokenrush-tui.exe: put ftxui.hpp and ftxui.o in dependencies\ first (see README)."
    } else {
        Build "tokenrush-tui.exe" ($core + $shell + @(Sources "tui")) @($ftxuiObj)
    }
}

if ($Sim) { Build "sim.exe" ($core + @(Join-Path $root "tools\sim.cpp")) @() }

$total.Stop()
Write-Host ("Done in " + $total.Elapsed.TotalSeconds.ToString("0.0", $inv) + "s")
