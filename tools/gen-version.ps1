$tag = (git describe --tags --always --dirty).Trim()
$sha = (git rev-parse --short HEAD).Trim()
$ts  = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
$dst = "$PSScriptRoot\..\src\build\version.h"
@"
#pragma once
#define CG_GIT_TAG   L"$tag"
#define CG_GIT_SHA   L"$sha"
#define CG_BUILD_TIME L"$ts"
"@ | Set-Content -Encoding UTF8 $dst
Write-Host "Wrote $dst"
