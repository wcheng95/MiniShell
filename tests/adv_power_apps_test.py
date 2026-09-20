#!/usr/bin/env python3
"""Guard ADV internal batt/sleep app wiring and V2-derived hardware behavior."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
apps = (root / "platform/adv/adv_apps.c").read_text()
cmake = (root / "platform/adv/main/CMakeLists.txt").read_text()
power = (root / "platform/adv/adv_power.cpp").read_text()
batt = (root / "platform/adv/main/batt_static.c").read_text()
sleep = (root / "platform/adv/main/sleep_static.c").read_text()

assert '{"batt", minishell_app_batt_main}' in apps
assert '{"sleep", minishell_app_sleep_main}' in apps
assert '"batt_static.c"' in cmake
assert '"sleep_static.c"' in cmake
assert '"${CMAKE_CURRENT_LIST_DIR}/../adv_power.cpp"' in cmake
assert "esp_adc" in cmake

# MiniFT8-V2 Cardputer ADV battery measurement: ADC1 channel 9 / GPIO10,
# 2:1 divider, simple display-oriented Li-ion percentage mapping.
assert "ADC_CHANNEL_9" in power
assert "GPIO10" in power
assert "kBatteryDivider = 2.0f" in power
for mv, pct in ((4200, 100), (4100, 90), (4000, 80), (3900, 65),
                (3800, 50), (3700, 35), (3600, 20), (3500, 10),
                (3400, 5)):
    assert f"if (mv >= {mv}) return {pct};" in power

# MiniFT8-V2 sleep behavior: display off, short settle, GPIO0 active-low wake.
assert "M5.Display.sleep()" in power
assert "pdMS_TO_TICKS(100)" in power
assert "esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0)" in power
assert "esp_deep_sleep_start()" in power

assert '"battery %d%% %dmV\\n"' in batt
assert '"sleep: GPIO0 wakes ADV\\n"' in sleep

print("ADV batt/sleep internal apps: PASS")
