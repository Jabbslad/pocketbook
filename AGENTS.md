# PocketBook Repo Agent Notes

This repo is for developing normal native PocketBook InkView applications.

## Target
- Device: PocketBook Era.
- App type: native PocketBook InkView `.app` executable.
- Preferred language: C/C++.
- UI/API: PocketBook `libinkview` via `inkview.h`.
- Not using: Android, Qt, webview, KOReader plugin, or the Go InkView wrapper unless explicitly requested.

## Build approach
Use the Dockerized PocketBook SDK 6.3.0 environment.

Known local fact:
- Docker is installed on this machine.

Expected output:
- An ARM Linux executable named `*.app`.
- Install by copying the `.app` file to the PocketBook USB storage `applications/` directory.

## Start here
- Development guide: `docs/inkview-app-development.md`
- RSS reader design system/source of truth: `docs/rss-reader-design-system.html`
- Minimal app example: `examples/hello-inkview/`
- RSS reader UI mockup: `examples/rss-reader-mockup/`
- RSS reader "Journal" design implementation: `examples/rss-reader-journal/`
- Build helpers: `scripts/build-hello.sh`, `scripts/build-rss-mockup.sh`, `scripts/build-rss-journal.sh`
- USB deploy helpers: `scripts/deploy.sh`, `scripts/deploy-rss-mockup.sh`, `scripts/deploy-rss-journal.sh`

## Constraints
- Keep examples minimal and directly tied to InkView.
- Prefer direct InkView calls such as `InkViewMain`, `EVT_INIT`, `ClearScreen`, `DrawTextRect`, `FullUpdate`, and `CloseApp`.
- Verify changes by building a `.app` where practical; runtime verification requires a connected PocketBook device.
- Do not replace this approach with Go, KOReader plugins, or shell-script apps unless the user asks for that specifically.

## Implementation learnings
- The SDK toolchain supplies `PB_LINK_DIRECTORIES`, `PB_INCLUDE_DIRECTORIES`, and the other PocketBook build variables; do not derive unused tools such as `PBRES` in an app CMake file.
- Minimize unnecessary e-ink work: avoid redundant full-screen clears when every active draw path already paints its complete screen, and reuse precomputed text measurements during rendering.
- Base pagination on the items actually shown after filtering rather than on the feed's unfiltered article count.
- Keep article image memory bounded by clearing the litehtml image cache at the start of every document build, including builds that exit early when scroll mode is disabled. Composite grayscale-alpha images over white while downscaling instead of allocating a second full-size image buffer.
- When changing shared example or build behavior, build every affected `.app` with its Docker helper. Syntax-check changed POSIX shell scripts with `sh -n`.
