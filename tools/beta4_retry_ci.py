from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    if text.count(old) != 1:
        raise RuntimeError(f"{path}: expected one match for {old!r}, got {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


qa = ".github/workflows/qa-beta1-bench-fix.yml"
replace_once(qa, "name: NearBy One NEXT beta.3 QA CI", "name: NearBy One NEXT beta.4 QA CI")
replace_once(qa,
'''    branches:
      - qa/beta-0.1-bench-bugs''',
'''    branches:
      - qa/beta-0.1-bench-bugs
      - qa/beta4-retry-2''')
replace_once(qa,
'''      - name: Guard beta.3 QA fixes
        run: python tests/integration/test_beta3_qa_regressions.py''',
'''      - name: Guard beta.3 QA fixes
        run: python tests/integration/test_beta3_qa_regressions.py
      - name: Guard beta.4 QA fixes
        run: python tests/integration/test_beta4_qa_regressions.py''')
replace_once(qa, "NearBy-One-NEXT-ESP32C6-beta3-qa.bin", "NearBy-One-NEXT-ESP32C6-beta4-qa.bin")
replace_once(qa, "nearby-one-next-beta3-qa-${{ github.sha }}", "nearby-one-next-beta4-qa-${{ github.sha }}")

Path(".github/workflows/beta-release.yml").write_text(r'''name: NearBy One NEXT beta.4 release

on:
  workflow_dispatch:

permissions:
  contents: write

jobs:
  beta-release:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Re-run beta.4 regression guards
        run: |
          python tests/integration/test_beta1_bench_regressions.py
          python tests/integration/test_beta3_qa_regressions.py
          python tests/integration/test_beta4_qa_regressions.py

      - name: Build ESP-IDF v6.1 firmware and two-file package
        uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v6.1
          target: esp32c6
          path: firmware
          command: >-
            idf.py build &&
            grep -q '^CONFIG_LV_TXT_ENC_UTF8=y$' sdkconfig &&
            grep -q "lv_font_montserrat_14" build/nearby_one_next.map &&
            mkdir -p ../release &&
            cd build &&
            esptool --chip esp32c6 merge-bin -o ../../release/NearBy-One-NEXT-ESP32C6.bin @flash_args &&
            cd .. &&
            cp ../artifacts/pinned/nearby.nbdb ../release/NearBy-One-NEXT.nbdb

      - name: Publish v0.1.0-beta.4 prerelease
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release create v0.1.0-beta.4 \
            release/NearBy-One-NEXT-ESP32C6.bin \
            release/NearBy-One-NEXT.nbdb \
            --target "$GITHUB_SHA" \
            --title "NearBy One NEXT v0.1.0-beta.4" \
            --prerelease \
            --notes-file - <<'EOF'
          Fourth public beta for Waveshare ESP32-C6-Touch-LCD-1.9.

          QA fixes in this beta:
          - Product LVGL static text no longer relies on nonessential glyphs missing from the current font. `Starting Web Management...` and other decorative separators use safe ASCII while the supported LVGL symbol font path remains unchanged.
          - `.nbdb` whole-SD preparation is no longer performed synchronously inside the HTTP request. A dedicated worker exposes unmount/raw-open/head erase/tail erase/raw-close/remount+FAT stages, propagates the real ESP-IDF error, and has a 60-second failure deadline so Web Management does not remain indefinitely at Formatting. Upload still starts only after format reports ready; firmware validation of `.part` still occurs before promotion.
          - Stop Portal now shuts down HTTP + SoftAP only. A connected or connecting STA remains alive with its IPv4 visible in Settings; the shared-RF competing-operation lease stays held until STA disconnects/fails, preserving Scan exclusivity. Wi-Fi configuration remains RAM-only and is never persisted.
          - Beta SoftAP is open/no-password. Product UI shows only SSID and `192.168.4.1`.

          Regression scope retained:
          - HA blue rendering.
          - Touch and LVGL top-bar symbols.
          - Scan RF lifecycle/exclusivity.
          - Portal start/stop and STA connect/status.
          - `.nbdb` client preflight, upload, server release validation and promotion ordering.

          Release files:
          - `NearBy-One-NEXT-ESP32C6.bin` - merged ESP32-C6 image; flash from address `0x0`.
          - `NearBy-One-NEXT.nbdb` - recognition database; install through Web Management -> Database.

          Verification status:
          - Host/integration regression tests and ESP-IDF v6.1 `esp32c6` build are required to pass before this workflow is dispatched.
          - Physical-board acceptance is `RETEST_REQUIRED` for this prerelease.
          - This prerelease does NOT claim physical-device PASS until the Waveshare board is retested.
          EOF
''')
