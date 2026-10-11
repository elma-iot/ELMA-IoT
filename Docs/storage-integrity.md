# Storage transfer integrity

Browser multi-file uploads target the folder open when upload begins. Each file
is CRC32 checked before transfer and after SD readback. Upload requests send
X-File-CRC32 and X-File-Size; the firmware stages data in a temporary file, verifies
length and CRC before replacing the destination, and preserves existing files
when a transfer fails. Legacy clients without checksum headers remain supported.

Interrupted transfers retry from the beginning, up to three attempts. CRC32 detects
accidental corruption; it is not a cryptographic signature. HTTPS provides transport
security. No boot-time compression or whole-file ESP RAM allocation is used.

The Cancel button appears at the far right of the progress bar. Completed files
appear immediately and remain after cancellation. Progress and Cancel disappear
when the batch ends. Downloads verify file length and CRC before committing the
saved file. Android's native downloader streams through a bounded buffer; browsers
without the File System Access API use a browser Blob.

`/api/storage/checksum` starts/polls a bounded background CRC job; DELETE cancels
it. Only one checksum job runs at a time. Card/read failures are reported explicitly.
An unexpected power loss can leave a staging or backup file for manual recovery.

The built-in web interface remains accessible at `/fallback` when custom SD web
assets are enabled. `/fallback/` redirects to `/fallback` so relative assets work.
