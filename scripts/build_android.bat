@echo off

:: Build the Android APK (arm64-v8a, Vulkan-only) via Gradle, which drives
:: erhe's top-level CMake under externalNativeBuild.
::
:: Default task is assembleDebug. Extra arguments are forwarded to gradlew,
:: e.g.:
::     scripts\build_android.bat assembleRelease
::     scripts\build_android.bat clean assembleDebug
::     scripts\build_android.bat assembleDebug --info

setlocal

:: --- Locate Android SDK -----------------------------------------------------
if "%ANDROID_HOME%"=="" (
    if exist "%LOCALAPPDATA%\Android\Sdk\" (
        set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
    )
)
if "%ANDROID_HOME%"=="" (
    echo ERROR: ANDROID_HOME is not set and %%LOCALAPPDATA%%\Android\Sdk does not exist.
    echo Install Android Studio or set ANDROID_HOME to your SDK path.
    exit /b 1
)
if not exist "%ANDROID_HOME%\platform-tools\" (
    echo ERROR: ANDROID_HOME=%ANDROID_HOME% does not look like a valid SDK
    echo - no platform-tools subdirectory.
    exit /b 1
)
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"

:: --- Locate JDK 21 ----------------------------------------------------------
:: Gradle 8.12 (bundled wrapper) does not support JDK 25; use JDK 21 from
:: Android Studio's bundled JetBrains Runtime by default.
if "%JAVA_HOME%"=="" (
    if exist "C:\Program Files\Android\Android Studio\jbr\bin\java.exe" (
        set "JAVA_HOME=C:\Program Files\Android\Android Studio\jbr"
    )
)
if "%JAVA_HOME%"=="" (
    echo ERROR: JAVA_HOME is not set and Android Studio's JBR was not found.
    echo Set JAVA_HOME to a JDK 21 installation.
    exit /b 1
)
if not exist "%JAVA_HOME%\bin\java.exe" (
    echo ERROR: JAVA_HOME=%JAVA_HOME% does not contain bin\java.exe
    exit /b 1
)

:: --- Run Gradle -------------------------------------------------------------
set "_proj_root=%~dp0.."
set "_gradle_dir=%_proj_root%\android-project"

if not exist "%_gradle_dir%\gradlew.bat" (
    echo ERROR: %_gradle_dir%\gradlew.bat not found.
    exit /b 1
)

set "_tasks=%*"
if "%_tasks%"=="" set "_tasks=assembleDebug"

echo ANDROID_HOME = %ANDROID_HOME%
echo JAVA_HOME    = %JAVA_HOME%
echo Gradle dir   = %_gradle_dir%
echo Tasks        = %_tasks%

for /f "delims=" %%a in ('powershell -nologo -command "Get-Date -Format o"') do set "start=%%a"

call "%_gradle_dir%\gradlew.bat" -p "%_gradle_dir%" --no-daemon --console=plain %_tasks%
set "_rc=%ERRORLEVEL%"

for /f "delims=" %%a in ('powershell -nologo -command "Get-Date -Format o"') do set "end=%%a"
for /f "delims=" %%a in ('powershell -nologo -command "$start=[datetime]::Parse('%start%'); $end=[datetime]::Parse('%end%'); ($end - $start).TotalSeconds"') do set duration=%%a

if not "%_rc%"=="0" goto :failed

echo Build completed in %duration% seconds
if exist "%_gradle_dir%\app\build\outputs\apk\debug\app-debug.apk" echo APK: %_gradle_dir%\app\build\outputs\apk\debug\app-debug.apk
exit /b 0

:failed
echo Build FAILED in %duration% seconds, exit code %_rc%
exit /b %_rc%
