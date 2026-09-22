# Plugins

The Plugins page separates the active processing chain from the installed plugin database.

## Running

**Running** contains plugin instances in processing order. Each card shows the plugin name, manufacturer, format, a status badge, and an action menu. The toolbar combines search, output mute, chain bypass, and sorting.

Available actions include:

- open the native or generic plugin editor;
- bypass or enable processing;
- duplicate the instance;
- rename the instance or restore its original name;
- inspect plugin identity and input/output buses;
- swap positions with another instance;
- remove the instance from the chain;
- reorder the chain by drag and drop or the available move actions.

Duplicating a plugin creates another independent running instance. Reordering changes signal flow immediately because audio is processed from the first card to the last.

Bypass keeps the slot in the chain and the processor running while selecting latency-compensated dry audio. Individual bypass, global chain bypass, and output mute use short transitions. Global mute and bypass are runtime controls that reset when the host restarts.

## Installed

**Installed** is the known-plugin database produced by scanning. Its toolbar combines search, **Scan for plugins**, and sorting. **Group by manufacturer**, at the top of the Sort menu, displays manufacturer headings connected to their plugin cards. Status badges distinguish available, running, bypassed, and unavailable entries.

Each installed entry can be:

- added to the running chain;
- opened in File Explorer;
- renamed, restored to its original name, or inspected in Plugin details;
- removed from the database.

These actions are in each card's **…** menu. A custom installed name is saved and used when adding a new running instance; running instances can then be renamed independently. Restore original name is disabled when no custom name is in use.

Removing an installed entry that is currently running also removes its running instances after confirmation. **Settings > Plugin database > Remove missing** deletes entries whose plugin files no longer exist. **Clear database** clears the database and running chain after confirmation.

## Scan paths

**Scan for plugins** opens one dialog for folders and scanning. Under **Add new path**, type a full folder path or use the folder picker, then save it with the save icon. Saved paths appear below in individual cards with open-folder and delete actions. The editor and saved paths scroll together, with their headings outside the cards.

Default Windows locations include common system and per-user VST3 folders and conventional VST2 folders under Program Files. Changes are saved automatically in WinUI preferences and sent to the host when scanning starts. Duplicate and invalid paths show inline feedback.

## Scanning and quarantine

**Start scan** opens a small progress dialog with cancellation. Completion shows results and offers **Retry failed files**, **View failures**, and **Close**. Failure cards separate the path, readable error, format, and attempt count; selected entries can be retried without pagination buttons.

Scanning uses a separate `LightHostModernScanner.exe` worker for each module, with a timeout and cleanup when cancelled or when its owner exits. Unchanged plugin descriptions are cached, and interrupted scans preserve completed results. This isolation applies to discovery; active effects still run inside the audio host. VST2 scanning occurs only when support was compiled into the host and **Enable VST2 plugins** is enabled.

Plugins that fail to load can be quarantined so one broken binary does not repeatedly crash startup or chain restoration. Use `--clear-failed-plugins` to clear that quarantine, or `--safe-mode` to start without restoring the saved chain.

## State persistence

The host saves the installed database, aliases, running order, per-instance names and bypass state, individual processor state, and plugin editor positions. Persistent instance IDs keep duplicate plugins distinct. Versioned session files use atomic writes, backups, and migration of legacy keys to preserve state across reorder, restart, and recovery.

See [Persistence and recovery](persistence-and-recovery.md) for recovery commands and storage behavior.
