$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$dependencies = @(
    @('snes9x', 'https://github.com/snes9xgit/snes9x.git', '1bcc369e89f08243e0a462882fb1f3e42e51de3a'),
    @('SDL', 'https://github.com/libsdl-org/SDL.git', '5d249570393f7a37e037abf22cd6012a4cc56a71'),
    @('imgui', 'https://github.com/ocornut/imgui.git', '6d910d5487d11ca567b61c7824b0c78c569d62f0')
)
foreach ($dependency in $dependencies) {
    $destination = Join-Path 'third_party' $dependency[0]
    if (!(Test-Path $destination)) {
        git init $destination
        if ($LASTEXITCODE) { throw 'git init failed' }
        git -C $destination remote add origin $dependency[1]
        git -C $destination fetch --depth 1 origin $dependency[2]
        if ($LASTEXITCODE) { throw 'Dependency fetch failed' }
        git -C $destination checkout --detach FETCH_HEAD
    }
    $actual = git -C $destination rev-parse HEAD
    if ($actual -ne $dependency[2]) { throw "Unexpected dependency revision: $destination" }
}
New-Item -ItemType Directory -Force licenses | Out-Null
Copy-Item third_party/snes9x/LICENSE licenses/Snes9x.txt
Copy-Item third_party/snes9x/filter/snes_ntsc-license.txt licenses/snes_ntsc.txt
Copy-Item third_party/SDL/LICENSE.txt licenses/SDL2.txt
Copy-Item third_party/imgui/LICENSE.txt licenses/DearImGui.txt
