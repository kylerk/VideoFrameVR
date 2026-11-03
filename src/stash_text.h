#pragma once

#include <string>

// Text as the Stash catalog compares it (UTF-8; anything else is made
// valid first).

// Case folded, for searching and comparing names.
std::string stashFold(const std::string& text);
// Case folded with spaces and punctuation removed: the names of duplicate
// tags ("Doggy Style", "doggystyle") have the same key.
std::string stashMergeKey(const std::string& text);
// Sorts as names are listed: by the locale's collation, ignoring case.
std::string stashCollateKey(const std::string& text);
