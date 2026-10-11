# Foreground SD file operations

Deleting files renews a three-second foreground priority window. Directory indexing cooperatively closes its readers and incomplete temporary index, waits for a quiet period, and restarts its scan. The SD capacity refresh is deferred during this window. A successful deletion invalidates the affected cached directory index immediately; live directory enumeration supplies the updated list without starting another index writer for every file in a batch.

The browser automatically retries a busy deletion every 300 ms for up to two minutes and displays a waiting message. Batch deletion captures the original target and folder, advances through the entire selection and refreshes the listing after each success. Single-file and microphone recording deletion use the same request handling.

SD music playback fades out and pauses on the audio task while foreground priority is active, releasing its storage read lease. After the quiet period it reacquires the lease, resumes playback and fades in. Communication/capture keeps its own independent hold reason.

An upload, download, checksum verification or recording that already owns the SD card is allowed to finish safely. The firmware does not interrupt a write or bypass a storage lease to remove a file. If one of these operations keeps the card occupied for two minutes, the browser explains that it must finish before deletion can proceed. Removing the SD card still reports it unavailable.

This prioritization preempts directory indexing, pauses SD music playback and defers capacity refresh; other owners retain their leases until completion. HTTP and HTTPS use the same storage handlers.
