# PocketBook Development Toolchain

This repository contains tooling and examples for native PocketBook Era InkView application development.

Start here:

- Agent/developer notes: [`AGENTS.md`](AGENTS.md)
- InkView app development guide: [`docs/inkview-app-development.md`](docs/inkview-app-development.md)
- RSS reader design system: [`docs/rss-reader-design-system.html`](docs/rss-reader-design-system.html)
- Minimal app example: [`examples/hello-inkview/`](examples/hello-inkview/)
- RSS reader UI mockup: [`examples/rss-reader-mockup/`](examples/rss-reader-mockup/)
- RSS reader "Journal" design implementation: [`examples/rss-reader-journal/`](examples/rss-reader-journal/)

## Build the example

Docker is used for the PocketBook SDK toolchain:

```bash
./scripts/build-hello.sh
```

The expected output is:

```text
examples/hello-inkview/build/hello-inkview.app
```

## Deploy the example

Connect the PocketBook in PC-link/file-transfer mode, then run:

```bash
./scripts/deploy.sh
```

The deploy script builds the example, finds the USB-mounted `applications/` directory, backs up an existing app if it would be overwritten, copies the new `.app`, and syncs it to disk.

You can also pass paths explicitly:

```bash
./scripts/deploy.sh examples/hello-inkview/build/hello-inkview.app /path/to/PocketBook/applications
```

## RSS reader mockup

Build and deploy the e-ink RSS reader UI mockup:

```bash
./scripts/deploy-rss-mockup.sh
```

The mockup includes Home, Inbox, Reader, and Saved screens. It avoids on-screen navigation buttons: use hardware page/back keys for movement, and tap articles to open them.

## RSS reader — Journal design

Implementation of the [`RSS Reader - PocketBook Era` design system](docs/rss-reader-design-system.html) (Journal direction, three screens: feed list, article list, reading view). Pure black on white, serif typography, 7-row lists, hardware page buttons turn pages, touch selects/opens.

```bash
./scripts/deploy-rss-journal.sh
```

Interactions: tap a feed to open its article list, tap an article to read it (marks it read and updates unread counts), page buttons paginate lists and article pages, Back unwinds to the feed list and then exits. In the reading footer: Save toggles the star, Aa cycles the body text size, and Next article advances within the feed. Sync now refreshes the header timestamp; Settings is a stub, as in the design.
