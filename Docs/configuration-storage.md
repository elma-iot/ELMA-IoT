# Configuration persistence

ESP32 configurations with diagrams and peripheral profiles can exhaust the 20 KiB
NVS partition. Existing partition sizes and OTA slots are unchanged. When internal
LittleFS is available, complete settings are now stored in two private records,
independent of the SD card. JSON allocations prefer PSRAM.

Each record has a schema, monotonic sequence, exact byte length and CRC32. Saves
stream to a temporary file, flush, read back and compare the checksum before
replacing the inactive slot. Boot selects the newest valid record and can fall
back to the other slot. Both records must be valid on initial migration before
large legacy diagram/menu/profile values are removed from the notifier namespace.
Wi-Fi credentials and all other legacy NVS settings are retained. Neither the NVS
partition nor media storage is erased. Devices without an internal filesystem
retain the previous NVS implementation.

Private records are excluded from file listings, uploads, downloads and checksums.
They contain configuration credentials and must not be published. Factory reset
removes both records. Normal OTA updates preserve the existing filesystem.
Downgrading to firmware without this storage implementation will read the legacy
NVS values, which can be older and no longer contain migrated editor data. Export
a configuration backup before downgrading.

A failed file write returns an error and leaves the active record intact. Check
internal flash free space and the device log. Mounting settings storage never
formats the filesystem. The usual storage startup retains its existing handling
for an uninitialized filesystem. CRC32 detects accidental corruption; it is not
encryption or an authenticity signature.
