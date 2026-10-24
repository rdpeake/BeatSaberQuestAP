# pushes the freshly built library to a headset connected over adb and restarts the game
$library = "./build/libbeatsaberquestap.so"
if (-not (Test-Path $library)) {
    Write-Output "Build the mod first: qpm s build"
    exit 1
}

& adb push $library /sdcard/ModData/com.beatgames.beatsaber/Modloader/mods/libbeatsaberquestap.so
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& adb shell am force-stop com.beatgames.beatsaber
& adb shell am start com.beatgames.beatsaber/com.unity3d.player.UnityPlayerActivity
