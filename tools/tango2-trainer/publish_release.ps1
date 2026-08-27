param(
    [Parameter(Mandatory=$true)]
    [ValidatePattern('^\d+\.\d+(?:\.\d+)?$')]
    [string]$Version
)

$ErrorActionPreference = "Stop"
$tag = "trainer-v$Version"

if (git status --porcelain) {
    throw "Working tree is not clean. Commit your changes before creating a release tag."
}

git rev-parse --verify $tag 2>$null
if ($LASTEXITCODE -eq 0) {
    throw "Tag $tag already exists locally."
}

Write-Host "Creating release tag $tag..."
git tag $tag

Write-Host "Pushing $tag to origin..."
git push origin $tag

Write-Host ""
Write-Host "Release workflow started."
Write-Host "When it finishes, the latest installer will be available at:"
Write-Host "https://github.com/Arealkermit/freedomtx/releases/latest/download/Tango2RacingTrainer_Setup.exe"
