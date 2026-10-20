#pragma once
#include "util.h"

struct GenOptions {
    std::wstring name; 
    std::wstring package; 
    fs::path dest; 
    bool kotlin = false;
    int minSdk = 26;
    bool workflow = false;
};

std::wstring projectFolderName(const std::wstring &name);

std::wstring generateProject(const fs::path &templateRoot, const GenOptions &o);
