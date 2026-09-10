# LumaText 预编译依赖

固定的 Windows x64 Release 共享库包，供 LUMEN 日常构建与 Release 直接复用。该目录随源码保存，不放在会被清理的 `build/` 中；无需重新编译 LumaText、FreeType 或 HarfBuzz。

- 原始包：`lumatext-prebuilt-20260906.zip`，来自本仓库的 `lumatext-deps` Release。
- 原始 ZIP SHA256：`FEAD9871FC68C341E537B437800D22E1E0B8D7F86884D6427A7C8A7423C047A3`。
- 内容：`include/`、`bin/lumatext.dll`、`lib/lumatext.lib`、`lib/cmake/LumaText/`、`licenses/`。
- `SHA256SUMS` 固定各依赖文件的字节校验值，CI 构建前核对。不要单独替换 DLL 或导入库，两者与头文件应一起更新。
- LumaText 与其第三方许可证保留在 `licenses/`，构建时随应用复制，安装 SDK 时一并安装。

默认通过 `find_package(LumaText CONFIG)` 导入，不会编译该目录。可用 `LUMATEXT_PREBUILT_DIR` 指向另一份完整包；显式开发源码时同时设置 `LUMEN_USE_PREBUILT_LUMATEXT=OFF` 和 `LUMATEXT_SOURCE_DIR`。

本包只提供 x64 Release 二进制，不代表已验证 ARM64、x86 或独立 Debug 版 LumaText。
