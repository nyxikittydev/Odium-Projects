#pragma once
#include "util.h"

struct Project {
    std::wstring name;
    std::wstring path;
    std::wstring package;
    long long created = 0;
    long long opened = 0; 
};

class Backend {
public:
    explicit Backend(const fs::path &exeDir);

    std::wstring handle(int id, const std::wstring &cmd, const std::vector<std::wstring> &args, HWND owner);

private:
    struct Reply {
        bool ok = true;
        std::wstring error;
        std::wstring toast;
        bool toastError = false;
        bool withState = true;
        std::wstring value;
        bool hasValue = false;
    };

    std::wstring stateJson() const;
    std::wstring toJson(int id, const Reply &r) const;

    Reply create(const std::vector<std::wstring> &a);
    void openProject(const std::wstring &path, Reply &r);
    void addExisting(const std::wstring &folder, Reply &r);
    void deleteProject(const std::wstring &path, Reply &r);
    std::wstring validate(const std::wstring &name, const std::wstring &pkg, const std::wstring &location, fs::path *dest) const;
    bool studioFound() const;
    std::wstring launchStudio(const std::wstring &projectPath) const;

    Project *find(const std::wstring &path);
    void load();
    void save() const;
    void loadSettings();
    void saveSettings() const;

    fs::path m_exeDir, m_dataDir, m_projectsFile, m_settingsFile;
    std::vector<Project> m_projects;
    std::wstring m_studio, m_location, m_prefix, m_accent;
};
