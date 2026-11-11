# Oma Spend

A lightweight Omarchy desktop app that shows the current month's fund, what's been spent, and how much can be spent each day for the rest of the month. It's built like Omarchy's own apps: C++ and Qt Quick, compiled with qmake6 and make into one binary. It's installed for Kris's user, not as a pacman package.

## Layout

| Path | What it holds |
|---|---|
| `src/backend.{h,cpp}` | All the logic: funds, expenses, the daily allowance, undo and redo, loading and saving |
| `src/main.cpp` | App setup and the command line (`status`, `spend`, `fund`) |
| `src/Main.qml` | The window: layout, keyboard shortcuts, the `?` overlay. No logic here |
| `src/theme.{h,cpp}` | Follows the Omarchy accent color and Hyprland's window rounding, live |
| `src/translator.{h,cpp}` | Follows the Omarchy language. Only `src/i18n/en.json` ships, for plurals |
| `tests/oma_spend_tests.cpp` | Qt Test cases for the backend, theme and translator |
| `data/` | The launcher entry (`.desktop`) and icon |
| `docs/screenshot.png` | README screenshot, made-up data and the default accent |

## Build, test, install

- `bin/build` builds `build/oma_spend`.
- `bin/test` runs the tests offscreen. Run it after every backend change.
- `bin/install` builds, runs the tests, and installs to `~/.local/bin` and `~/.local/share`. It needs no sudo.
- **After every change to the app, run `bin/install` yourself**, so the launcher always has the latest version. If the tests fail, it stops before installing. Fix the failure and run it again.
- `bin/uninstall` removes the installed app and keeps Kris's records.
- Set `OMA_SPEND_DATA_FILE` to a temp file when you try the app or CLI, so Kris's real data isn't touched.

## Rules

- Amounts are whole cents (`qint64`), never floating point, from parsing to the daily allowance. Format them only through `Backend::money`, which shows dollars with a minus sign for negatives.
- `Backend::maxCents` (2^53 − 1, the most JSON stores exactly) caps each amount and each month's fund and spending totals. Parsing, saving and loading all enforce it, so every saved file loads again.
- Per day = (fund − expenses before today) ÷ days left, including today, rounded down. Today's spending lowers "Left today" and tomorrow's allowance, not today's allowance. The tests pin this, so keep them passing.
- Each month is stored under its `YYYY-MM` key in `~/.local/share/oma_spend/oma_spend.json` and starts empty. There's no carry-over.
- Save atomically (`QSaveFile`). Never overwrite a data file that fails to load: show the error in the window and refuse to save.
- Loading validates the whole file: `version` 1, `months` keyed `YYYY-MM`, and every entry's id, kind (`fund` or `expense`), cents, date in its month, and note. Anything else counts as failing to load.
- The window and CLI can run at once. Every change goes through `beginWrite`: it takes `oma_spend.json.lock` and reloads if another process saved, clearing the now stale undo history. Build the new state on the side and adopt it only when `save()` succeeds, so a failed save changes nothing.
- Logic goes in C++, layout in QML. Write every visible string as `qsTr()` or `tr()`.
- Keyboard first: every action needs a shortcut, listed in the `?` overlay and the README table.
- Keep it simple. Kris wants a lightweight app, so ask before adding features.
- To check how the window looks, render it offscreen with `QT_QPA_PLATFORM=offscreen` and `grabWindow()` rather than opening it on Kris's desktop.

## Git

- Commit each logical change locally with a short, imperative message.
- `origin` is Kris's public GitHub repo, https://github.com/ikpvk/oma_spend, on the `master` branch. Ask Kris before every push.
