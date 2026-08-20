# ============================================================
#   Vulkan 开发环境一键部署脚本
#   检测 + 自动安装缺失组件 (Windows 10/11, 需管理员权限)
#
#   覆盖组件：
#     MSYS2 / MinGW-w64 / CMake / Git / GLFW / GLM / Vulkan SDK
#   附带功能：
#     - Vulkan MinGW 导入库自动生成 (gendef + dlltool)
#     - 最终环境完整性验证报告
#
#   用法：双击或右键运行即可
#         脚本检测到非管理员会自动 UAC 提权重启（无需手动右键管理员）
#         或: powershell -ExecutionPolicy Bypass -File setup_env.ps1
# ============================================================

# ============================================================
# 全局状态
# ============================================================
$script:LogFile    = Join-Path $PSScriptRoot "install_log.txt"
$script:StartTime  = Get-Date
$script:Components = @()
$script:Msys2Root  = ""
$script:Subsystem  = ""   # ucrt64 / mingw64 / clang64

# MSYS2 可能安装路径（按优先级）
$script:Msys2Candidates = @(
    "C:\msys64", "C:\msys2",
    "$env:ProgramFiles\msys64", "${env:ProgramFiles(x86)}\msys64",
    "D:\msys64", "D:\msys2", "D:\Program Files\msys64",
    "E:\msys64", "E:\msys2"
)

# 子系统 → pacman 包前缀 映射
$script:SubsystemPrefix = @{
    "ucrt64"  = "mingw-w64-ucrt-x86_64-"
    "mingw64" = "mingw-w64-x86_64-"
    "clang64" = "mingw-w64-clang-x86_64-"
    "mingw32" = "mingw-w64-i686-"
    "clang32" = "mingw-w64-clang-i686-"
}

# ============================================================
# 1. 辅助函数
# ============================================================

function Write-Log {
    param([string]$Message)
    $line = "[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] $Message"
    Add-Content -Path $script:LogFile -Value $line -Encoding UTF8
}

function Write-Color {
    param([string]$Message, [ConsoleColor]$Color = "White")
    $old = $host.UI.RawUI.ForegroundColor
    $host.UI.RawUI.ForegroundColor = $Color
    Write-Host $Message
    $host.UI.RawUI.ForegroundColor = $old
    Write-Log $Message
}

function Write-Info  { param([string]$M) Write-Color "  [INFO] $M" -Color Blue   }
function Write-Ok    { param([string]$M) Write-Color "  [OK]   $M" -Color Green  }
function Write-Warn  { param([string]$M) Write-Color "  [WARN] $M" -Color Yellow }
function Write-Err   { param([string]$M) Write-Color "  [FAIL] $M" -Color Red    }

function Write-Section {
    param([string]$Title)
    Write-Host ""
    Write-Color ("-" * 60) -Color DarkGray
    Write-Color "  $Title" -Color Cyan
    Write-Color ("-" * 60) -Color DarkGray
}

function Add-Status {
    param([string]$Name, [string]$Status)
    $script:Components += [PSCustomObject]@{ Name = $Name; Status = $Status }
}

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable("PATH", "Machine")
    $user    = [Environment]::GetEnvironmentVariable("PATH", "User")
    $env:PATH = (($machine + ";" + $user) -split ';' | Where-Object { $_ } | Select-Object -Unique) -join ';'
}

