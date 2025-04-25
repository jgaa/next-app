@echo off
setlocal enabledelayedexpansion

:: Remove all paths containing "vcpkg"
set "new_path="
for %%A in ("%PATH:;=";"%") do (
    set "item=%%~A"
    if "!item:vcpkg=!"=="!item!" (
        set "new_path=!new_path!;!item!"
    )
)
if defined new_path set "new_path=!new_path:~1!"

:: Return the cleaned PATH to the caller
endlocal & set "CLEANED_PATH=%new_path%"
