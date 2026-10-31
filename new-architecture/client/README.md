# Native Qt/C++ client

`new-architecture/client` is a standalone Qt Widgets client. Its pages, dialogs, charts, tables, tree controls, login screen, and navigation are C++; it does not host the Vue application or a WebView. It talks to the Qt server through asynchronous HTTP requests. The server's `Database`, `ApiServer`, and `DeviceServer` classes are not linked into the client.

## Start

From the project root on macOS:

```bash
./new-architecture/start-client.command
```

The login screen accepts a server IP or hostname. It uses port 8088 by default; `host:port` also works when the server has a custom management port. The client stores language, server history, and optional remembered credentials with Qt `QSettings`. Bearer tokens stay in process memory and are cleared on logout or HTTP 401.

Build directly with Qt 6.4+, CMake 3.21+, and a C++17 compiler:

```bash
cmake -S new-architecture/client -B new-architecture/client/build -DCMAKE_BUILD_TYPE=Release
cmake --build new-architecture/client/build -j
```

For Windows, `build-windows.ps1 -QtRoot C:\Qt\<version>\<compiler>` builds an `.exe` and runs `windeployqt`. Windows deployment still needs testing on a Windows machine.

The macOS Dock icon, Windows executable icon, and Qt window icon now use the same padded artwork proportion as the server. On macOS, regenerate the client PNG, ICNS, and ICO assets with `swift new-architecture/client/tools/build-icons.swift` after changing the logo.

## Implemented pages

- Login and logout, server history, optional password recall, direct Chinese/English toggle matching the server login screen, role permission navigation, session expiry handling, and a non-modal login error message.
- Home and dashboard with native Qt charts, device tree selection, summary cards, production trends, usage tables, and periodic refresh.
- Device list/group management, type/electric-control catalogs, group context actions and parent selection, device drag and drop, batch move/removal, device editing with quick type creation, and sorting. Catalog tables support batch clearing and linked-record navigation.
- Remote device data view with device/date filters, status, speed, stitches, current pattern, alarm details, and production trend; VNC video and control are excluded.
- Server and device pattern files, multi-file drag-and-drop upload with separate metadata per file, type/order quick creation, download, ZIP export, preview information, batch edit/delete/delivery, transfer conflict choices, and upload/download queue operations. The two pattern sections follow their separate role permissions.
- Salary, process, duration, and alarm statistics, tree/date filters, charts, pagination, native Excel `.xlsx` export, salary details, employee/device linked selection, and the salary table's current-page totals. The client converts server CSV results to XLSX; salary current/all/merged exports also honor the visible local filters.
- Employee CRUD, batch deletion, CSV/text import (including pasted tabular data), import template, and export.
- Profile editing, avatar upload from both profile pages with title-bar sync, login history, password change, contact and email draft, FAQs, and the bundled operation manual with native chapter navigation.

VNC image/control remains intentionally excluded. The remote-monitoring menu opens the non-VNC device data page.

The common `boer-qt-ui` target in `../common` links the same native controls, dialogs, checkbox painting, date picker, and translation engine into both client and server. Business values are marked as original data so changing language does not translate user-entered names or remarks. UI feedback, row selection, checkbox behavior, and popups follow [开发注意事项.md](../开发注意事项.md).

The eight main sidebar icons use the same Element UI glyphs as the original Vue menu, rendered by Qt at device pixel ratio 2. Their font and license are included in the shared server assets; the original submenu layout intentionally has text-only child entries.

## Verification

```bash
ctest --test-dir new-architecture/client/build --output-on-failure
```

`--ui-self-test` visits every non-VNC page, checks English labels and permission-specific pattern sections, verifies the direct login language toggle, row-to-checkbox behavior, fixed action column and dropdown placement, and exercises multi-file drop plus per-file metadata without changing user-entered Chinese text. `--excel-self-test` generates a native XLSX fixture. `--api-self-test --server-host 127.0.0.1 --server-port 18095` signs in to a **disposable** Qt server, visits all non-VNC pages, and checks statistics export conversion, catalog CRUD, and pattern multipart upload, download, and deletion. The client was also compared visually with the current Vue pages using synthetic data. Target Windows, real devices, and every possible production data shape still need field validation; see the server README for its separate MySQL/MSSQL and device-protocol validation limits.
`--modal-self-test` checks application modality, the full-window overlay, and restoration after closing; the macOS Cocoa run additionally verifies that the owner window cannot be dragged or closed while the dialog remains interactive, without making the owner window click-through.
The client uses a shared Qt file chooser for imports, downloads, and exports. UI tests verify that save/open dialogs appear and nested import selection returns the chosen path; the API self-test also clicks the employee import/export, pattern download, and statistics Excel export buttons against the disposable server.

The top bar shows the application name followed by the running client version. Immediately before the language toggle, the two-line server block shows the connected server version and management address. Device TCP port details remain available in that block’s tooltip. These labels follow the selected application language.
