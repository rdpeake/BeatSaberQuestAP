# streams this mod's log lines, plus crashes, from a headset connected over adb
& adb logcat -c
& adb logcat | Select-String -Pattern "beatsaberquestap|FATAL|backtrace"