function Add-ToUserPath {
    param([string]$NewPath)
    $userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
    if (($userPath -split ';') -contains $NewPath) { return }
    $newPath = if ($userPath) { "$userPath;$NewPath" } else { $NewPath }
    [Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
    Write-Ok "已加入用户 PATH: $NewPath"
    Update-SessionPath
}

function Test-Cmd {
    param([string]$Cmd)
    try { $null = Get-Command $Cmd -ErrorAction Stop; return $true } catch { return $false }
}

function Test-IsAdmin {
    try {
        $id = [Security.Principal.WindowsIdentity]::GetCurrent()
        $p  = New-Object Security.Principal.WindowsPrincipal($id)
        return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    } catch { return $false }
}

function Test-Is64BitWindows {
    return [Environment]::Is64BitOperatingSystem
}

function Open-Url {
    param([string]$Url, [string]$Name)
    Write-Warn "$Name 需手动安装，打开下载页: $Url"
    try { Start-Process $Url } catch { Write-Err "无法打开浏览器，请手动访问: $Url" }
}

# ============================================================
# 2. MSYS2 探测 + 子系统识别
# ============================================================

function Find-Msys2 {
    # 策略 1: 环境变量
    if ($env:MSYS2_ROOT -and (Test-Path "$env:MSYS2_ROOT\etc\pacman.conf")) {
        $script:Msys2Root = $env:MSYS2_ROOT
        Write-Info "MSYS2 (环境变量): $($script:Msys2Root)"
        return $true
    }
    # 策略 2: where g++ 反推
    $gpp = Get-Command g++ -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
    if ($gpp) {
        # g++ 位于 {root}/{subsys}/bin/g++.exe
        $subsysDir = Split-Path (Split-Path $gpp -Parent) -Parent
        $candidate = Split-Path $subsysDir -Parent
        if (Test-Path "$candidate\etc\pacman.conf") {
            $script:Msys2Root = $candidate
            Write-Info "MSYS2 (g++ 反推): $($script:Msys2Root)"
            return $true
        }
    }
    # 策略 3: 常见路径
    foreach ($p in $script:Msys2Candidates) {
        if (Test-Path "$p\etc\pacman.conf") {
            $script:Msys2Root = $p
            Write-Info "MSYS2 (常见路径): $($script:Msys2Root)"
            return $true
        }
    }
    return $false
}

function Get-LatestMsys2Installer {
    # 优先通过 GitHub API 拿最新 release 的非 sfx NSIS 安装器
    try {
        $api = "https://api.github.com/repos/msys2/msys2-installer/releases/latest"
        $release = Invoke-RestMethod -Uri $api -UseBasicParsing -TimeoutSec 15
        # 优先选 NSIS: msys2-x86_64-<date>.exe (非 sfx)
        $asset = $release.assets | Where-Object {
            $_.name -like "msys2-x86_64-*.exe" -and $_.name -notlike "*sfx*"
        } | Select-Object -First 1
        if (-not $asset) {
            # 退到 sfx 自解压包
            $asset = $release.assets | Where-Object { $_.name -like "*sfx*" } | Select-Object -First 1
        }
        if ($asset) {
            return @{ Url = $asset.browser_download_url; Name = $asset.name; IsSfx = ($asset.name -like "*sfx*") }
        }
    } catch {
        Write-Warn "GitHub API 调用失败，使用 fallback 链接: $_"
    }
    # Fallback: 官方 nightly sfx
    return @{
        Url  = "https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-base-x86_64-latest.sfx.exe"
        Name = "msys2-base-x86_64-latest.sfx.exe"
        IsSfx = $true
    }
}

function Install-MSYS2 {
    Write-Host ""
    Write-Color "  即将自动安装 MSYS2（这是较大下载，约 100MB+）" -Color Yellow
    $ans = Read-Host "  确认继续? [y/N]"
    if ($ans -notmatch '^[yY]') {
        Write-Warn "用户取消，跳过 MSYS2 自动安装"
        return $false
    }

    # 必须是 64 位 Windows
    if (-not (Test-Is64BitWindows)) {
        Write-Err "MSYS2 自动安装仅支持 64 位 Windows"
        return $false
    }

    # 安装目标路径（优先 C:\msys64，遵循官方默认）
    $installRoot = "C:\msys64"
    if (Test-Path "$installRoot\usr\bin\bash.exe") {
        Write-Ok "MSYS2 已存在: $installRoot (跳过安装)"
        $script:Msys2Root = $installRoot
        return $true
    }

    # 选择安装路径（避免与已存在目录冲突）
    if (Test-Path $installRoot) {
        $i = 2
        while (Test-Path "C:\msys64_$i") { $i++ }
        $installRoot = "C:\msys64_$i"
        Write-Warn "C:\msys64 已存在但非完整 MSYS2，改用: $installRoot"
    }

    # 下载安装器
    $installer = Get-LatestMsys2Installer
    $installerPath = Join-Path $env:TEMP $installer.Name
    Write-Info "下载: $($installer.Url)"
    Write-Info "保存到: $installerPath"
    try {
        # 优先用 BITS（支持断点续传、更稳定）
        $bits = $false
        try {
            Import-Module BitsTransfer -ErrorAction Stop
            Start-BitsTransfer -Source $installer.Url -Destination $installerPath -DisplayName "MSYS2 Installer" -Description "Vulkan env setup"
            $bits = $true
        } catch {
            Write-Info "BITS 不可用，回退到 WebClient"
        }
        if (-not $bits) {
            $wc = New-Object System.Net.WebClient
            $wc.Headers.Add("User-Agent", "VulkanProjectBase-setup")
            $wc.DownloadFile($installer.Url, $installerPath)
        }
        if (-not (Test-Path $installerPath) -or (Get-Item $installerPath).Length -lt 1MB) {
            Write-Err "下载失败或文件过小"
            return $false
        }
        $sizeMB = [math]::Round((Get-Item $installerPath).Length / 1MB, 1)
        Write-Ok "下载完成 ($sizeMB MB)"
    } catch {
        Write-Err "下载出错: $_"
        return $false
    }

    # 静默安装
    Write-Info "静默安装到: $installRoot"
    $proc = $null
    try {
        if ($installer.IsSfx) {
            # SFX 自解压格式: install --root <path> --confirm-command
            $proc = Start-Process -FilePath $installerPath -ArgumentList @("install", "--root", $installRoot, "--confirm-command") -Wait -PassThru
        } else {
            # NSIS 安装器: /S 静默，/D=<绝对路径> 指定目录
            $proc = Start-Process -FilePath $installerPath -ArgumentList "/S /D=$installRoot" -Wait -PassThru
        }
    } catch {
        Write-Err "安装进程启动失败: $_"
        return $false
    } finally {
        if (Test-Path $installerPath) { Remove-Item $installerPath -Force -ErrorAction SilentlyContinue }
    }
    if (-not $proc -or $proc.ExitCode -ne 0) {
        Write-Err "MSYS2 安装失败 (退出码 $(if($proc){$proc.ExitCode}else{'N/A'}))"
        return $false
    }
    if (-not (Test-Path "$installRoot\usr\bin\bash.exe")) {
        Write-Err "安装后未找到 bash.exe，可能安装未完整"
        return $false
    }

    $script:Msys2Root = $installRoot
    Write-Ok "MSYS2 安装完成: $installRoot"

    # 加入用户 PATH（usr/bin + 默认 ucrt64/bin）
    Add-ToUserPath "$installRoot\usr\bin"
    Add-ToUserPath "$installRoot\ucrt64\bin"
    Update-SessionPath

    $bash = "$installRoot\usr\bin\bash.exe"

    # 初始化 pacman 密钥（首次安装必需）
    Write-Info "初始化 pacman 密钥 (pacman-key --init / --populate)..."
    & $bash -lc "pacman-key --init; pacman-key --populate msys2" 2>&1 | Out-Null
    Write-Ok "pacman 密钥初始化完成"

    # 同步数据库
    Write-Info "同步 pacman 数据库 (首次)..."
    & $bash -lc "pacman -Sy --noconfirm --disable-download-timeout" 2>&1 | Out-Null

    # 默认安装 ucrt64 工具链（最新 MSYS2 推荐）
    Write-Info "安装 ucrt64 默认工具链 (mingw-w64-ucrt-x86_64-toolchain)..."
    $out = & $bash -lc "pacman -S --noconfirm --needed --disable-download-timeout mingw-w64-ucrt-x86_64-toolchain" 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Ok "ucrt64 工具链安装完成"
        # 设置默认子系统
        $script:Subsystem = "ucrt64"
    } else {
        Write-Warn "ucrt64 工具链自动安装失败，请后续手动 pacman -S mingw-w64-ucrt-x86_64-toolchain"
        $tail = $out | Where-Object { $_ -and $_.ToString().Trim() } | Select-Object -Last 10
        if ($tail) { $tail | ForEach-Object { Write-Color "    $_" -Color DarkGray } }
    }

    return $true
}

