# Repository Guidelines

## Project Structure & Module Organization

Oma Spend is a C++17/Qt 6 desktop budgeting app built with qmake and Qt Quick. `src/backend.{h,cpp}` handles budgeting, persistence, and undo/redo; `src/main.cpp` provides application setup and CLI commands. Keep business logic in C++ and presentation in `src/Main.qml`. Theme and translation helpers live in `src/theme.*` and `src/translator.*`, and `src/i18n/en.json` supplies English plural forms only; visible strings live in the C++ and QML sources as `tr()`/`qsTr()`. `src/resources.qrc` embeds resources. Tests live in `tests/`; launcher assets are in `data/`, and the README screenshot is in `docs/`. Generated files belong in ignored `build/`.

## Build, Test, and Development Commands

Install Qt 6 development tools, including `qmake6`, Qt Quick Controls, Qt Test, and `make`.

- `bin/build`: compile the app to `build/oma_spend`.
- `bin/test`: build and run Qt Test with `QT_QPA_PLATFORM=offscreen`.
- `OMA_SPEND_DATA_FILE=/tmp/oma-spend-dev.json build/oma_spend`: launch the development binary against temporary data. To check how the window looks, render it with `QT_QPA_PLATFORM=offscreen` and `grabWindow()` instead of opening it on the desktop.
- `OMA_SPEND_DATA_FILE=/tmp/oma-spend-dev.json build/oma_spend status --json`: inspect isolated development data.
- `bin/install`: build, test, then install the binary and launcher assets under `~/.local/`. Existing project instructions require this after app changes.
- `bin/uninstall`: remove the installed app while retaining records.

## Coding Style & Naming Conventions

Match existing four-space indentation. Use PascalCase class names, camelCase methods and properties, and `m_` prefixes for private members. Follow surrounding brace placement. No formatter or linter is configured. Wrap visible strings in `tr()` or `qsTr()`. New actions need keyboard shortcuts documented in the help overlay and README.

Store money as `qint64` cents with no floating point, from parsing to the daily allowance, and format through `Backend::money`. Keep each amount and each month's totals within `Backend::maxCents` (2^53 − 1). Preserve daily allowance calculations and independent monthly records. Save atomically with `QSaveFile`; validate the whole file on load (version, months, entries) and refuse writes when it fails.

Several processes may write the same file. Route every change through `beginWrite`, which takes `oma_spend.json.lock` and reloads newer data, then adopt the new state only after `save()` succeeds so failed saves leave no trace.

## Testing Guidelines

Add descriptive camelCase test slots to `tests/oma_spend_tests.cpp`. Use Qt Test assertions, `QTemporaryDir`, and pinned dates for deterministic behavior. Run `bin/test` after backend changes; cover budgeting boundaries, amount limits, persistence failures, malformed files, concurrent instances, and undo/redo where relevant. No numeric coverage threshold is configured.

## Commit & Pull Request Guidelines

Follow `CLAUDE.md`: short, imperative subject lines (for example, "Lock writes, roll back failed saves, and validate data files"), one logical change per commit, and ask before pushing. PRs should describe behavior changes, list validation commands, link relevant issues, and include screenshots for UI changes. Use temporary data for manual checks; never modify personal spending records.
