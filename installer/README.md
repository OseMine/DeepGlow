# DeepGlow 2 Installers

This directory contains installers for Windows, macOS, and Linux.

## Windows

### Option 1: Batch installer (simple)
1. Build the plugin: `cmake --build build --config Release`
2. Right-click `installer/windows/install.bat` and select **Run as Administrator**
3. Restart DaVinci Resolve

### Option 2: Inno Setup installer
1. Install [Inno Setup](https://jrsoftware.org/isinfo.php)
2. Build the plugin
3. Compile `installer/windows/DeepGlow2.iss` with `iscc`
4. Distribute `DeepGlow2-Setup-Windows.exe`

## macOS

1. Build the plugin (universal binary is configured by default)
2. Open Terminal and run: `sudo installer/macos/install.command`
3. Restart DaVinci Resolve

Bundle is installed to: `/Library/OFX/Plugins/DeepGlow2.ofx.bundle`

## Linux

### Option 1: Install script
```bash
cd installer/linux
chmod +x install.sh
./install.sh  # as root installs to /usr/OFX/Plugins/, otherwise ~/.OFX/Plugins/
```

### Option 2: Tarball
```bash
cd installer/linux
chmod +x build-tarball.sh
./build-tarball.sh
# Creates installer/dist/DeepGlow2-Linux-x86_64-*.tar.gz
```

Bundle is installed to: `/usr/OFX/Plugins/` or `~/.OFX/Plugins/`

## Notes

- DaVinci Resolve searches OFX plugins in standard locations. If it doesn't appear, verify the bundle structure: `Contents/<Arch>/DeepGlow2.ofx`
- On macOS, you may need to allow the plugin in Security & Privacy settings
- All installers copy from `build/bundle/` if available, otherwise from the installer directory
