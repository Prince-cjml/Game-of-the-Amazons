#pragma once

#include <string>
#include <vector>

// Save the full history to the given path (UTF-16 path). Returns true on success.
bool SaveHistoryToFile(const std::wstring &path);

// Load history from file into memory (returns lines). Does not modify game state.
// Returns true on success and fills outLines.
bool LoadHistoryFromFile(const std::wstring &path, std::vector<std::wstring> &outLines);
