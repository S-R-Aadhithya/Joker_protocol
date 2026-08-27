# Android App Build & Debug Skill (Joker Protocol)

This document serves as the master instruction set and paradigm guide for building the Joker protocol on Android. Consult this file to remember how to handle errors, compilation issues, and build paradigms.

## 1. Context Window Protection (CRITICAL)
- **DO NOT** read large files entirely. The codebase is 270,000 lines long.
- When a compilation error occurs, the compiler will specify the file and line number.
- Use `view_file` with precise `StartLine` and `EndLine` parameters to read only the 20-40 lines surrounding the error.
- Use `replace_file_content` or `multi_replace_file_content` to fix the specific lines.
- **Never** print large compiler outputs directly to the user or read them in full if they are massive.

## 2. Iterative Compilation Loop
- Building the app will fail multiple times due to C++ to NDK porting issues (missing POSIX headers, type mismatches).
- Treat this as a standard autonomous loop:
  1. Run the build command (e.g., `./gradlew assembleDebug`).
  2. Parse the error output for the file and line number.
  3. View the lines.
  4. Fix the code.
  5. Loop back to step 1 until success.
- Do not stop to ask the user for permission on every single compilation error. Keep pushing forward.

## 3. Delegation via Subagents
- If a compilation error is deeply rooted in the architecture (e.g., a core `include` file is fundamentally incompatible with Android), do not try to load 5 different C++ files into the main context to trace the dependency tree.
- Instead, invoke a `research` subagent.
- Provide the subagent with the exact error and ask it to find the root cause and propose a localized fix.
- Apply the fix in the main context once the subagent returns.

## 4. Build Environment & Java 21
- The Android project requires Java 21. 
- You must always wrap your Gradle commands to use Java 21. For example, if Java 21 is located in a specific path, export `JAVA_HOME` before running Gradle, or write a `.sh` script that does this:
  ```bash
  export JAVA_HOME=/path/to/java/21
  ./gradlew assembleDebug
  ```
- If the exact path is unknown, search for it first (e.g., `update-alternatives --list java` or check `/usr/lib/jvm`).

## 5. Deployment Paradigm
- The user's phone is connected in debug mode.
- Use `./gradlew installDebug` or `adb install` to push the app to the device.
- Read logs using `adb logcat` (filtered by the app's package name) to verify runtime behavior and catch JNI crashes.
