# Durable session contract

Application version 1.4.0 uses IPC version 4. Session storage is separate from preferences and is scoped by the preferences file, including test profiles and separate instances.

## File and model

The preferences file's sibling `<preferences filename>.session.json` is an envelope with `formatVersion: 1`, a decimal-string `revision`, `intentionalEmpty`, `migrationId`, `contentHash` (SHA-256), and `sessionXml`. The XML uses the existing strict, ordered `LIGHTHOSTSESSION` version 1 adapter. UUID, original class identity and description, bypass, custom name, last valid state and recovery material remain independent. XML serialization uses revision zero; the envelope owns the storage revision. Timestamps and revision are excluded from the content digest and deduplication. No fuzzy matching of legacy plugin identities is permitted.

A stored empty collection must explicitly have `intentionalEmpty: true`. Suppressed loading (safe mode/startup preference), missing devices, missing plugins and invalid source data cannot authorize writing an empty replacement. Invalid/unsupported files retain their original bytes and make recovery visible. Files larger than 256 MiB are rejected for automatic loading/writing and retained for manual recovery.

## Migration and recovery

Load and validate main, backup and completed temporary files before considering legacy preferences. Select the valid candidate with the highest revision; ties prefer main. Preserve damaged originals before repairing the primary file. If session files exist but none validates, do not silently reimport stale legacy data or create an empty session.

When no session exists, back up the exact previous preferences before importing the version-1 adapter or legacy exact keys. A migration identity derived from the recovery copy makes generated legacy UUIDs deterministic. Once any valid session exists, it is authoritative; repeated migration never appends records. General preferences, device settings, language, appearance and original legacy recovery keys stay in their existing files.

## Capture and commit

The controller captures plugin state on the JUCE message thread behind processing suspension/drain. A failed capture retains that instance's previous valid state and reports an error. A copied model is immutable after submission. One storage worker serializes it, computes the digest, coalesces changes with a one-second debounce and skips writes for identical content. At most one pending snapshot and one in-flight snapshot are retained.

Before replacing a pending file, the worker copies the newest valid candidate to a flushed backup temporary and atomically renames it over the backup. This also protects recovery when a pending file is the only valid copy. The worker then writes the new session temporary in the same directory, flushes it to disk and atomically renames it over the primary. The primary is never truncated in place. Errors keep the requested revision pending and are exposed through session status. A new submission or explicit retry may retry the pending revision.

Normal shutdown captures and waits for the final write before processors are destroyed. An explicit `flush-session` operation provides the same durability barrier to the updater and returns a structured error on failure. Saved revisions advance only after a successful commit or verified content deduplication against an already successful commit. Recovery must be tested at each write/flush/backup/replace interruption point, including disk-full and replacement failures.