function Resolve-Subsystem {
    # 策略 1: 优先选择已安装 GLFW/GLM 包的子系统（说明用户已在该子系统配置开发环境）
    # 避免出现 g++ 在 ucrt64 但包在 mingw64 导致 pacman 装错子系统的情况
    foreach ($s in @("ucrt64", "mingw64", "clang64")) {
        $glfwInc = "$($script:Msys2Root)\$s\include\GLFW\glfw3.h"
        if (Test-Path $glfwInc) {
            $script:Subsystem = $s
            Write-Info "子系统选择 (已有 GLFW): $s"
            return
        }
    }
    # 策略 2: g++ 路径反推
    $gpp = Get-Command g++ -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
    if ($gpp) {
        $sub = (Split-Path (Split-Path $gpp -Parent) -Parent | Split-Path -Leaf)
        if ($script:SubsystemPrefix.ContainsKey($sub)) {
            $script:Subsystem = $sub
            Write-Info "子系统选择 (g++ 反推): $sub"
            return
        }
    }
    # 策略 3: 按优先级找已安装工具链
    foreach ($s in @("ucrt64", "mingw64", "clang64")) {
        if (Test-Path "$($script:Msys2Root)\$s\bin\g++.exe") {
            $script:Subsystem = $s
            Write-Info "子系统选择 (扫描工具链): $s"
            return
        }
    }
}

