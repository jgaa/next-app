@echo off
setlocal EnableDelayedExpansion

:: Define substrings to exclude (case-insensitive)
set "exclude_list=openssl vcpkg"

:: Initialize new PATH
set "new_path="

:: Loop through each entry in PATH
for %%A in ("%PATH:;=";"%") do (
    set "item=%%~A"

    :: Convert item to lowercase using PowerShell
    for /f "delims=" %%B in ('powershell -nologo -command "[string]'!item!'.ToLower()"') do (
        set "item_lc=%%B"
    )

    :: Check if item contains any excluded substrings
    set "exclude=0"
    for %%S in (%exclude_list%) do (
        echo !item_lc! | findstr /C:"%%S" >nul
        if not errorlevel 1 (
            set "exclude=1"
        )
    )

    :: If not excluded, add to new_path
    if "!exclude!"=="0" (
        set "new_path=!new_path!;!item!"
    ) else (
        echo Skipping path: !item!
    )
)

:: Remove leading semicolon if present
if defined new_path (
    set "new_path=!new_path:~1!"
)

:: Export the cleaned PATH to the parent script
endlocal & set "CLEANED_PATH=%new_path%"
