. ".\scripts\config-project.ps1"

# 使用cmake的 CompileShaders target 编译 shader
Write-Host "检查Vulkan SDK..."
if (-not $env:VULKAN_SDK) {
    Write-Error "VULKAN_SDK 环境变量未设置"
    exit 1
}

Write-Host "VULKAN_SDK: $env:VULKAN_SDK"

$glslcPath = "$env:VULKAN_SDK\Bin\glslc.exe"
if (-not (Test-Path $glslcPath)) {
    Write-Error "找不到 glslc.exe: $glslcPath"
    exit 1
}

Write-Host "glslc.exe 找到: $glslcPath"



# 执行编译
# 检查shader文件是否存在
Write-Host "检查shader文件..." -ForegroundColor Cyan
$vertShader = ".\shaders\glsl\blinnphong_0.vert"
$fragShader = ".\shaders\glsl\blinnphong_0.frag"

if (-not (Test-Path $vertShader)) {
    Write-Error "找不到vertex shader: $vertShader"
    exit 1
}
if (-not (Test-Path $fragShader)) {
    Write-Error "找不到fragment shader: $fragShader"
    exit 1
}
Write-Host "Shader文件检查通过" -ForegroundColor Green

# 执行编译
Write-Host "开始编译shader..." -ForegroundColor Cyan
Push-Location $BuildDir
try {
    cmake --build . --target CompileShaders
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Shader编译成功完成!" -ForegroundColor Green
        
        # 显示生成的SPIR-V文件
        Write-Host "生成的SPIR-V文件:" -ForegroundColor Yellow
        if (Test-Path "shaders\glsl\blinnphong_0.vert.spv") {
            Write-Host "  - shaders/glsl/blinnphong_0.vert.spv" -ForegroundColor White
        }
        if (Test-Path "shaders\glsl\blinnphong_0.frag.spv") {
            Write-Host "  - shaders/glsl/blinnphong_0.frag.spv" -ForegroundColor White
        }
    } else {
        Write-Error "Shader编译失败!"
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}