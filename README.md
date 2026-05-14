# DeepSeekTranslator

Windows 桌面快捷键翻译工具，按下快捷键自动读取剪贴板内容并在 DeepSeek 网页版中翻译。

## 功能

- 全局快捷键触发翻译
- 自动读取剪贴板文本
- 调用系统默认浏览器打开 DeepSeek 网页版并填入内容
- 通过 YAML 配置文件自定义快捷键
- 优点是即开即用,快捷键，url，翻译提示词，性能参数都可以自定义

## 依赖

- MinGW (g++ 支持 C++11)
- CMake 3.10+
- yaml-cpp (静态库)

## 编译

### 1. 准备 yaml-cpp

项目已包含编译好的 yaml-cpp 静态库，位于 `build/yaml-cpp-install/`。

如需重新编译：

```bash
cd build
mkdir yaml-cpp-build && cd yaml-cpp-build
cmake -G "MinGW Makefiles" \
  -DCMAKE_INSTALL_PREFIX="../yaml-cpp-install" \
  -DYAML_BUILD_SHARED_LIBS=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  ../yaml-cpp-0.8.0
mingw32-make -j4
mingw32-make install
```

### 2. 编译项目

```bash
mkdir build_output && cd build_output
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..
mingw32-make
```

生成的 `DeepSeekTranslator.exe` 位于 `build_output/` 目录。

## 配置

编辑 `config.yaml` 设置快捷键（如 `Ctrl+Alt+T`），具体配置格式见源码中的 `main.cpp`。

## 使用

1. 运行 `DeepSeekTranslator.exe`（后台常驻，无窗口）
2. 框选任意文本
3. 按下配置的快捷键
4. 自动打开 DeepSeek 网页版并填入剪贴板内容
