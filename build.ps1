param([ValidateSet('Release','Debug')][string]$Configuration='Release',[switch]$Package)
$ErrorActionPreference='Stop'
$env:MSBUILDDISABLENODEREUSE='1'
$root=$PSScriptRoot
cmake -S $root -B "$root/build" -G 'Visual Studio 17 2022' -A x64
if($LASTEXITCODE){throw 'CMake configuration failed'}
cmake --build "$root/build" --config $Configuration --parallel 4
if($LASTEXITCODE){throw 'Build failed'}
python "$root/scripts/test-vst2-paths.py" "$root/build/bin/$Configuration/reajsfx.dll"
if($LASTEXITCODE){throw 'VST2 resource-path regression failed'}
cmake --install "$root/build" --config $Configuration --prefix "$root/stage"
if($LASTEXITCODE){throw 'Installation staging failed'}
if($Package){
  python "$root/scripts/package.py" --stage "$root/stage" --output "$root/release"
  if($LASTEXITCODE){throw 'Packaging failed'}
}
