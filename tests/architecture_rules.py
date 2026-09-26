"""Authoritative application ownership, dependency and purity policy."""

# Application-local dependency rules. Add another entry when a new application
# needs the same architectural enforcement; keep the checker itself generic.
APP_RULES = {
    "rtty": {
        "enforced_roots": {"src", "main"},
        "module_paths": {"core": ("src",), "main": ("main",)},
        "include_roots": ("src", "main"),
        "allowed": {"core": {"core"}, "main": {"main", "core"}},
        "native_exceptions": {},
        "api_modules": {"main"},
        "no_heap_modules": {"core"},
    },
    "js8chat": {
        "enforced_roots": {"src", "tools", "main"},
        "module_paths": {"js8_engine": ("src/js8_engine",), "tools": ("tools",),
                         "activity_json": ("src/activity_json",), "live_rx": ("src/live_rx",), "main": ("main",)},
        "include_roots": ("src/js8_engine", "src/js8_engine/vendor/kissfft", "src/activity_json", "src/live_rx"),
        "allowed": {"js8_engine": {"js8_engine"}, "activity_json": {"activity_json", "js8_engine"},
                    "tools": {"tools", "js8_engine", "activity_json"},
                    "live_rx": {"live_rx", "js8_engine", "activity_json"},
                    "main": {"main", "live_rx", "js8_engine", "activity_json"}},
        "native_exceptions": {
            "tools/js8_decode.c": {"fopen"},
            "tools/js8_activity_log.c": {"fopen"},  # T063 host-only append logger.
        },
        "api_modules": {"live_rx", "main"},
        "no_heap_modules": {"js8_engine", "activity_json"},
    },
    "ft8": {
        "enforced_roots": {"main", "include", "src"},
        "module_paths": {
            "main": ("main",),
            "shared": ("include",),
            "app_controller": ("src/app_controller",),
            "auto_seq": ("src/auto_seq",),
            "config_service": ("src/config_service",),
            "presentation_profile": ("src/presentation_profile",),
            "rx_audio_adapter": ("src/rx_audio_adapter",),
            "rx_frontend": ("src/rx_frontend",),
            "rx_result_builder": ("src/rx_result_builder",),
            "rx_slot_framer": ("src/rx_slot_framer",),
            "storage_service": ("src/storage_service",),
            "log_service": ("src/log_service",),
            "radio_control": ("src/radio_control",),
            "tx_lifecycle": ("src/tx_lifecycle",),
            "tx_encoder": ("src/tx_encoder",),
            "tx_offset": ("src/tx_offset",),
            "ui_shell": ("src/ui_shell",),
            "ft8_engine": ("src/ft8_engine",),
        },
        "private_headers": {
            "src/app_controller/app_controller_internal.h": "app_controller",
            "src/app_controller/app_tx_schedule.h": "app_controller",
            "src/tx_encoder/tx_channel.h": "tx_encoder",
        },
        "forbidden_source_patterns": {
            "src/tx_offset/tx_offset.c": (
                (r"\b(?:rand|srand|random|srandom|getrandom|esp_random)\s*\(",
                 "TX offset must use application-owned PRNG state"),
            ),
            "main/ft8_main.c": (
                (r"\bui\s*\.\s*(?:screen|submenu)\b",
                 "ft8_main must not inspect UiShell screen/submenu state"),
                (r"\b(?:SCREEN_|UI_SUBMENU_)",
                 "ft8_main must not encode UIScreen/submenu policy"),
                (r"\bmodel\s*\.\s*[A-Za-z_]",
                 "ft8_main must not inspect individual UiModel fields"),
            ),
        },
        "allowed": {
            "main": {"main", "shared", "app_controller", "presentation_profile", "ui_shell"},
            "shared": {"shared"},
            "app_controller": {
                "app_controller", "shared", "auto_seq", "config_service",
                "presentation_profile", "rx_audio_adapter", "rx_frontend",
                "rx_result_builder", "rx_slot_framer", "storage_service", "log_service",
                "tx_lifecycle", "ui_shell", "ft8_engine", "radio_control", "tx_encoder", "tx_offset",
            },
            "auto_seq": {"auto_seq"},
            "config_service": {"config_service"},
            "presentation_profile": {"presentation_profile"},
            "rx_audio_adapter": {"rx_audio_adapter"},
            "rx_frontend": {"rx_frontend"},
            "rx_result_builder": {"rx_result_builder", "ft8_engine"},
            "rx_slot_framer": {"rx_slot_framer"},
            "storage_service": {"storage_service"},
            "log_service": {"log_service", "config_service"},
            "radio_control": {"radio_control"},
            "tx_lifecycle": {"tx_lifecycle"},
            "tx_offset": {"tx_offset", "config_service", "auto_seq"},
            "tx_encoder": {"tx_encoder", "auto_seq", "ft8_engine"},
            "ui_shell": {"ui_shell", "shared", "presentation_profile"},
            "ft8_engine": {"ft8_engine"},
        },
    },
}


