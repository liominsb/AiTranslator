#include <Windows.h>
#include <string>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <cctype>
#include <cwctype>
#include <atomic>

// 全局配置结构体
struct AppConfig
{
    int keyMode;
    WORD hotKey;
    std::wstring baseUrl;
    int maxWaitMs;
    double chineseThreshold;
    std::wstring promptEnToZh;
    std::wstring promptZhToEn;
    bool autoGenerateConfig;
} g_config;

// 原子变量：防止快捷键重复触发
std::atomic<bool> g_isProcessing(false);

// 单实例互斥体
HANDLE g_hMutex = NULL;

// UTF8转宽字符（增加错误检查）
std::wstring StringToWstring(const std::string &str)
{
    if (str.empty())
        return L"";

    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0)
        return L"";

    // len包含null终止符，构造时用len-1避免嵌入的多余\0
    std::wstring res(len - 1, 0);
    if (MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &res[0], len) <= 0)
        return L"";

    return res;
}

// 宽字符转UTF8（增加错误检查）
std::string WstringToUTF8(const std::wstring &wstr)
{
    if (wstr.empty())
        return "";

    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return "";

    // len包含null终止符，构造时用len-1避免嵌入的多余\0
    std::string res(len - 1, 0);
    if (WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &res[0], len, nullptr, nullptr) <= 0)
        return "";

    return res;
}

// URL编码（优化性能，支持所有字符）
std::string UrlEncode(const std::string &str)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string res;
    res.reserve(str.size() * 3); // 预分配内存，避免多次扩容

    for (unsigned char c : str)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            res += c;
        else
        {
            res += '%';
            res += hex[c >> 4];
            res += hex[c & 15];
        }
    }
    return res;
}

// 安全读取剪贴板（增加重试机制，解决剪贴板竞争问题）
std::wstring GetClipboardUnicodeSafe()
{
    std::wstring res;

    // 最多重试3次，每次间隔10ms
    for (int retry = 0; retry < 3; retry++)
    {
        if (OpenClipboard(nullptr))
        {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData)
            {
                wchar_t *buf = static_cast<wchar_t *>(GlobalLock(hData));
                if (buf)
                    res = buf;
                GlobalUnlock(hData);
            }
            CloseClipboard();

            if (!res.empty())
                break;
        }
        Sleep(10);
    }

    return res;
}

// 极速模拟Ctrl+C（增加按键间隔，兼容更多程序）
void FastCopy()
{
    INPUT input[4] = {0};

    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_CONTROL;

    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = 'C';

    input[2].type = INPUT_KEYBOARD;
    input[2].ki.wVk = 'C';
    input[2].ki.dwFlags = KEYEVENTF_KEYUP;

    input[3].type = INPUT_KEYBOARD;
    input[3].ki.wVk = VK_CONTROL;
    input[3].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(4, input, sizeof(INPUT));
}

// 智能等待剪贴板变化（优化检测逻辑）
std::wstring WaitForClipboardChange(int maxWaitMs)
{
    std::wstring oldText = GetClipboardUnicodeSafe();
    FastCopy();

    for (int i = 0; i < maxWaitMs / 5; i++)
    {
        std::wstring newText = GetClipboardUnicodeSafe();
        if (!newText.empty() && newText != oldText)
            return newText;
        Sleep(5);
    }
    return L"";
}

// 增强版语言检测（支持扩展汉字和全角符号）
bool IsMainlyChinese(const std::wstring &text)
{
    size_t chineseCount = 0;
    size_t totalCharCount = 0;

    for (wchar_t c : text)
    {
        if (iswspace(c) || iswpunct(c))
            continue;

        totalCharCount++;
        // 扩展中文Unicode范围：基本汉字+扩展A+全角符号
        // 注意：Windows上wchar_t为16位，无法表示扩展B(0x20000+)等增补平面字符（需代理对）
        if ((c >= 0x4E00 && c <= 0x9FFF) || // 基本汉字
            (c >= 0x3400 && c <= 0x4DBF) || // 扩展A
            (c >= 0xFF00 && c <= 0xFFEF))   // 全角符号
        {
            chineseCount++;
        }
    }

    if (totalCharCount == 0)
        return false;

    return (double)chineseCount / totalCharCount > g_config.chineseThreshold;
}

// 增强版按键解析（支持更多特殊键）
WORD ParseKey(const std::string &keyStr)
{
    if (keyStr.empty())
        return 'Q';

    if (keyStr.size() >= 2 && keyStr[0] == 'F' && isdigit(keyStr[1]))
    {
        int fNum = atoi(keyStr.substr(1).c_str());
        if (fNum >= 1 && fNum <= 12)
            return VK_F1 + fNum - 1;
    }

    if (keyStr.size() == 1)
    {
        if (isdigit(keyStr[0]))
            return '0' + (keyStr[0] - '0');
        else
            return toupper(keyStr[0]);
    }

    return 'Q';
}

