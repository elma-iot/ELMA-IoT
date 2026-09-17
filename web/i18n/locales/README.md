# ELMA IoT language modules

Each language is kept in its own JavaScript module. To add a language:

1. Copy `en.js` to `<language-code>.js`.
2. Set `code`, `nativeName`, `rtl`, and translate every value in `messages` while preserving each English key.
3. Import the module and add it to `locales` in `index.js`.
4. Add the same language code and native label to Android `UiPreferences.java`.
5. Run `npm --prefix shared-web run build:android`, `npm --prefix shared-web run build:firmware-i18n`, and the localization coverage test.

Each firmware image contains English plus the language selected in the Android app. The firmware generator assigns the locale a compile-time ID, so only that second language is linked into the ESP image. When adding a locale, also add its code to `scripts/embed-firmware-i18n.mjs`, `LocalFirmwareCompiler.languageId`, and `APP_COMPILED_LANGUAGE_CODE` in `settings_schema.h`.

Keep board names, GPIO identifiers, URLs, and diagnostic/compiler output exact when translating them would reduce technical accuracy.
