# Contributing to mideaapple

Thanks for helping out! This is a small firmware project — the glue between
[HomeSpan](https://github.com/HomeSpan/HomeSpan) (HomeKit) and
[dudanov/MideaUART](https://github.com/dudanov/MideaUART) (the AC protocol).

## Ways to help

- **Report whether your AC works** — open an *AC compatibility report* issue. Even a
  "works fully" is useful; it grows the known-working list in the README.
- **File bugs** with the serial log (`@115200`) attached.
- **Send PRs** for fixes and features.

## Building

See [Build & flash](README.md#build--flash) in the README. In short:

```sh
pio run -e esp32-c3-devkitm-1     # compile
pio run -t upload                 # flash over USB-C
pio device monitor                # serial log @115200
```

CI builds the `esp32-c3-devkitm-1` env on every push — keep it green.

## Ground rules

- **Match the surrounding style.** Terse, comment *why* not *what*, keep the diff
  minimal. The existing files are the style guide.
- **Pin your dependencies.** `platformio.ini` pins the platform, HomeSpan, and
  MideaUART to exact versions on purpose (reproducible builds + a stable CI badge).
  If you bump one, do it deliberately in its own commit and re-verify the build.
- **Test the path you touch.** A build passing is necessary but not sufficient —
  anything touching the UART/AC or WiFi paths should be tried on real hardware. Note
  in the PR what you actually exercised (and against which AC model, if relevant).
- **Don't commit build output.** `.pio/`, `dist/`, and `*.bin` are gitignored;
  release binaries are published by the `release` workflow, not checked in.

By contributing you agree your work is licensed under the repo's [MIT License](LICENSE).
