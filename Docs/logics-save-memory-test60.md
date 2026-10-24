# Live graph save memory fix (test.60)

On the ESP32-3248S035C, saving a Wi-Fi / two LED Color / RGB Mixer graph
could exhaust the heap in `LogicDevice::persist`. Serial capture on test.58
showed a failed 1024-byte allocation with only 2320 bytes free and a largest
block of 660 bytes, followed by a C++ string allocation abort.

Validation now retains executable port contracts without copying editor help
or the device catalog into each graph. Device bindings remain authoritative;
the API still supplies the editor catalog separately. Already compact graphs
skip the catalog-removal copy. Persistence writes the compact record in place,
scans only prior revisions, verifies file bytes by streaming CRC32, and streams
MessagePack into one 1000-byte chunk buffer instead of allocating the complete
encoded record. Unwired optional input contracts are omitted on the Sunton
3248 target; the editor restores them from its catalog.
Alternating file/NVS slots continue to preserve the previous valid record.

Regression checks:

- `logic_save_memory_host.cpp`: the mixed LED graph validates within a 32 KiB
  host allocator budget despite large editor-only metadata; allocation denial
  returns a validation failure.
- `logic_storage_host.cpp`: filesystem and NVS round trips, latest-revision
  selection, chunked records, and failed-write retention.
- `logic_led_mix_host.cpp` and `logic_led_controls_host.cpp`: Boolean gates,
  color mixing, independent effects and unchanged-value behavior.
