# The Simpsons: Hit & Run - Apple TV Port

## Overview

This is a port of *The Simpsons: Hit & Run* to tvOS (Apple TV), built upon the [ZenoArrows](https://github.com/ZenoArrows) repository.

**Note:** Game assets are not included. Please place your own game data into the `/assets` folder to build.

---

## 🛠 Setup & Build

1. **Assets:** Drop your game files into the `/assets` directory.
2. **Dependencies:** Install Premake5 via Homebrew:
```bash
brew install premake5

```


3. **Generate:** Run `premake5 xcode4` in your terminal.
4. **Deploy:** Open the project in Xcode and build for Apple TV.

---

## 🐛 Progress Tracker

| Feature / Bug | Status |
| --- | --- |
| **Lisa's School Environment** | ✅ FIXED |
| **Camera Controls** | ✅ FIXED |
| **UI & Text Scaling** | ✅ FIXED |
| **NPC Speech Speed** | 🛑 OPEN |
| **General Audio Bugs** | 🛑 OPEN |

---

## 🤝 The Team & Credits

This project has been a massive undertaking, and it wouldn't be where it is today without the dedicated work of **Jveda**.

When I started this, **Jveda** was the only person who stepped up to help. He has been a primary collaborator on this port, specifically:

* **Core Improvements:** He was instrumental in getting the **Camera Controls** and **UI Scaling** to a playable state.
* **The School Grind:** We spent countless hours (and Jveda lost plenty of sleep) debugging the **Lisa’s School environment**. Even when it seemed "unfixable," his dedication to digging through the geometry and textures was what eventually allowed us to get this environment fully fixed and functional.

### 🚀 Join Us

We are still looking for help with the remaining audio issues! If you want to contribute to a project that has a lot of heart behind it, feel free to open a Pull Request.