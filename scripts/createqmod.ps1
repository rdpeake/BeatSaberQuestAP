Param(
    [Parameter(Mandatory = $false)]
    [String] $qmodName = ""
)

& qpm qmod manifest
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$modJson = Get-Content "./mod.json" -Raw | ConvertFrom-Json
if ($qmodName -eq "") {
    $qmodName = $modJson.name
}

$fileList = @("./mod.json")
if ($modJson.coverImage -and (Test-Path ("./" + $modJson.coverImage))) {
    $fileList += "./" + $modJson.coverImage
}

foreach ($library in @($modJson.modFiles) + @($modJson.lateModFiles) + @($modJson.libraryFiles)) {
    if (-not $library) {
        continue
    }
    $path = "./build/" + $library
    if (-not (Test-Path $path)) {
        $path = "./extern/libs/" + $library
    }
    if (-not (Test-Path $path)) {
        Write-Output "Error: could not find $library"
        exit 1
    }
    $fileList += $path
}

$zip = $qmodName + ".zip"
$qmod = $qmodName + ".qmod"
Compress-Archive -Path $fileList -DestinationPath $zip -Force
Move-Item $zip $qmod -Force
Write-Output "Created $qmod"
