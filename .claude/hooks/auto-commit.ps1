# SkyrimMods - Claude Code "Stop" hook.
# SkyrimMods is a SINGLE monorepo, so this commits + best-effort pushes the workspace root repo.
$ErrorActionPreference = 'SilentlyContinue'

# hooks -> .claude -> SkyrimMods (the repo root)
$root  = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'

if (-not (Test-Path (Join-Path $root '.git'))) { return }

Push-Location $root
try {
    $dirty = git status --porcelain
    if ($dirty) {
        if (-not (git config user.name))  { git config user.name  'Hagryph' }
        if (-not (git config user.email)) { git config user.email 'hagryph.gaming@gmail.com' }

        git add -A | Out-Null
        git commit -m "auto: $stamp (Claude Stop hook)" | Out-Null

        $branch = (git rev-parse --abbrev-ref HEAD).Trim()
        git push origin $branch 2>$null | Out-Null
        Write-Output "[auto-commit] SkyrimMods @ $stamp"
    }
} finally {
    Pop-Location
}