function Invoke-Pacman {
    param([string]$PackageSpec)
    $bash = "$($script:Msys2Root)\usr\bin\bash.exe"
    if (-not (Test-Path $bash)) { Write-Err "MSYS2 bash.exe 未找到"; return $false }

    # 1) 同步数据库（避免 "database file does not exist" / "target not found" 等）
    Write-Info "同步 pacman 数据库 (pacman -Sy)..."
    try {
        & $bash -lc "pacman -Sy --noconfirm --disable-download-timeout" 2>&1 | Out-Null
        Write-Ok "数据库同步完成"
    } catch {
        Write-Warn "数据库同步异常 (继续尝试): $_"
    }

    # 2) 安装包，同时捕获输出便于诊断
    $cmd = "pacman -S $PackageSpec --noconfirm --needed --disable-download-timeout"
    Write-Info "执行: $cmd"
    try {
        $output = & $bash -lc $cmd 2>&1
        $rc = $LASTEXITCODE
        if ($rc -eq 0) { Write-Ok "pacman 安装成功: $PackageSpec"; return $true }

        Write-Err "pacman 失败 (退出码 $rc): $PackageSpec"
        # 输出最后 15 行（去掉空行）用于诊断
        $tail = $output | Where-Object { $_ -and $_.ToString().Trim() } | Select-Object -Last 15
        if ($tail) {
            Write-Info "----- pacman 输出 (尾部 15 行) -----"
            $tail | ForEach-Object { Write-Color "    $_" -Color DarkGray }
            Write-Info "-----------------------------------"
        }
        Write-Info "请手动在 MSYS2 终端运行: pacman -S $PackageSpec"
        return $false
    } catch {
        Write-Err "执行 pacman 出错: $_"
        return $false
    }
}

function Install-Winget {
    param([string]$PackageId, [string]$Name)
    if (-not (Test-Cmd winget)) {
        Write-Warn "winget 不可用"
        Open-Url -Url "https://github.com/microsoft/winget-cli/releases" -Name "winget"
        return $false
    }
    Write-Info "winget 安装: $PackageId"
    try {
        $proc = Start-Process -FilePath winget -ArgumentList "install $PackageId -e --silent --accept-package-agreements --accept-source-agreements" -NoNewWindow -Wait -PassThru
        if ($proc.ExitCode -eq 0) { Write-Ok "$Name 安装完成"; Update-SessionPath; return $true }
        Write-Err "winget 安装失败 (退出码 $($proc.ExitCode)): $Name"
        return $false
    } catch {
        Write-Err "winget 出错: $_"
        return $false
    }
}

# ============================================================
# 3. Vulkan MinGW 导入库生成（gendef + dlltool）
# ============================================================

