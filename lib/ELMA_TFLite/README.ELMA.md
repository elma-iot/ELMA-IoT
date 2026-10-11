Vendored Arduino_TensorFlowLite_ESP32 from https://github.com/tanakamasayuki/Arduino_TensorFlowLite_ESP32 at 8572c7217b16bdc912bc38f46ff152e4a4eb11ef. Apache-2.0; see LICENSE and individual source notices.

Only the person detector operators and core runtime are compiled (library.json). DebugLog was renamed ElmaTfLiteDebugLog to avoid the firmware logger symbol. Uses the reference int8 kernels compatible with the existing Arduino/IDF toolchain; no IDF migration. Model and validation images are from the same revision. Runtime tensor arena is allocated in PSRAM on demand.