// 生成默认配置文件
void GenerateDefaultConfig()
{
    std::ofstream file("config.yml");
    if (!file.is_open())
        return;

    file << "# 全局快捷键设置\n";
    file << "hotkey:\n";
    file << "  mode: \"ctrl\"    # 可选: alt / ctrl / shift\n";
    file << "  key: \"Q\"       # 支持: 字母(A-Z)、数字(0-9)、功能键(F1-F12)\n\n";
    file << "# DeepSeek官方网页版地址（无需?q=参数，程序会自动粘贴文本）\n";
    file << "url: \"https://chat.deepseek.com/a/chat\"\n\n";
    file << "# 性能参数（一般不用改）\n";
    file << "delay: 50                  # 剪贴板最大等待时间(毫秒)\n";
    file << "language:\n";
    file << "  chinese_threshold: 0.3   # 中文占比超过30%则认为是中文文本\n\n";
    file << "# 翻译提示词\n";
    file << "prompt:\n";
    file << "  en_to_zh: \"把下面这段文字翻译成通顺自然的中文，保留专业术语和格式，不要添加额外解释：\\n\"\n";
    file << "  zh_to_en: \"把下面这段文字翻译成地道流畅的英文，符合母语表达习惯，保留专业术语：\\n\"\n";

    file.close();
}

// 加载YAML配置（增加自动生成配置文件功能）
bool LoadConfig()
{
    bool configLoaded = true;
    try
    {
        YAML::Node node = YAML::LoadFile("config.yml");

        std::string mode = node["hotkey"]["mode"].as<std::string>("ctrl");
        if (mode == "ctrl")
            g_config.keyMode = MOD_CONTROL;
        else if (mode == "shift")
            g_config.keyMode = MOD_SHIFT;
        else
            g_config.keyMode = MOD_ALT;

        std::string key = node["hotkey"]["key"].as<std::string>("Q");
        g_config.hotKey = ParseKey(key);

        std::string url = node["url"].as<std::string>("https://chat.deepseek.com/a/chat?q=");
        g_config.baseUrl = StringToWstring(url);

        g_config.maxWaitMs = node["delay"].as<int>(50);
        g_config.chineseThreshold = node["language"]["chinese_threshold"].as<double>(0.3);

        std::string enToZh = node["prompt"]["en_to_zh"].as<std::string>(
            "把下面这段文字或单词翻译成通顺自然的中文，保留专业术语和格式，不要添加额外解释：\n");
        std::string zhToEn = node["prompt"]["zh_to_en"].as<std::string>(
            "把下面这段文字翻译成地道流畅的英文，符合母语表达习惯，保留专业术语：\n");
        g_config.promptEnToZh = StringToWstring(enToZh);
        g_config.promptZhToEn = StringToWstring(zhToEn);
    }
    catch (const YAML::BadFile &)
    {
        MessageBoxW(NULL, L"配置文件不存在，已自动生成默认配置", L"提示", MB_ICONINFORMATION);
        GenerateDefaultConfig();
        configLoaded = false;
    }
    catch (const std::exception &e)
    {
        MessageBoxA(NULL, ("配置文件错误: " + std::string(e.what()) + "\n使用默认配置").c_str(), "错误", MB_ICONERROR);
        configLoaded = false;
    }

    // 兜底默认值
    if (g_config.hotKey == 0)
        g_config.hotKey = 'Q';
    if (g_config.keyMode == 0)
        g_config.keyMode = MOD_CONTROL;
    if (g_config.baseUrl.empty())
        g_config.baseUrl = L"https://chat.deepseek.com/a/chat";
    if (g_config.maxWaitMs < 10)
        g_config.maxWaitMs = 50;
    if (g_config.chineseThreshold < 0.1 || g_config.chineseThreshold > 0.9)
        g_config.chineseThreshold = 0.3;
    if (g_config.promptEnToZh.empty())
        g_config.promptEnToZh = L"把下面这段文字或单词翻译成通顺自然的中文，保留专业术语和格式，不要添加额外解释：\n";
    if (g_config.promptZhToEn.empty())
        g_config.promptZhToEn = L"翻译成英文：\n";

    return configLoaded;
}

