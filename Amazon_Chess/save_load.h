#pragma once

#include <string>
#include <vector>

// Save the full history to the given path (UTF-16 path). Returns true on success.
bool SaveHistoryToFile(const std::wstring &path);

// Load history from file into memory (returns lines) and optionally parsed header values.
// Returns true on success and fills outLines. Header values are only set if present in file.
bool LoadHistoryFromFile(const std::wstring &path, std::vector<std::wstring> &outLines, int &outBoardSize, bool &outOpponentIsAI, bool &outAIIsFirst);
