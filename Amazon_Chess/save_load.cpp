#include "save_load.h"
#include "game.h"
#include <windows.h>
#include <fstream>

bool SaveHistoryToFile(const std::wstring &path)
{
    // open as binary w/ UTF-8 encoded text (for portability)
    std::ofstream ofs;
    // Convert UTF-16 path to UTF-8 for std::ofstream? Use Windows API to write with wide path.
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    const auto &hist = Game_GetHistory();
    // write as UTF-8 with BOM
    const unsigned char bom[] = {0xEF,0xBB,0xBF};
    DWORD written = 0;
    WriteFile(h, bom, sizeof(bom), &written, nullptr);
    for (const auto &s : hist)
    {
        // convert s (wstring) to UTF-8
        int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        if (len > 0)
        {
            std::string utf8; utf8.resize(len);
            WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &utf8[0], len, nullptr, nullptr);
            // append CRLF
            utf8 += "\r\n";
            WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        }
    }
    CloseHandle(h);
    return true;
}

bool LoadHistoryFromFile(const std::wstring &path, std::vector<std::wstring> &outLines)
{
    outLines.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr);
    if (size == INVALID_FILE_SIZE) { CloseHandle(h); return false; }
    std::string buf; buf.resize(size);
    DWORD read = 0;
    if (!ReadFile(h, &buf[0], size, &read, nullptr)) { CloseHandle(h); return false; }
    CloseHandle(h);
    // handle BOM
    size_t pos = 0;
    if (read >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) pos = 3;
    // split lines
    while (pos < read)
    {
        size_t eol = buf.find('\n', pos);
        size_t len = (eol == std::string::npos) ? (read - pos) : (eol - pos);
        // trim CR
        if (len > 0 && buf[pos + len - 1] == '\r') --len;
        if (len > 0)
        {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, &buf[pos], (int)len, nullptr, 0);
            std::wstring w; w.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, &buf[pos], (int)len, &w[0], wlen);
            outLines.push_back(w);
        }
        else
        {
            outLines.push_back(L"");
        }
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    return true;
}
