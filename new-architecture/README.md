# New architecture

The native Qt/C++ [server](server/README.md) and [client](client/README.md) live in separate directories. Their business code is independent; both link the [common Qt UI target](common/CMakeLists.txt).

Start either application from the project root:

```bash
./new-architecture/start-server.command
./new-architecture/start-client.command
```

The server wrapper uses the formal Qt database. The client wrapper builds and opens the native login screen. Development notes are in [开发注意事项.md](开发注意事项.md). VNC remains outside this stage by request.

Pattern uploads also support a temporary direct-to-machine mode in Qt 2.1.10. See [花型临时直传说明.md](花型临时直传说明.md) for usage, server requirements, limits and verification.
