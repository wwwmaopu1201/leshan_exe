# Qt/C++ server prototype

This directory is the server part of `new-architecture`. It is a separate implementation of the Boer LAN server and does not modify or launch the existing Go/Tauri server. VNC is intentionally outside this implementation. A client directory can be added beside `server/` later.

For the future native Qt client, see [开发注意事项.md](../开发注意事项.md) for API contracts, reusable controls, UI lessons, and known server gaps.

## Build and run

On macOS, double-click `start-server.command` to build if necessary and start the formal Qt database. From the project root, the equivalent one-command launch is:

```bash
./new-architecture/start-server.command
```

Close any other server using HTTP port 8088 or device TCP port 38400 before starting it.

Requires CMake 3.21+, a C++17 compiler, and Qt 6.4+ with Core, Network, Sql, Widgets, and QSQLITE. Selecting MySQL as the primary database additionally requires QMYSQL and its MySQL client library.

On Windows, use `build-windows.ps1 -QtRoot C:\Qt\<version>\<compiler> -MySqlClientDll C:\path\to\libmysql.dll` from a Qt build environment. The script builds `bohr-qt-server.exe`, runs `windeployqt`, and requires QSQLITE, QMYSQL, and QODBC plugins. Qt documents how to [build QMYSQL for the exact Qt version](https://doc.qt.io/qt-6/sql-driver.html) and [deploy Windows dependencies](https://doc.qt.io/qt-6/windows-deployment.html).

```bash
cmake -S new-architecture/server -B new-architecture/server/build -DCMAKE_BUILD_TYPE=Release
cmake --build new-architecture/server/build -j
./new-architecture/server/build/bohr-qt-server.app/Contents/MacOS/bohr-qt-server
```

Run without the desktop window:

```bash
./new-architecture/server/build/bohr-qt-server.app/Contents/MacOS/bohr-qt-server --headless --port 8088 --tcp-port 38400 --db ./new-architecture/server/data/boer-lan-qt.db
```

On first launch, the local administrator account is `admin` / `admin123`. Change its password immediately in a deployment. The default primary database is a separate SQLite file in the Qt application data directory. A **new, empty MySQL database** can instead be selected with `DB_TYPE=mysql` and the `DB_HOST`, `DB_PORT`, `DB_USER`, `DB_PASSWORD`, `DB_NAME`, and `DB_CHARSET` environment variables, or a simple YAML file based on [config.example.yaml](config.example.yaml) selected with `CONFIG_PATH` or `--config`. Uploaded files and trial state remain in the local data directory (`BOERLAN_DATA_DIR`, `DATA_DIR`, or `--data-dir`). Old Go data and bcrypt password hashes are not migrated. New passwords use PBKDF2-SHA256. Existing Go and Qt processes must not listen on the same ports at once.

The HTTP port comes from `--port`, then the `server_port` database setting, then `server.port` in YAML, then 8088. The server writes the active port to `backend-port.txt` beside its local data, or to `PORT_FILE` when set. JWT secret and lifetime can be set with `jwt.secret`/`jwt.expire` or `JWT_SECRET`/`JWT_EXPIRE`; when no secret is provided, the Qt database's own persisted key is used. The sample YAML contains only flat scalar keys under `server`, `database`, and `jwt`; advanced YAML constructs are not needed.

Normal launches also maintain `server-trial-state.json` beside the Qt database for machine binding and clock rollback detection. Set `APP_ENV=development` or `BOERLAN_SKIP_TRIAL=1` for development runs.
On Windows, the state reader is closed before atomically saving the next launch state. Leaving it open prevents replacement of an existing state file and makes the second launch exit before showing a window. Rebuild and replace the executable to apply this fix; keep the existing database and trial state. Trial-state startup failures now show a desktop error dialog, and write failures include the path and operating-system error. `--trial-self-test` checks repeated state updates and must pass on the target Windows build.
On macOS, the binding uses Qt's stable machine ID; prior hostname-based state is migrated only when its saved fingerprint matches this Mac's current local host identity. Migration preserves the original first-seen time and launch count. Running the one-click script while the Qt server is already listening on device port 38400 now reports that it is already running. Startup failures print the latest server log lines.

## Implemented so far

| Area | Current Qt/C++ implementation |
| --- | --- |
| Desktop console | Native Qt Widgets throughout: Chinese/English switching, account/device trees, device drag-and-drop between groups, responsive filters, styled dialogs, calendars, button feedback, and nine accessible pages. The pattern-file page supports uploads, edits, device download/upload queues, and device-file operations; the pattern-statistics page has both legacy tables. Home shows persisted, filterable debug-log cards. Network, firewall, and external database tools use native Qt forms. |
| Persistence | Standalone SQLite or new MySQL primary schema for the main existing entities, soft deletion, runtime sessions, production/salary fields, default root group, roles, and administrator; separate machine-bound trial state. MySQL requires QMYSQL and still needs validation in a target environment. |
| Authentication | Login, JWT-compatible Bearer token signing, active-account check, account info, logout response, password/profile update, avatar upload/read, and filtered login logs. |
| HTTP API | Health, group/role/account/operator/device/employee CRUD, employee/operator JSON import and export, group tree and sorting, move-to-group, catalog summaries, streamed pattern upload/download/ZIP/batch update and transfer queues, system diagnostics/config/firewall/external database endpoints, home/dashboard/production/salary/alarm/process/duration/device-pattern statistics, and four CSV exports. Multi-device statistics filtering and missing production-time derivation are included. Responses use the existing `{code,data,message}` envelope. Non-pattern request bodies retain a 256 MiB limit. |
| Device TCP | Port 38400 by default; DT frame and CRC validation, registration/heartbeat/time sync, device identity and runtime sessions, work start and current-user push/result, realtime status, ten-second polling, five-second reconnect grace, sewing/pattern/count/speed/high-low fields, trim/idle/oil messages, alarm records with the original 165 descriptions, old/new production reports with duplicate protection and salary creation, and bidirectional pattern transfer with acknowledgments, queue workers, list refresh, and device-side deletion. |
| External database | Qt SQL implementation for connection configuration/testing and manual/scheduled transfer of seven entity tables to MySQL or MSSQL. Requires the QMYSQL or QODBC Qt plugin plus the underlying client driver. |
| Access | Administrator restrictions for server administration and detailed parent/child role permissions for business endpoints. Account groups organize records; they do not restrict business-data visibility, matching the current Go service. |

## Not yet equivalent to the existing server

This is an initial implementation, **not a 1:1 replacement**. The following existing behaviors are still missing or differ:

- Real-device transfer recovery and uncommon identity/control edge cases still need on-site verification. Synthetic DT tests cover bidirectional transfers, conflict handling, final-name rejection, current-user push, Chinese device filenames, and identity rebinding.
- Exact parity of all statistics edge cases remains unverified on a representative production data set. The current code handles historical employee attribution, alarm-window overlap, runtime sessions, line hierarchy, live pattern prices, salary fallback, multi-device filters, and missing production-time derivation on disposable fixtures.
- Primary MySQL, external MySQL/MSSQL synchronization, and Windows firewall repair still need on-site testing. This macOS Qt installation currently has only the SQLite SQL plugin. Bundle QMYSQL/QODBC and their client libraries on target systems.
- Historical data migration and old bcrypt password compatibility are excluded for now at the user's request. The Qt database remains separate.
- Visual pixel-level comparison with the existing Vue desktop interface has not been completed; the Qt UI self-test checks layout, controls, language switching, dialogs, and pattern pages.

Do not direct production clients to this version yet. The server does not return success for unsupported operations; it responds with an error.

## Tests

```bash
ctest --test-dir new-architecture/server/build --output-on-failure
# Start a disposable --headless server on ports 18088 and 18400 first.
# Give each script its own fresh temporary database/server process:
python3 new-architecture/server/tests/api_smoke.py 18088
python3 new-architecture/server/tests/tcp_smoke.py 18088 18400
python3 new-architecture/server/tests/device_protocol_smoke.py 18088 18400
```

The API smoke test covers authentication, multipart and chunked multipart uploads, chunked JSON requests, ZIP export, system tools, external database configuration, statistics, imports, detailed permissions, organization-wide business visibility, and CRUD. The TCP tests send valid DT frames and check registration, heartbeat, realtime polling, reconnection grace, current-user push, alarms, both production formats, salary creation, multi-device statistics filtering, time derivation, bidirectional transfer, list refresh, Chinese names, and identity rebinding. The UI self-test checks both native pattern pages, upload/download dialogs, pagination, log filtering, and English labels. A separate disposable test uploaded a 257 MiB file successfully without loading its request body into memory.

The icon font is copied from the existing Element UI dependency for visual consistency. Its MIT license is included in `assets/ELEMENT_UI_LICENSE`. The application interface itself is implemented with Qt Widgets and C++.