#define HOTKEY_ID_TRANSLATE 1001
#define HOTKEY_ID_EXIT 1002

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_HOTKEY)
    {
        if (wParam == HOTKEY_ID_TRANSLATE)
        {
            // 原子操作：防止重复触发
            if (g_isProcessing.exchange(true))
                return 0;

            std::wstring text = WaitForClipboardChange(g_config.maxWaitMs);
            if (!text.empty())
            {
                bool isChinese = IsMainlyChinese(text);
                std::wstring fullText;
                if (isChinese)
                    fullText = g_config.promptZhToEn + text;
                else
                    fullText = g_config.promptEnToZh + text;

                std::string utf8FullText = WstringToUTF8(fullText);
                std::string encodedFullText = UrlEncode(utf8FullText);
                std::wstring finalUrl = g_config.baseUrl + std::wstring(encodedFullText.begin(), encodedFullText.end());

                // 始终使用剪贴板粘贴模式（DeepSeek网页版不支持URL参数）
                // 先打开DeepSeek页面
                ShellExecuteW(NULL, L"open", g_config.baseUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

                // 等待页面加载
                Sleep(1200);

                // 复制完整文本到剪贴板
                if (OpenClipboard(nullptr))
                {
                    EmptyClipboard();
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (fullText.length() + 1) * sizeof(wchar_t));
                    if (hMem)
                    {
                        wchar_t *pBuf = static_cast<wchar_t *>(GlobalLock(hMem));
                        if (pBuf)
                        {
                            wcscpy(pBuf, fullText.c_str());
                            GlobalUnlock(hMem);
                            SetClipboardData(CF_UNICODETEXT, hMem);
                        }
                        else
                        {
                            GlobalFree(hMem);
                        }
                    }
                    CloseClipboard();
                }

                // 自动粘贴并发送（正确的Ctrl+V：按下V → 释放V）
                INPUT input[4] = {0};

                input[0].type = INPUT_KEYBOARD;
                input[0].ki.wVk = VK_CONTROL;

                input[1].type = INPUT_KEYBOARD;
                input[1].ki.wVk = 'V';

                input[2].type = INPUT_KEYBOARD;
                input[2].ki.wVk = 'V';
                input[2].ki.dwFlags = KEYEVENTF_KEYUP;

                input[3].type = INPUT_KEYBOARD;
                input[3].ki.wVk = VK_CONTROL;
                input[3].ki.dwFlags = KEYEVENTF_KEYUP;

                SendInput(4, input, sizeof(INPUT));
                Sleep(50);
                keybd_event(VK_RETURN, 0, 0, 0);
                keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
            }

            g_isProcessing = false;
        }
        else if (wParam == HOTKEY_ID_EXIT)
        {
            PostQuitMessage(0);
        }
    }
    else if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
    }
    else if (msg == WM_QUERYENDSESSION)
    {
        // 系统关机时正常退出
        return TRUE;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    // 单实例检查
    g_hMutex = CreateMutexW(NULL, TRUE, L"DeepSeekTranslateHotkey_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(NULL, L"程序已经在运行中", L"提示", MB_ICONINFORMATION);
        CloseHandle(g_hMutex);
        return 0;
    }

    LoadConfig();

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"DeepSeekTranslateHotkey";

    if (!RegisterClassEx(&wc))
    {
        MessageBoxW(NULL, L"窗口类注册失败", L"错误", MB_ICONERROR);
        CloseHandle(g_hMutex);
        return 1;
    }

    HWND hwnd = CreateWindowEx(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!hwnd)
    {
        MessageBoxW(NULL, L"窗口创建失败", L"错误", MB_ICONERROR);
        UnregisterClass(wc.lpszClassName, hInst);
        CloseHandle(g_hMutex);
        return 1;
    }

    if (!RegisterHotKey(hwnd, HOTKEY_ID_TRANSLATE, g_config.keyMode, g_config.hotKey))
    {
        MessageBoxW(NULL, L"快捷键已被其他程序占用，请修改config.yml", L"错误", MB_ICONERROR);
        DestroyWindow(hwnd);
        UnregisterClass(wc.lpszClassName, hInst);
        CloseHandle(g_hMutex);
        return 1;
    }

    if (!RegisterHotKey(hwnd, HOTKEY_ID_EXIT, MOD_ALT | MOD_SHIFT, 'Q'))
    {
        // 退出快捷键注册失败不影响主功能，仅记录
        // 主翻译快捷键仍可正常工作，用户可通过关闭窗口退出
    }

    MSG m{};
    while (GetMessage(&m, nullptr, 0, 0))
    {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }

    // 完整清理资源
    UnregisterHotKey(hwnd, HOTKEY_ID_TRANSLATE);
    UnregisterHotKey(hwnd, HOTKEY_ID_EXIT);
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, hInst);
    CloseHandle(g_hMutex);

    return 0;
}