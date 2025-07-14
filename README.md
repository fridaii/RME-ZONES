This is  fork of OTA rme that can be found here: https://github.com/OTAcademy/RME
It has zoneId system added, rme edit was made by Oskar (but on old RME, and with one small bug that is fixed in my repo): https://github.com/Oskar1121/RME/pull/3/files

This rme is meant to be used with this commit for tfs: https://github.com/fridaii/TFS-ZONES

Join Discord for more resources:  https://discord.gg/UuG4WuCZ9b

### Compilation using vcpkg manifest

1. Install and integrate `vcpkg` using `cmd` or other terminal on Windows.
Example with Git Bash terminal (installed with Git SCM):
    ```
    git clone https://github.com/microsoft/vcpkg.git
    cd vcpkg
    ./bootstrap-vcpkg.sh
    ./vcpkg integrate install
    ```
2. Open `vcproj/Editor.sln` in Visual Studio 2022.
3. Change a build type from `Debug` to `Release` - `Debug` build does not work in Visual Studio.
4. Click `Build` -> `Build Solution` in a top menu.
5. Copy all files from `vcproj\x64\Release` to the main RME directory.
6. Done. You can run `Editor_x64.exe` to open RME.