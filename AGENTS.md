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
