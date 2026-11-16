# SilentRunner 0.1.0 使用说明

## 命令格式

```text
SilentRunner.exe <target> [--cwd <directory>] [-- arguments...]
SilentRunner.exe --help
SilentRunner.exe --version
```

目标必须放在最前面；`--cwd` 可放在目标后、`--` 前。目标参数必须放在 `--` 后。帮助和版本必须单独使用。不提供短选项。旧的 `--cwd <目录> <target>` 格式和无参数调用返回错误码 2。

目标支持 `.exe`、`.bat`、`.cmd`、`.ps1`，扩展名不区分大小写。省略扩展名只补 `.exe`。带目录的路径直接解析；裸名称按调用者当前目录、`PATH` 顺序查找，不使用文件关联、App Paths 或 PATHEXT。

带目录的目标路径和 `--cwd` 的相对路径以调用者当前目录解析。指定 `--cwd` 后，裸文件名优先在该目录查找，然后查找调用者当前目录和 `PATH`。未指定 `--cwd` 时，以解析出的目标 EXE 或脚本所在目录作为工作目录。仅写 `nginx.exe` 时仍需能从调用者当前目录或 `PATH` 找到它；从其他目录运行请写出 nginx 的路径。环境变量和权限继承，不自动提权。

```powershell
.\SilentRunner.exe "C:\Apps\server.exe" --cwd "C:\Data" -- --port 8080
.\SilentRunner.exe "C:\Applications\DevTools\nginx\nginx.exe" -- -s reload
.\SilentRunner.exe nginx.exe --cwd "C:\Applications\DevTools\nginx" -- -s reload
.\SilentRunner.exe "C:\Scripts\backup.cmd" -- "daily backup"
.\SilentRunner.exe "C:\Scripts\job.ps1" -- -Name "中文任务"
.\SilentRunner.exe pwsh.exe -- -NoProfile -NonInteractive -File "C:\Scripts\job.ps1"
```

## 无窗口与返回结果

启动器使用 Windows 子系统，不分配控制台。已有终端或重定向可用时，帮助/版本写标准输出，启动错误写标准错误。重定向输出编码为 UTF-8；无终端且无重定向时静默，不显示对话框。启动成功不输出信息。

| 退出码 | 含义 |
|---|---|
| 0 | 进程创建成功，或者帮助/版本成功 |
| 1 | 找不到目标、工作目录无效、权限不足、无法启动或内部错误 |
| 2 | 选项错误、缺失目标、不支持的扩展名、无法安全编码的批处理参数 |

目标的标准输入为 EOF，标准输出和错误丢弃。需要交互输入的程序不适用。创建脚本解释器成功也返回 0，即使解释器之后拒绝脚本、脚本语法错误或任务失败。首版不观察这些异步结果。

启动器不主动终止目标，也不创建限制目标寿命的 Job。外部 Job、会话注销、系统关机或目标自身行为仍可结束目标。默认子进程不会分配控制台；GUI 程序或脚本主动创建的新窗口不受保证。

## 终端等待和退出码

GUI 子系统程序在交互终端中的等待行为与普通控制台 EXE 不同。不要依赖直接执行后立即读取的旧 `$LASTEXITCODE`。

PowerShell 查看帮助/版本可以使用管道：

```powershell
.\SilentRunner.exe --help | Out-String
.\SilentRunner.exe --version | Out-String
```

获取启动结果时，只等待启动器进程：

```powershell
$p = Start-Process -FilePath '.\SilentRunner.exe' `
    -ArgumentList '"C:\Apps\server.exe" --cwd "C:\Data" -- --port 8080' `
    -NoNewWindow -PassThru
$p.WaitForExit()
$p.ExitCode
```

这里故意不使用 `Start-Process -Wait`：该开关可能等待整个进程树，包括长期运行的目标。复杂参数需要按 Windows 引号规则传入；调用方 Shell 在启动 SilentRunner 之前进行的变量展开和参数转换无法由 SilentRunner 撤销。

CMD 中使用：

```cmd
start "" /b /wait "C:\Tools\SilentRunner.exe" --help
start "" /b /wait "C:\Tools\SilentRunner.exe" "C:\Apps\server.exe"
echo %ERRORLEVEL%
```

快捷方式的“目标”可以填写 `"C:\Tools\SilentRunner.exe" "C:\Apps\server.exe"`。双击 EXE 本身无参数时静默返回 2。

## 脚本规则

BAT/CMD 使用系统目录中的 `cmd.exe /d /s /v:off /c`。参数逐个加引号，支持空字符串、空格、中文。为避免 CMD 展开和注入，**脚本完整路径及所有参数**禁止以下字符：

```text
" % ! ^ & | < > ( ) 回车 换行
```

遇到这些字符返回 2，不启动解释器。这也意味着位于带括号目录的批处理脚本需要移动到符合规则的目录。脚本内部对 `%1`、`%~1`、`%*` 的使用及其调用其他程序的引号方式仍由脚本负责；启动器不能保证脚本内部再次解析后保持原值。脚本内容编码由 CMD 解释，不自动转换。

PS1 默认使用系统 Windows PowerShell 的 `-NoLogo -NoProfile -NonInteractive -File`。不添加 `-ExecutionPolicy Bypass`；遵守当前执行策略。参数绑定和类型转换遵循 Windows PowerShell `-File` 规则，复杂引号、空参数等行为不承诺与原生 EXE 相同。PowerShell 7 必须显式以 `pwsh.exe` 为目标。

EXE 参数按 Microsoft C/C++ 运行库规则编码，支持空参数、引号和末尾反斜杠；自行解析原始命令行的应用可能采用不同语法。