# Shared standard-message CQ grammar, extracted from the codec for T082.
# Keep this edge limited to the pure token helper rather than UI shared types.
APP_RULES["ft8"]["module_paths"]["cq_token"] = ("include/ft8/cq_token.h",)
APP_RULES["ft8"]["allowed"]["cq_token"] = {"cq_token"}
for module in ("config_service", "auto_seq", "ft8_engine"):
    APP_RULES["ft8"]["allowed"][module].add("cq_token")

APP_RULES["ft8"]["module_paths"]["tools"] = ("tools",)
APP_RULES["ft8"]["allowed"]["tools"] = {"tools", "ft8_engine"}
APP_RULES["ft8"]["api_modules"] = {
    "main", "app_controller", "storage_service", "log_service", "rx_audio_adapter", "radio_control",
}
# The standalone host decoder reads a host WAV; it is not a runtime app.
# Only its fopen call is exempt, not the tools directory or other platform rules.
APP_RULES["ft8"]["native_exceptions"] = {"tools/ft8_decode.c": {"fopen"}}
APP_RULES["ft8"]["no_heap_modules"] = {"auto_seq", "tx_encoder", "tx_offset"}
APP_RULES["minicw"] = {
    "enforced_roots": {"main", "src"},
    "module_paths": {name: (path,) for name, path in {
        "main": "main", "app_core": "src/app_core", "keyer_service": "src/keyer_service",
        "ui_service": "src/ui_service", "audio_service": "src/audio_service",
        "port": "src/port", "runtime": "src/runtime", "storage_service": "src/storage_service",
    }.items()},
    "allowed": {
        "main": {"main", "port"},
        "app_core": {"app_core", "keyer_service", "ui_service", "audio_service", "storage_service", "port", "runtime"},
        "storage_service": {"storage_service", "keyer_service", "port", "runtime"},
        "keyer_service": {"keyer_service", "audio_service", "port", "runtime"},
        "ui_service": {"ui_service", "keyer_service", "audio_service", "port", "runtime"},
        "audio_service": {"audio_service", "port", "runtime"},
        "port": {"port", "app_core"},
        "runtime": {"runtime"},
    },
    "private_headers": {"src/ui_service/ui_screen.h": "ui_service"},
    "forbidden_source_patterns": {},
    "api_modules": {"port"},
    "no_heap_modules": {"main", "app_core", "keyer_service", "ui_service", "audio_service", "storage_service", "port", "runtime"},
}
for rule in APP_RULES.values():
    rule["include_roots"] = tuple(
        prefix for prefixes in rule["module_paths"].values() for prefix in prefixes
    )
APP_RULES["ft8"]["include_roots"] += ("src/ft8_engine/vendor/kissfft",)
# Unmodified KissFFT's optional FIXED_POINT branch uses this type header.
# No other vendor header/call or application source is exempt.
APP_RULES["ft8"]["header_exceptions"] = {
    "src/ft8_engine/vendor/kissfft/kiss_fft.h": {"sys/types.h"},
}
