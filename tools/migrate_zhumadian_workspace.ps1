param([switch]$Execute)
$ErrorActionPreference = 'Stop'
$source = 'D:\Project\yhq\htengine'
$target = 'D:\Project\Zhumadian'
$backup = Join-Path $target 'Backup\yhq_20261012\htengine'
$engine = Join-Path $target 'XNBYEngine'
$app = Join-Path $target 'Application'
$delivery = Join-Path $source 'work\releases\landscape_apps_20261011'
function Assert-InRoot([string]$Path, [string]$Root) {
  $absolute = [IO.Path]::GetFullPath($Path).TrimEnd('\')
  $boundary = [IO.Path]::GetFullPath($Root).TrimEnd('\')
  if (-not ($absolute -eq $boundary -or $absolute.StartsWith($boundary + '\', [StringComparison]::OrdinalIgnoreCase))) {
    throw "Path escapes intended root: $absolute"
  }
}
function Copy-Code([string]$From, [string]$To) {
  Assert-InRoot $From $source
  Assert-InRoot $To $target
  New-Item -ItemType Directory -Path $To -Force | Out-Null
  & robocopy $From $To /E /XJ /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XD node_modules __pycache__ .git target dist .vs /XF *.pyc *.obj *.pdb *.ilk *.exe *.dll *.lib *.spv | Out-Null
  if ($LASTEXITCODE -ge 8) { throw "Copy failed: $From" }
}
function Move-Checked([string]$From, [string]$To) {
  Assert-InRoot $From $target
  Assert-InRoot $To $target
  if (Test-Path -LiteralPath $To) { throw "Destination exists: $To" }
  New-Item -ItemType Directory -Path (Split-Path -Parent $To) -Force | Out-Null
  Move-Item -LiteralPath $From -Destination $To
}
function Link-Checked([string]$Path, [string]$Destination) {
  Assert-InRoot $Path $target
  Assert-InRoot $Destination $target
  if (Test-Path -LiteralPath $Path) { throw "Link path exists: $Path" }
  New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
  New-Item -ItemType Junction -Path $Path -Target $Destination | Out-Null
}
Assert-InRoot $source 'D:\Project\yhq'
Assert-InRoot $backup $target
if (-not $Execute) {
  Write-Output "Copy engine code to $engine; application code and verified 0.8 delivery to $app."
  Write-Output 'Centralize runtime GIS, layers, cache and all documents; preserve remaining old workspace in Backup.'
  Write-Output 'No original data or historical evidence is deleted. Old root cleanup follows relocated runtime verification.'
  exit 0
}
if (Test-Path -LiteralPath $backup) { throw 'Migration backup already exists; inspect before retry.' }
if (-not (Test-Path -LiteralPath (Join-Path $delivery 'ht_landscape_editor_0_8.exe'))) { throw 'Verified delivery missing.' }
if ((Test-Path -LiteralPath (Join-Path $app 'Runtime\current'))) { throw 'Application runtime already exists; inspect partial migration.' }
$modules = @('core','rhi','renderer','scene','asset','window','coordinate','measure','gis','cloud','shaders','cmake','tests','tools','vcpkg-ports','apps')
foreach ($name in $modules) { Copy-Code (Join-Path $source $name) (Join-Path $engine $name) }
foreach ($name in @('CMakeLists.txt','vcpkg.json','.clang-format','.gitignore','README.md','ARCHITECTURE.md','AGENTS.md')) {
  Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $engine $name)
}
Copy-Code (Join-Path $source 'apps\showcase_demo') (Join-Path $app 'native')
Copy-Code (Join-Path $source 'apps\landscape_web') (Join-Path $app 'web')
# dist is an intentional prebuilt user entry, copied separately from web source.
& robocopy (Join-Path $source 'apps\landscape_web\dist') (Join-Path $app 'web\dist') /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'Viewer dist copy failed.' }
Copy-Code (Join-Path $source 'tools') (Join-Path $app 'tools')
Copy-Code (Join-Path $source 'docs') (Join-Path $target 'Document\Engine')
& robocopy (Join-Path $source '参考') (Join-Path $target 'Document\Reference') /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'Reference media copy failed.' }
New-Item -ItemType Directory -Path (Split-Path -Parent $backup) -Force | Out-Null
# One native PowerShell move, with exact checked source/destination. Running processes are not killed.
New-Item -ItemType Directory -Path $backup -Force | Out-Null
foreach ($entry in Get-ChildItem -LiteralPath $source -Force) {
  $destination = Join-Path $backup $entry.Name
  Assert-InRoot $entry.FullName 'D:\Project\yhq'
  Assert-InRoot $destination $target
  Move-Item -LiteralPath $entry.FullName -Destination $destination
}
$delivery = Join-Path $backup 'work\releases\landscape_apps_20261011'
$runtime = Join-Path $app 'Runtime\current'
Move-Checked $delivery $runtime
Move-Checked (Join-Path $runtime 'assets\驻马店园区') (Join-Path $target 'Assets\3DGIS\驻马店园区')
Move-Checked (Join-Path $runtime 'assets') (Join-Path $target 'Assets\Runtime\assets')
Link-Checked (Join-Path $target 'Assets\Runtime\assets\驻马店园区') (Join-Path $target 'Assets\3DGIS\驻马店园区')
Move-Checked (Join-Path $runtime 'data') (Join-Path $target 'Assets\Runtime\data')
Move-Checked (Join-Path $runtime 'projects') (Join-Path $target 'Assets\DLG')
Move-Checked (Join-Path $runtime 'work\zhumadian_native_chunk_cache') (Join-Path $target 'Assets\Cache\zhumadian_native_chunk_cache')
Link-Checked (Join-Path $runtime 'assets') (Join-Path $target 'Assets\Runtime\assets')
Link-Checked (Join-Path $runtime 'data') (Join-Path $target 'Assets\Runtime\data')
Link-Checked (Join-Path $runtime 'projects') (Join-Path $target 'Assets\DLG')
Link-Checked (Join-Path $runtime 'work\zhumadian_native_chunk_cache') (Join-Path $target 'Assets\Cache\zhumadian_native_chunk_cache')
Link-Checked (Join-Path $engine 'docs') (Join-Path $target 'Document\Engine')
Link-Checked (Join-Path $engine 'assets') (Join-Path $target 'Assets\Runtime\assets')
Link-Checked (Join-Path $engine 'data') (Join-Path $target 'Assets\Runtime\data')
Link-Checked (Join-Path $engine 'projects') (Join-Path $target 'Assets\DLG')
Link-Checked (Join-Path $engine '参考') (Join-Path $target 'Document\Reference')
Link-Checked (Join-Path $app 'assets') (Join-Path $target 'Assets\Runtime\assets')
Link-Checked (Join-Path $app 'data') (Join-Path $target 'Assets\Runtime\data')
Link-Checked (Join-Path $app 'projects') (Join-Path $target 'Assets\DLG')
Link-Checked (Join-Path $app 'apps\landscape_web') (Join-Path $app 'web')
Link-Checked (Join-Path $app 'runtime') (Join-Path $runtime 'runtime')
foreach ($name in @('osgb_exporter_build','osg-build-min')) {
  Link-Checked (Join-Path $app ('work\' + $name)) (Join-Path $runtime ('work\' + $name))
}
Link-Checked (Join-Path $app 'work\zhumadian_native_chunk_cache') (Join-Path $target 'Assets\Cache\zhumadian_native_chunk_cache')
Link-Checked (Join-Path $engine 'work\zhumadian_native_chunk_cache') (Join-Path $target 'Assets\Cache\zhumadian_native_chunk_cache')
foreach ($name in @('osgb_exporter_build','osg-build-min')) {
  Link-Checked (Join-Path $engine ('work\' + $name)) (Join-Path $runtime ('work\' + $name))
}
$utf8 = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $app 'run_editor.cmd'), "@echo off`r`nsetlocal`r`nstart `"`" `"%~dp0Runtime\current\ht_landscape_editor_0_8.exe`" %*`r`n", $utf8)
[IO.File]::WriteAllText((Join-Path $app 'run_web.cmd'), "@echo off`r`nsetlocal`r`n`"%~dp0Runtime\current\runtime\python\python.exe`" `"%~dp0tools\launch_landscape_web.py`" %*`r`n", $utf8)
[IO.File]::WriteAllText((Join-Path $target 'run_editor.cmd'), "@echo off`r`ncall `"%~dp0Application\run_editor.cmd`" %*`r`n", $utf8)
[IO.File]::WriteAllText((Join-Path $target 'run_web.cmd'), "@echo off`r`ncall `"%~dp0Application\run_web.cmd`" %*`r`n", $utf8)
Write-Output "MIGRATED: engine=$engine application=$app archive=$backup"
