#pragma once
#include "util.h"

struct GenOptions {
    std::wstring name;     // "My Cool App"
    std::wstring package;  // com.example.mycoolapp
    fs::path dest;         // folder to create
    bool kotlin = false;
    int minSdk = 26;
    bool workflow = false;
};

// "My Cool App" -> "MyCoolApp"
std::wstring projectFolderName(const std::wstring &name);

// Writes the starter project (Gradle wrapper included). Returns an error message, or empty on success.
std::wstring generateProject(const fs::path &templateRoot, const GenOptions &o);
