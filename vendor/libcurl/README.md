# 📦 libcurl (vendored)

This folder contains a prebuilt static version of **libcurl** extracted from vcpkg.

- **Version:** 8.14.1-DEV  
- **Platform:** Windows x64  
- **Linkage:** Static
- **Compiled with:** MSVC (Microsoft Visual C++)  
- **Source:** Installed via `vcpkg install curl[core,sspi,ssl,schannel,non-http]:x64-windows-static`

## Folder Structure

vendor/libcurl/
├── include/ # Header files
└── lib/ # Static libraries (.lib)

## License
libcurl is licensed under an MIT-like license: https://curl.se/docs/copyright.html