function Generate-VulkanMingwLib {
    if (-not $env:VULKAN_SDK) { return }
    $libDir = Join-Path $env:VULKAN_SDK "Lib"
    $outLib = Join-Path $libDir "libvulkan-1.dll.a"
    if (Test-Path $outLib) {
        $size = (Get-Item $outLib).Length
        Write-Info "Vulkan MinGW 导入库已存在 ($size 字节) → 跳过"
        return
    }
    $sysDll = "C:\Windows\System32\vulkan-1.dll"
    if (-not (Test-Path $sysDll)) {
        Write-Warn "系统 vulkan-1.dll 未找到，跳过导入库生成"
        return
    }
    $gendef   = Get-Command gendef -ErrorAction SilentlyContinue
    $dlltool  = Get-Command dlltool -ErrorAction SilentlyContinue
    if (-not $gendef -or -not $dlltool) {
        # 尝试在子系统 bin 下查找
        $binDir = "$($script:Msys2Root)\$($script:Subsystem)\bin"
        if (Test-Path "$binDir\gendef.exe")   { $gendef = Get-Item "$binDir\gendef.exe" }
        if (Test-Path "$binDir\dlltool.exe")  { $dlltool = Get-Item "$binDir\dlltool.exe" }
    }
    if (-not $gendef -or -not $dlltool) {
        Write-Warn "缺少 gendef/dlltool，跳过导入库生成"
        Write-Info "修复: pacman -S $($script:SubsystemPrefix[$script:Subsystem])binutils"
        return
    }
    if (-not (Test-Path $libDir)) { New-Item -ItemType Directory -Path $libDir -Force | Out-Null }
    $defFile = Join-Path $libDir "vulkan-1.def"
    Write-Info "步骤 1/2: gendef 导出符号 → vulkan-1.def"
    & $gendef.Source - $sysDll | Out-File -FilePath $defFile -Encoding ascii
    if (-not (Test-Path $defFile) -or (Get-Item $defFile).Length -eq 0) {
        Write-Err "gendef 未生成 .def 文件"
        return
    }
    Write-Info "步骤 2/2: dlltool 生成 → libvulkan-1.dll.a"
    Push-Location $libDir
    & $dlltool.Source -d "vulkan-1.def" -l "libvulkan-1.dll.a" -D $sysDll 2>&1 | Out-Null
    $rc = $LASTEXITCODE
    Pop-Location
    if ($rc -ne 0 -or -not (Test-Path $outLib)) {
        Write-Err "dlltool 失败 (退出码 $rc)"
        return
    }
    $size = (Get-Item $outLib).Length
    Write-Ok "Vulkan MinGW 导入库生成成功 ($size 字节): $outLib"
}

# ============================================================
# 4. 主流程
# ============================================================

