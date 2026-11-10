# ArenaMP launcher

[Русский](LAUNCHER_RU.md) · [Project](../README.md)

## First start

Extract a complete compiled package into a writable directory. Run `arenamp-launcher.exe` on Windows or `./arenamp-launcher` on Linux. Keep the package's updater, agents, libraries and resources together.

Select the folder containing your Morrowind data and the plugins used by the server. The build-setup dialog recognises a direct data folder or its `Data Files` child. It loads `build.ini` when present. For a new build, enter its name and content language, choose plugins, arrange their order and accept the generated manifest. BSA archives and recognised groundcover are included in setup.

Content language controls legacy text encoding: Russian uses win1251, Polish win1250, and the remaining selection win1252. It is separate from the language of server messages.

## Join a remote server

Open the Servers tab and refresh. Select an entry to inspect its build, mode, population and connection options. Requirements/files and website actions display information supplied by the operator. An invitation ID can locate a private entry supported by the master service.

Choose direct connection when the published game endpoint is reachable. Use the relay selection when the master service offers a tunnel; the bundled relay agent must be present. Enter the game password if requested, then press Play. For manual connections, enter the host and port in the connection controls.

The selected-server card refreshes status, population and uptime when those fields are supported by the server. A reachable status query does not establish content compatibility: the client still needs the correct plugin list and hashes.

## Run a local server

1. Select the intended game build and stop any running local server before changing it.
2. Set the server name, password, maximum players, port and bind interface.
3. Configure the available CO-OP/MMO policies and gameplay values in Server Settings.
4. Refresh the required plugin hashes from the selected build when its content changes.
5. Start the server. Review the embedded console and then join it with Play.

The launcher manages its server process, exposes automatic restart and displays session uptime. The console supports log encoding and log-view clearing. Server stop handling includes backup support; maintain independent verified backups of production data as well.

Server Settings provides grouped forms and a raw `config.lua` view. Synchronise the two views deliberately and save before restarting. Cell cleanup and full data reset are destructive maintenance actions with confirmation dialogs.

Enable Show in server list to register the running server. Supply an optional website/build URL. Registration requires a compatible master endpoint; direct reachability and relay availability depend on the network and service configuration.

## Chat and voice

Create a character on the game server first. On Chat, sign in with that account. Compatible servers provide channels, history, presence and mention notifications. Open Chat.log from the page when diagnosing a connection problem.

Select the recording device, refresh the device list after attaching a microphone, choose the voice key and enable transmission. Incoming voice and outgoing microphone state are separate. Local proximity and party rules are set by the server and in-game communication state.

## Settings and update management

Data Files selects content and order. Graphics and the additional settings pages configure display/rendering and other client options. The overview shows detected hardware and launcher/server state. Saved configuration remains effective between sessions.

Updates come from the chosen build's manifest, rather than from an arbitrary server-directory attachment. The native updater handles staging, progress and hash checks. The operator must configure update endpoints and the expected package format. Consult the [updater documentation](arena-updater/README_RU.md) and inspect Update.log for failures.

## Troubleshooting

| Symptom | Check |
|---|---|
| No servers listed | Master URL, service availability, HTTPS and network access |
| Selected server is full or unavailable | Refresh the entry and its live status |
| Relay does not start | Complete package, agent executable and tunnel service availability |
| Content mismatch | Selected build, plugin order, server requirements and hashes |
| Local server will not start | Console, bind port, writable data paths and Lua modules |
| Update cannot start | Native updater, writable installation, manifest and Update.log |
| Linux Qt error | Start through the portable wrapper with the supplied Qt libraries |

See the [server guide](guide/05-server.md) for persistent data and the [build guide](BUILDING.md) for producing the full application package.
