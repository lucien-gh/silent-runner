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

输出：`build/release/SilentRunner.exe` 和 `build/packages/SilentRunner-0.1.0-windows-x64.zip`。

## 说明

- [完整使用说明](docs/usage.md)。
- [验收记录](docs/validation.md)。
- 首版不提供等待目标结束、日志文件、提权、自动拉起或开机启动管理。
- 许可证尚未选择；本项目暂未授予开源许可，公开发布前需确定。

实现基于 Windows SDK，无第三方 C++ 库。源码按 `src/cli`、`src/process`、`src/windows` 分工；测试包含真实子进程和脚本，不只验证命令行字符串。