function Main {
    # 初始化日志
    "=" * 60 | Out-File -FilePath $script:LogFile -Encoding UTF8
    "Vulkan 开发环境部署日志  $($script:StartTime)" | Out-File -FilePath $script:LogFile -Encoding UTF8 -Append
    "=" * 60 | Out-File -FilePath $script:LogFile -Encoding UTF8 -Append

    Write-Host ""
    Write-Color "============================================================" -Color Cyan
    Write-Color "   Vulkan 开发环境一键部署 (检测 + 自动安装)" -Color Cyan
    Write-Color "   Windows 10/11  |  需管理员权限" -Color Cyan
    Write-Color "============================================================" -Color Cyan

    # ---- 权限检查 ----
    Write-Section "权限检查"
    if (-not (Test-IsAdmin)) {
        Write-Warn "当前未以管理员身份运行，尝试 UAC 自动提权..."
        # 获取当前脚本完整路径（兼容 -File 调用 / 直接双击 / ISE 等场景）
        $scriptPath = $null
        if ($PSCommandPath)                                     { $scriptPath = $PSCommandPath }
        elseif ($MyInvocation -and $MyInvocation.MyCommand.Path){ $scriptPath = $MyInvocation.MyCommand.Path }
        if (-not $scriptPath) {
            Write-Err "无法确定脚本路径，请右键脚本 → 以管理员身份运行"
            Read-Host "按 Enter 退出"
            exit 1
        }
        try {
            # 通过 -Verb RunAs 触发 UAC 提权，新进程独立窗口运行
            $ps = Start-Process -FilePath "powershell.exe" `
                 -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$scriptPath`"") `
                 -Verb RunAs -PassThru -WindowStyle Normal
            Write-Info "已启动管理员进程 (PID: $($ps.Id))，当前窗口将关闭"
            Start-Sleep -Seconds 2
            exit 0
        } catch {
            Write-Err "UAC 提权失败或被用户拒绝: $_"
            Write-Warn "请右键脚本 → 以管理员身份运行"
            Write-Warn "或: 以管理员身份启动 PowerShell 后执行本脚本"
            Read-Host "按 Enter 退出"
            exit 1
        }
    }
    Write-Ok "管理员权限已获得"

    # ---- MSYS2 ----
    Write-Section "MSYS2"
    if (Find-Msys2) {
        Resolve-Subsystem
        Write-Ok "MSYS2: $($script:Msys2Root)"
        if ($script:Subsystem) {
            Write-Ok "MinGW 子系统: $($script:Subsystem)"
            Add-Status "MSYS2" "已安装 ($($script:Subsystem))"
        } else {
            Write-Warn "未检测到任何 MinGW 子系统（工具链未装？）"
            Add-Status "MSYS2" "已安装 (无工具链)"
        }
    } elseif (Install-MSYS2) {
        # 自动安装后 Msys2Root 已设置；Subsystem 由 Install-MSYS2 设为 ucrt64
        if (-not $script:Subsystem) { Resolve-Subsystem }
        if ($script:Subsystem) {
            Write-Ok "MinGW 子系统: $($script:Subsystem)"
            Add-Status "MSYS2" "已安装 (本次, $($script:Subsystem))"
        } else {
            Add-Status "MSYS2" "已安装(本次, 无工具链)"
        }
    } else {
        Write-Warn "MSYS2 自动安装失败或被取消"
        Add-Status "MSYS2" "未安装(需手动)"
        Open-Url -Url "https://www.msys2.org/" -Name "MSYS2"
        Write-Host ""
        Write-Color "  手动安装步骤:" -Color Yellow
        Write-Color "  1. 下载 msys2-x86_64-xxxx.exe 并运行" -Color Yellow
        Write-Color "  2. 默认安装到 C:\msys64" -Color Yellow
        Write-Color "  3. 在 MSYS2 终端运行: pacman -S mingw-w64-ucrt-x86_64-toolchain" -Color Yellow
        Write-Color "  4. 完成后重新运行此脚本" -Color Yellow
        Read-Host "按 Enter 退出"
        exit 0
    }

    # ---- MinGW-w64 工具链 ----
    Write-Section "MinGW-w64 工具链 (GCC)"
    if (Test-Cmd g++) {
        $ver = (g++ -dumpfullversion 2>$null)
        Write-Ok "g++ 已安装: $ver"
        Add-Status "MinGW-w64" "已安装"
    } else {
        $pkg = "$($script:SubsystemPrefix[$script:Subsystem])toolchain"
        Write-Info "通过 pacman 安装工具链: $pkg"
        if (Invoke-Pacman -PackageSpec $pkg) {
            $binDir = "$($script:Msys2Root)\$($script:Subsystem)\bin"
            if (Test-Path $binDir) { Add-ToUserPath -NewPath $binDir }
            Update-SessionPath
            if (Test-Cmd g++) { Write-Ok "工具链安装完成"; Add-Status "MinGW-w64" "已安装(本次)" }
            else { Write-Err "安装后仍检测不到 g++，请检查 PATH"; Add-Status "MinGW-w64" "未安装(需手动)" }
        } else {
            Add-Status "MinGW-w64" "未安装(需手动)"
        }
    }

    # ---- CMake ----
    Write-Section "CMake"
    if (Test-Cmd cmake) {
        Write-Ok "CMake: $((cmake --version | Select-Object -First 1))"
        Add-Status "CMake" "已安装"
    } elseif (Install-Winget -PackageId "Kitware.CMake" -Name "CMake") {
        Add-Status "CMake" "已安装(本次)"
    } else {
        Open-Url -Url "https://cmake.org/download/" -Name "CMake"
        Add-Status "CMake" "未安装(需手动)"
    }

    # ---- Git ----
    Write-Section "Git"
    if (Test-Cmd git) {
        Write-Ok "Git: $(git --version)"
        Add-Status "Git" "已安装"
    } elseif (Install-Winget -PackageId "Git.Git" -Name "Git") {
        Add-Status "Git" "已安装(本次)"
    } else {
        Open-Url -Url "https://git-scm.com/download/win" -Name "Git"
        Add-Status "Git" "未安装(需手动)"
    }

    # ---- GLFW ----
    Write-Section "GLFW"
    $glfwHeader = "$($script:Msys2Root)\$($script:Subsystem)\include\GLFW\glfw3.h"
    if (Test-Path $glfwHeader) {
        Write-Ok "GLFW 已安装: $glfwHeader"
        Add-Status "GLFW" "已安装"
    } else {
        $pkg = "$($script:SubsystemPrefix[$script:Subsystem])glfw"
        Write-Info "通过 pacman 安装: $pkg"
        if (Invoke-Pacman -PackageSpec $pkg -and (Test-Path $glfwHeader)) {
            Write-Ok "GLFW 安装完成"; Add-Status "GLFW" "已安装(本次)"
        } else {
            Add-Status "GLFW" "未安装(需手动)"
        }
    }

    # ---- GLM ----
    Write-Section "GLM"
    $glmHeader = "$($script:Msys2Root)\$($script:Subsystem)\include\glm\glm.hpp"
    if (Test-Path $glmHeader) {
        Write-Ok "GLM 已安装: $glmHeader"
        Add-Status "GLM" "已安装"
    } else {
        $pkg = "$($script:SubsystemPrefix[$script:Subsystem])glm"
        Write-Info "通过 pacman 安装: $pkg"
        if (Invoke-Pacman -PackageSpec $pkg -and (Test-Path $glmHeader)) {
            Write-Ok "GLM 安装完成"; Add-Status "GLM" "已安装(本次)"
        } else {
            Add-Status "GLM" "未安装(需手动)"
        }
    }

    # ---- Vulkan SDK ----
    Write-Section "Vulkan SDK"
    if ($env:VULKAN_SDK -and (Test-Path "$env:VULKAN_SDK\Include\vulkan\vulkan.h")) {
        Write-Ok "Vulkan SDK: $env:VULKAN_SDK"
        Add-Status "Vulkan SDK" "已安装"
        # 自动生成 MinGW 导入库
        Generate-VulkanMingwLib
    } else {
        Write-Warn "Vulkan SDK 未安装（安装器需用户交互勾选组件）"
        Add-Status "Vulkan SDK" "未安装(需手动)"
        Open-Url -Url "https://vulkan.lunarg.com/sdk/home" -Name "Vulkan SDK"
        Write-Host ""
        Write-Color "  安装步骤:" -Color Yellow
        Write-Color "  1. 下载 VulkanSDK-xxxx-Installer.exe" -Color Yellow
        Write-Color "  2. 务必勾选 Vulkan Runtime + Development Components" -Color Yellow
        Write-Color "  3. 安装后确认 VULKAN_SDK 环境变量已设置" -Color Yellow
        Write-Color "  4. 重新运行此脚本" -Color Yellow
    }

    # ---- 最终环境验证 ----
    Write-Section "环境完整性验证"
    $pass = 0; $fail = 0

    # CMake 版本 >= 3.15
    if (Test-Cmd cmake) {
        $v = (cmake --version | Select-Object -First 1) -replace 'cmake version ',''
        $major = [int]($v -split '\.')[0]
        $minor = [int]($v -split '\.')[1]
        if ($major -gt 3 -or ($major -eq 3 -and $minor -ge 15)) { Write-Ok "CMake $v (>=3.15)"; $pass++ }
        else { Write-Err "CMake $v 版本过低 (需 >=3.15)"; $fail++ }
    } else { Write-Err "CMake 未安装"; $fail++ }

    # GCC >= 8.0
    if (Test-Cmd g++) {
        $v = g++ -dumpfullversion 2>$null
        $major = [int]($v -split '\.')[0]
        if ($major -ge 8) { Write-Ok "GCC $v (>=8.0, 支持 C++17)"; $pass++ }
        else { Write-Err "GCC $v 版本过低 (需 >=8.0)"; $fail++ }
    } else { Write-Err "g++ 未找到"; $fail++ }

    # Vulkan SDK + 头文件 + 系统 DLL
    if ($env:VULKAN_SDK) {
        if (Test-Path "$env:VULKAN_SDK\Include\vulkan\vulkan.h") { Write-Ok "vulkan.h 头文件"; $pass++ }
        else { Write-Err "缺少 vulkan.h 头文件"; $fail++ }
        $mingwLib = "$env:VULKAN_SDK\Lib\libvulkan-1.dll.a"
        $msvcLib  = "$env:VULKAN_SDK\Lib\vulkan-1.lib"
        if (Test-Path $mingwLib) { Write-Ok "libvulkan-1.dll.a (MinGW 导入库)"; $pass++ }
        elseif (Test-Path $msvcLib) { Write-Warn "仅 MSVC vulkan-1.lib (MinGW 链接将自动生成)"; $pass++ }
        else { Write-Err "缺少 Vulkan 导入库"; $fail++ }
    } else { Write-Err "VULKAN_SDK 未设置"; $fail++ }
    if (Test-Path "C:\Windows\System32\vulkan-1.dll") { Write-Ok "系统 vulkan-1.dll"; $pass++ }
    else { Write-Err "缺少 C:\Windows\System32\vulkan-1.dll (装显卡驱动)"; $fail++ }

    # GLFW 头 + 库（按子系统）
    $glfwOk = $false
    foreach ($s in @($script:Subsystem, "mingw64", "ucrt64", "clang64") | Select-Object -Unique) {
        $inc = "$($script:Msys2Root)\$s\include\GLFW\glfw3.h"
        $lib = "$($script:Msys2Root)\$s\lib\libglfw3.a"
        $libDll = "$($script:Msys2Root)\$s\lib\libglfw3.dll.a"
        if (Test-Path $inc) {
            $libPath = if (Test-Path $lib) { $lib } elseif (Test-Path $libDll) { $libDll } else { $null }
            if ($libPath) { Write-Ok "GLFW ($s): $libPath"; $glfwOk = $true }
        }
    }
    if ($glfwOk) { $pass++ } else { Write-Err "GLFW 未找到 (头/库)"; $fail++ }

    # GLM 头文件
    $glmOk = $false
    foreach ($s in @($script:Subsystem, "mingw64", "ucrt64", "clang64") | Select-Object -Unique) {
        $inc = "$($script:Msys2Root)\$s\include\glm\glm.hpp"
        if (Test-Path $inc) { Write-Ok "GLM ($s): $inc"; $glmOk = $true; break }
    }
    if ($glmOk) { $pass++ } else { Write-Err "GLM 未找到"; $fail++ }

    # ---- 验证汇总 ----
    Write-Host ""
    Write-Color "  验证结果: 通过 $pass 项 / 失败 $fail 项" -Color $(if ($fail -eq 0) { 'Green' } else { 'Yellow' })

    # ---- 部署总结 ----
    Write-Section "部署总结"
    $dur = (Get-Date) - $script:StartTime
    Write-Host ""
    Write-Color "  组件状态:" -Color Cyan
    Write-Host ("  " + ("-" * 54))
    foreach ($item in $script:Components) {
        $icon = switch -Wildcard ($item.Status) {
            "已安装*"  { "[OK]";  $c = "Green"   }
            "未安装*" { "[--]";  $c = "Red"     }
            default     { "[??]";  $c = "Yellow" }
        }
        Write-Color ("    {0,-6} {1,-14} {2}" -f $icon, $item.Name, $item.Status) -Color $c
    }
    Write-Host ("  " + ("-" * 54))
    Write-Host ""
    Write-Info "总耗时: $($dur.Minutes) 分 $($dur.Seconds) 秒"
    Write-Info "日志: $script:LogFile"

    $pending = $script:Components | Where-Object { $_.Status -like "未安装*" }
    if ($pending) {
        Write-Host ""
        Write-Warn "以下组件需手动处理:"
        $pending | ForEach-Object { Write-Color "    - $($_.Name)" -Color Yellow }
        Write-Host ""
        Write-Info "安装完成后重新运行此脚本"
    } else {
        Write-Host ""
        Write-Ok "所有组件就绪！可构建:"
        Write-Color "    cmake -S . -B build -G `"MinGW Makefiles`"" -Color Cyan
        Write-Color "    cmake --build build" -Color Cyan
        Write-Color "    .\bin\VulkanApp.exe" -Color Cyan
    }
    Write-Host ""
    Write-Color "============================================================" -Color Cyan
    Write-Color "   脚本执行完毕" -Color Cyan
    Write-Color "============================================================" -Color Cyan
    Write-Host ""
}

# 启动
Main
