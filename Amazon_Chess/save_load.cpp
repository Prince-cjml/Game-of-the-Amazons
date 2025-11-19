#include "save_load.h"
#include "game.h"
#include <windows.h>
#include <fstream>
#include <sstream>

bool SaveHistoryToFile(const std::wstring &path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    const auto &hist = Game_GetHistory();
    // write as UTF-8 with BOM
    const unsigned char bom[] = {0xEF,0xBB,0xBF};
    DWORD written = 0;
    WriteFile(h, bom, sizeof(bom), &written, nullptr);

    // write header lines
    std::wstringstream hdr;
    hdr << L"BoardSize:" << Game_GetBoardSize() << L"\r\n";
    hdr << L"OpponentAI:" << (Game_IsOpponentAI() ? 1 : 0) << L"\r\n";
    hdr << L"AIFirst:" << (Game_IsAIBlack() ? 1 : 0) << L"\r\n";
    std::wstring hdrw = hdr.str();
    int hdrlen = WideCharToMultiByte(CP_UTF8, 0, hdrw.c_str(), (int)hdrw.size(), nullptr, 0, nullptr, nullptr);
    if (hdrlen > 0)
    {
        std::string utf; utf.resize(hdrlen);
        WideCharToMultiByte(CP_UTF8, 0, hdrw.c_str(), (int)hdrw.size(), &utf[0], hdrlen, nullptr, nullptr);
        utf += "\r\n"; // separate header from moves with blank line
        WriteFile(h, utf.data(), (DWORD)utf.size(), &written, nullptr);
    }

    for (const auto &s : hist)
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        if (len > 0)
        {
            std::string utf8; utf8.resize(len);
            WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &utf8[0], len, nullptr, nullptr);
            utf8 += "\r\n";
            WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        }
    }
    CloseHandle(h);
    return true;
}

static std::wstring Trim(const std::wstring &s)
{
    size_t a = 0; while (a < s.size() && iswspace(s[a])) ++a;
    size_t b = s.size(); while (b>0 && iswspace(s[b-1])) --b;
    if (b<=a) return L"";
    return s.substr(a, b-a);
}

bool LoadHistoryFromFile(const std::wstring &path, std::vector<std::wstring> &outLines, int &outBoardSize, bool &outOpponentIsAI, bool &outAIIsFirst)
{
    outLines.clear();
    outBoardSize = 8; outOpponentIsAI = true; outAIIsFirst = false; // defaults

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr);
    if (size == INVALID_FILE_SIZE) { CloseHandle(h); return false; }
    std::string buf; buf.resize(size);
    DWORD read = 0;
    if (!ReadFile(h, &buf[0], size, &read, nullptr)) { CloseHandle(h); return false; }
    CloseHandle(h);
    size_t pos = 0;
    // handle BOM
    if (read >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) pos = 3;

    // read UTF-8 content into wstring lines
    std::vector<std::wstring> lines;
    while (pos < read)
    {
        size_t eol = buf.find('\n', pos);
        size_t len = (eol == std::string::npos) ? (read - pos) : (eol - pos);
        // trim CR
        if (len > 0 && buf[pos + len - 1] == '\r') --len;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, &buf[pos], (int)len, nullptr, 0);
        std::wstring w;
        if (wlen > 0)
        {
            w.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, &buf[pos], (int)len, &w[0], wlen);
        }
        else w = L"";
        lines.push_back(w);
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }

    // parse header until blank line encountered
    size_t i = 0;
    for (; i < lines.size(); ++i)
    {
        std::wstring t = Trim(lines[i]);
        if (t.empty()) { ++i; break; }
        // header entries: key:value
        size_t colon = t.find(L':');
        if (colon == std::wstring::npos) continue;
        std::wstring key = Trim(t.substr(0, colon));
        std::wstring val = Trim(t.substr(colon+1));
        if (_wcsicmp(key.c_str(), L"BoardSize") == 0)
        {
            int v = _wtoi(val.c_str()); if (v==8||v==10) outBoardSize = v;
        }
        else if (_wcsicmp(key.c_str(), L"OpponentAI") == 0)
        {
            outOpponentIsAI = (val == L"1" || _wtoi(val.c_str()) != 0);
        }
        else if (_wcsicmp(key.c_str(), L"AIFirst") == 0)
        {
            outAIIsFirst = (val == L"1" || _wtoi(val.c_str()) != 0);
        }
    }

    // remaining lines are history entries
    for (size_t j = i; j < lines.size(); ++j)
    {
        outLines.push_back(lines[j]);
    }
    return true;
}
