<div align="center">

<img src="OdiumProjects/ui/cat.png" width="72" alt="Odium cat" style="image-rendering: pixelated;">

# Odium Projects

**A fast, good-looking project manager for Android Studio.**
Spin up a ready-to-build Android project in one click, keep all your projects in one list, and open any of them in Android Studio with a single click.

![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-ff2d4d?style=flat-square)
![Language](https://img.shields.io/badge/C%2B%2B-17-ff2d4d?style=flat-square)
![UI](https://img.shields.io/badge/UI-WebView2-ff2d4d?style=flat-square)

<img src="docs/projects.png" width="880" alt="Projects list">

</div>

---

## Features

- **One-click new projects.** Pick a name, package, location, language (Java or Kotlin) and minimum SDK. You get a complete project that builds right away, **Gradle wrapper included**, so there is nothing to install or set up first.
- **A project list that stays out of your way.** Sorted by last opened, instantly searchable (`Ctrl+F`), with a clear flag on projects whose folder has gone missing.
- **Click to open.** Selecting a project launches it in Android Studio.
- **Finds Android Studio for you.** Detects the official installer, the registry entry, JetBrains Toolbox installs and `PATH`. You can point it at a specific install in Settings.
- **Import existing projects.** Add any Android Studio / Gradle project folder; the package name is read from `build.gradle`.
- **Housekeeping.** Show a project in Explorer, remove it from the list, or delete it from disk (with a safety check so only real Gradle projects can be deleted).
- **Optional CI.** Tick one box to add a GitHub Actions workflow that builds a debug APK on every push.
- **Themeable.** Black and red by default, with a built-in accent color picker.
- **Small and native.** A Win32 C++ app: no Electron, no Qt, no runtime to bundle. The UI is plain HTML/CSS/JS rendered by Microsoft Edge WebView2.

<div align="center">
<img src="docs/new-project.png" width="430" alt="New project dialog">
&nbsp;
<img src="docs/settings.png" width="430" alt="Settings">
</div>

## What a new project contains

| | |
|---|---|
| Build tooling | Gradle **8.9** (wrapper), Android Gradle Plugin **8.5.2** |
| SDK | `compileSdk 34`, `targetSdk 34`, minimum SDK of your choice (26 / 28 / 30 / 33) |
| Language | Java 17, or Kotlin 1.9.24 |
| Extras | Adaptive launcher icon, dark theme, `.gitignore`, README, optional `.github/workflows/build.yml` |
| Dependencies | None. The starter is a plain `Activity`, so the first sync is fast |

Build it straight away from the terminal with `gradlew assembleDebug`, or just open it in Android Studio.

## Installing

Grab the latest `OdiumProjects-Setup-x.y.z.exe` from the **Releases** page and run it. It installs per-user (no admin rights needed), adds a Start Menu entry, and installs the Edge WebView2 Runtime if your PC doesn't already have it.

> **Requirements:** Windows 10 or 11 (x64) and Android Studio. The WebView2 Runtime is included with Windows 11 and most up-to-date Windows 10 installs.

## Building from source

You need **Visual Studio 2022** (Community is fine) with the **Desktop development with C++** workload.

1. Clone the repo and open `OdiumProjects.sln`.
2. Let NuGet restore packages (or right-click the solution → **Restore NuGet Packages**). This pulls in `Microsoft.Web.WebView2`.
3. Select **x64** and **Debug** or **Release**, then press **F5**.

The build copies `ui\` and `template\` next to the executable (`bin\<Configuration>\`), so that folder is self-contained and can be zipped up and shared as-is.

### Building the installer

The installer is an [Inno Setup](https://jrsoftware.org/isdl.php) 6.3+ script in [`installer/`](installer).

1. Build the app as **Release | x64**.
2. Run `installer\build-installer.bat` (or open `installer\OdiumProjects.iss` in Inno Setup and press **F9**).
3. The finished installer lands in `installer\output\`.

To rename the product, change `#define AppName` at the top of the `.iss` file.

## How it works

```
┌───────────────────────────────┐        ┌──────────────────────────────┐
│  ui/  (HTML + CSS + JS)       │        │  src/  (C++17, Win32)        │
│  rendered by WebView2         │        │                              │
│                               │  msg   │  main.cpp      window + host │
│  app.js  ── postMessage ───────────────▶  backend.cpp   commands      │
│          ◀── PostWebMessage ───────────  generator.cpp  templating    │
└───────────────────────────────┘        └──────────────────────────────┘
```

- **Host (`main.cpp`)**: creates the window, starts WebView2, maps the `ui` folder to a virtual host name (`https://odium.projects/`), and forwards messages to the backend.
- **Protocol**: the page sends a single string, `id ␟ command ␟ arg1 ␟ arg2 …` (separated by `U+001F`). The backend answers with JSON containing the same `id`, an optional toast message, and, after anything that changes data, the full fresh state. The UI just re-renders from that state.
- **Backend (`backend.cpp`)**: project list and settings storage, validation, Android Studio detection and launching, native folder/file pickers, import and delete.
- **Generator (`generator.cpp`)**: copies `template/common` plus `template/java` or `template/kotlin` (and `template/workflow` if requested) into the new folder, substituting placeholders as it goes.

### Customizing the template

Everything under `OdiumProjects/template/` is what new projects are made from. These placeholders are replaced on creation:

| Placeholder | Becomes |
|---|---|
| `__APP_NAME__` / `__APP_NAME_XML__` | The app name (raw / escaped for `strings.xml`) |
| `__PACKAGE__` | The package name |
| `__PACKAGE_PATH__` | The package as a folder path (usable in directory names) |
| `__PROJECT_NAME__` | A folder-safe version of the name |
| `__MIN_SDK__` | The chosen minimum SDK |
| `__KOTLIN_ROOT_PLUGIN__`, `__KOTLIN_APP_PLUGIN__`, `__KOTLIN_OPTIONS__` | Whole lines, filled in for Kotlin projects and removed for Java |

Files and folders starting with `dot_` become dotfiles (`dot_gitignore` → `.gitignore`), which keeps them visible to tools that skip hidden files. To change the Gradle or AGP version, edit `template/common` (including `gradle/wrapper/gradle-wrapper.properties`).

### Customizing the interface

The whole UI is three files in `OdiumProjects/ui/`: `index.html`, `style.css` and `app.js`. Open `index.html` in any browser to preview it with fake data (no build needed), which makes design tweaks quick. Colors and fonts are CSS variables at the top of `style.css`.

## Where things are stored

| What | Where |
|---|---|
| Project list and settings | `%APPDATA%\OdiumProjects\` (`projects.tsv`, `settings.txt`) |
| WebView2 cache | `%LOCALAPPDATA%\OdiumProjects\WebView2\` |

Removing a project from the list never touches its files. The uninstaller asks before deleting the list and settings.

## Repository layout

```
OdiumProjects.sln
OdiumProjects/
├── src/                 C++ sources (host, backend, project generator)
├── ui/                  HTML / CSS / JS interface
├── template/            Android starter project (common, java, kotlin, workflow)
├── app.ico, app.rc      Application icon
└── packages.config      NuGet: Microsoft.Web.WebView2
installer/               Inno Setup script, wizard artwork, build script
docs/                    Screenshots used in this README
```

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+N` | New project |
| `Ctrl+F` | Search projects |
| `Esc` | Close dialog / clear search |

## Troubleshooting

**"Android Studio not found".** Open **Settings** and use **Auto-detect**, or browse to `studio64.exe` (or the install folder) yourself.

**The app says WebView2 isn't installed.** Install the [Evergreen runtime](https://developer.microsoft.com/microsoft-edge/webview2/) and start the app again.

**Build error about a missing NuGet package.** Right-click the solution → **Restore NuGet Packages**, then rebuild.

**A project shows "Folder missing".** It was moved or deleted outside the app. Remove it from the list, or move it back.

## Ideas / roadmap

- Pin favorite projects to the top
- More templates (Compose, Material 3)
- Per-project notes and tags
- Open in other IDEs

## License

Add a `LICENSE` file and name it here.

---

<sub>Android Studio and Android are trademarks of Google LLC. This project is an independent tool and is not affiliated with or endorsed by Google.</sub>
