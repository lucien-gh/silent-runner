# silent-runner

Windows 隐藏进程启动工具，版本 **0.1.0**，交付文件 **SilentRunner.exe**。

支持 Windows 10/11 x64，启动 EXE、BAT、CMD 和 PowerShell 脚本。启动器不创建窗口，控制台目标以无控制台窗口方式启动；成功创建目标进程后立即退出。

```powershell
.\SilentRunner.exe --help | Out-String
.\SilentRunner.exe --version | Out-String
.\SilentRunner.exe "C:\nginx\nginx.exe" -- -s reload
.\SilentRunner.exe "C:\Scripts\job.ps1" -- -Name "测试任务"
```

目标输入、输出、错误均连接 `NUL`。GUI 应用只请求隐藏初始窗口，应用后续主动打开的窗口不保证隐藏。脚本也可以主动启动窗口，启动器不会拦截。程序创建成功不代表脚本执行成功或服务已就绪。

## 构建与测试

安装 MSVC x64 编译工具、Windows SDK、CMake 3.25+、Ninja。在 **x64 Native Tools Command Prompt** 中切换到项目根目录后运行以下命令；也可使用已加载 Visual Studio x64 编译环境的 PowerShell 7（将下面的 `#` 注释改为 `#`）：

```shell
# 配置 Debug：检测编译工具链，并在 build/debug 中生成 Ninja 构建文件
cmake --preset debug
# 编译 Debug 程序和测试工具，供开发调试使用
cmake --build --preset debug
# 运行 Debug 测试；失败时显示测试输出
ctest --preset debug

# 配置 Release：在 build/release 中生成用于发布的构建文件
cmake --preset release
# 编译 Release 程序和测试工具，生成 build/release/SilentRunner.exe
cmake --build --preset release
# 运行 Release 测试，验证发布构建；失败时显示测试输出
ctest --preset release

# Release 构建并通过测试后，将程序及文档打包为 ZIP，输出到 build/packages
cpack --config build/release/CPackConfig.cmake -B build/packages
```

按顺序执行，上一条命令成功后再继续；`ctest` 和 `cpack` 不会自动编译程序。测试默认启用，包含 Windows PowerShell 和 CMD 脚本测试；配置时检测到 `pwsh` 后，还会加入 PowerShell 7 测试。

预设不会自行初始化 MSVC。Release 静态链接 MSVC 运行库，无第三方运行时依赖；Debug 不用于分发。

输出：`build/release/SilentRunner.exe` 和 `build/packages/SilentRunner-v0.1.0.zip`。

## GitHub Actions 发布

工作流 `.github/workflows/release.yml` 在推送 `v主版本.次版本.修订号` 标签时触发，例如 `v0.1.0`。它在 Windows x64 环境中加载 MSVC，执行 Release 配置、编译、测试和 ZIP 打包，全部成功后创建并发布 GitHub Release，附带：

- `SilentRunner.exe`：可直接使用的程序。
- `SilentRunner-v{Version}.zip`：包含程序、README 和使用及验收文档的压缩包，例如 `SilentRunner-v0.1.0.zip`。

发布前，将 `CMakeLists.txt` 中 `project(... VERSION ...)` 的版本号更新为目标版本，并同步文档中的版本示例。标签必须与 CMake 版本一致，否则工作流失败并停止发布。将代码及工作流提交、推送到 `main` 后，再创建并推送对应标签：

```powershell
# 首次发布示例；后续版本替换为对应版本号，不要复用已有标签
git push origin main
git tag -a v0.1.0 -m "Release v0.1.0"
git push origin v0.1.0
```

在 GitHub 仓库的 **Actions → Build and release** 中查看进度，完成后到 **Releases** 下载附件。工作流使用 GitHub 自动提供的 `GITHUB_TOKEN`（`contents: write` 权限），无需另建个人访问令牌；仓库需要启用 GitHub Actions。

构建、测试或打包失败时不会创建 Release，修复后用新版本标签发布。上传完成前 Release 保持草稿；若最后发布步骤失败，可检查并手动发布保留的草稿。已有同名 Release 时工作流会报错，不会覆盖已发布附件。

## 图标素材

`img/` 保存同一图标的不同格式、尺寸及 Windows 场景素材：

| 文件或目录 | 用途 |
| --- | --- |
| `img/ico/Sudo.ico` | Windows EXE 和快捷方式图标，已通过 `src/SilentRunner.rc` 接入 `SilentRunner.exe` 的构建。 |
| `img/svg/ic_fluent_Sudo.svg` | 矢量源图，适合 README、项目主页和后续缩放导出。 |
| `img/svg/_16.png` ～ `_256.png` | 不同像素尺寸的 PNG，适合文档插图、按钮和工具栏。 |
| `img/Windows/AppList.*.png` | Windows 应用列表图标素材。 |
| `img/Windows/StoreLogo.*.png`、`StoreDisplay-*.png` | Microsoft Store 相关图标素材。 |
| `img/Windows/SplashScreen.*.png` | 应用启动画面素材；当前隐藏启动器不使用启动画面。 |
| `img/Windows/WideTile.*.png` | Windows 宽磁贴素材。 |

文件名中的 `scale-100/200/400` 表示不同显示缩放比例，`targetsize-16/32/...` 表示具体像素尺寸，`theme-light/dark` 表示浅色或深色主题版本。

当前仅将 ICO 接入 EXE 构建，其余图片作为备用素材保留，不会因放入目录就自动生效。未来制作 MSIX 包或发布到 Microsoft Store 时，可按对应清单和发布要求选用 Windows 素材。

## 说明

- [完整使用说明](docs/usage.md)。
- [验收记录](docs/validation.md)。
- 首版不提供等待目标结束、日志文件、提权、自动拉起或开机启动管理。
- 许可证尚未选择；本项目暂未授予开源许可，公开发布前需确定。

实现基于 Windows SDK，无第三方 C++ 库。源码按 `src/cli`、`src/process`、`src/windows` 分工；测试包含真实子进程和脚本，不只验证命令行字符串。
