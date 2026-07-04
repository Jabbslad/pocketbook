# Native InkView App Development for PocketBook Era

This repository targets normal native PocketBook applications built with InkView.

## What InkView is

InkView is PocketBook's native application and UI library. It provides the APIs used by third-party native apps to:

- run the application event loop;
- receive key, touch, pointer, and lifecycle events;
- draw text, lines, rectangles, and images;
- manage fonts;
- trigger e-ink screen refreshes;
- close the app and access selected device services.

A normal InkView app is a Linux ARM executable with a `.app` filename, launched from the PocketBook Applications menu.

## Recommended toolchain

Use the community Dockerized PocketBook SDK 6.3.0 image:

```bash
5keeve/pocketbook-sdk:6.3.0-b288-v1
```

The SDK provides the ARM cross-compiler, headers, libraries, and CMake toolchain file needed to link against `libinkview`.

Docker is available on this machine, so local builds should not require installing the SDK directly on the host.

## Minimal app shape

```cpp
#include "inkview.h"

static int main_handler(int event_type, int param_one, int param_two) {
    if (event_type == EVT_INIT) {
        ClearScreen();

        ifont *font = OpenFont("LiberationSans", 36, 0);
        SetFont(font, BLACK);

        DrawTextRect(
            0,
            ScreenHeight() / 2 - 30,
            ScreenWidth(),
            60,
            "Hello PocketBook",
            ALIGN_CENTER
        );

        FullUpdate();
        CloseFont(font);
    } else if (event_type == EVT_KEYPRESS) {
        CloseApp();
    }

    return 0;
}

int main(int argc, char **argv) {
    InkViewMain(main_handler);
    return 0;
}
```

Important calls:

- `InkViewMain(handler)` starts the InkView event loop.
- `EVT_INIT` is where the first screen draw usually happens.
- `ClearScreen()` clears the framebuffer.
- `DrawTextRect(...)` draws text into the framebuffer.
- `FullUpdate()` pushes the framebuffer to the e-ink display.
- `CloseApp()` exits the application.

## Panel gotcha (verified on PocketBook Era firmware)

Apps start with the system panel enabled (`GetPanelType()=1`,
`PanelHeight()=123` on the Era). The panel offsets the app framebuffer by
`PanelHeight()` pixels while `ScreenWidth()`/`ScreenHeight()` still report
the full physical panel size (1264x1680). Consequences:

- `y=0` lands `PanelHeight()` pixels below the physical top of the glass;
- anything drawn below `ScreenHeight() - PanelHeight()` (e.g. a footer
  anchored to `ScreenHeight()`) renders inside the reserved strip, which
  appears at the physical top of the screen.

Fix for full-screen apps: call `SetPanelType(PANEL_DISABLED)` in
`EVT_INIT` before the first draw. Alternatively keep the panel and lay out
within `ScreenHeight() - PanelHeight()`.

Also handle `EVT_SHOW` by redrawing: it is delivered right after
`EVT_INIT` and whenever the app becomes visible again.

## Build pattern

The example app in `examples/hello-inkview/` uses CMake because the Docker SDK exposes the PocketBook toolchain through `CMAKE_TOOLCHAIN_FILE`.

From the repository root:

```bash
./scripts/build-hello.sh
```

Manual equivalent:

```bash
docker run --rm \
  --user "$(id -u):$(id -g)" \
  --mount type=bind,source="$PWD",target=/project \
  5keeve/pocketbook-sdk:6.3.0-b288-v1 \
  -c 'cd /project/examples/hello-inkview && cmake -S . -B build && cmake --build build'
```

Expected output:

```text
examples/hello-inkview/build/hello-inkview.app
```

The RSS reader UI mockup has its own helper:

```bash
./scripts/build-rss-mockup.sh
```

## Deploy to PocketBook Era

The repository includes a USB deploy helper:

```bash
./scripts/deploy.sh
```

With no arguments, it builds `examples/hello-inkview`, mounts a connected PocketBook via `udisksctl` when possible, auto-detects a writable USB-mounted `applications/` directory, backs up an existing app if it would be overwritten, copies the new `.app`, and syncs it to disk.

Explicit form:

```bash
./scripts/deploy.sh examples/hello-inkview/build/hello-inkview.app /path/to/PocketBook/applications
```

You can also set the target directory with an environment variable:

```bash
POCKETBOOK_APPS_DIR=/path/to/PocketBook/applications ./scripts/deploy.sh
```

Manual install steps:

1. Connect the PocketBook by USB.
2. Choose the device's PC-link / file-transfer mode.
3. Copy the built `.app` file to the device's `applications/` directory:

```text
applications/hello-inkview.app
```

4. Eject/unmount the device cleanly.
5. Open the PocketBook Applications menu. Reboot if the app does not appear immediately.

## Device and path notes

Common user-visible/internal paths:

- USB app install directory: `applications/`
- Internal user storage mount used by apps: `/mnt/ext1`
- Optional memory card mount: `/mnt/ext2`

PocketBook Era is a Linux-based ARM device. Public community sources identify the Era as the PB700/U700 family. Exact ABI details are best treated as SDK-controlled; build using the selected SDK image rather than hand-rolling compiler flags.

## When not to use this path

Use a different approach only when the requested app is not a normal native InkView app:

- KOReader Lua plugin: use for features inside KOReader.
- Shell-script `.app`: use for simple filesystem or automation tasks.
- Go InkView wrapper: use only if explicitly requested or if the project decides to switch away from direct C/C++ InkView.

## References

- PocketBook SDK notes: https://wiki.mobileread.com/wiki/SDK_PocketBook
- PocketBook configuration notes: https://wiki.mobileread.com/wiki/Pocketbook_Configuration
- Dockerized SDK 6.3.0: https://github.com/Skeeve/SDK_6.3.0
- C/C++ examples: https://github.com/pmartin/pocketbook-demo
- InkView docs archive: https://github.com/pocketbook-free/InkViewDoc
