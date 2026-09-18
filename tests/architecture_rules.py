"""Authoritative application ownership, dependency and purity policy."""

# Application-local dependency rules. Add another entry when a new application
# needs the same architectural enforcement; keep the checker itself generic.
APP_RULES = {
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
            "tx_lifecycle": ("src/tx_lifecycle",),
            "ui_shell": ("src/ui_shell",),
            "ft8_engine": ("src/ft8_engine",),
        },
        "private_headers": {
            "src/app_controller/app_controller_internal.h": "app_controller",
        },
        "forbidden_source_patterns": {
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
                "tx_lifecycle", "ui_shell", "ft8_engine",
            },
            "auto_seq": {"auto_seq"},
            "config_service": {"config_service"},
            "presentation_profile": {"presentation_profile"},
            "rx_audio_adapter": {"rx_audio_adapter"},
            "rx_frontend": {"rx_frontend"},
            "rx_result_builder": {"rx_result_builder", "ft8_engine"},
            "rx_slot_framer": {"rx_slot_framer"},
            "storage_service": {"storage_service"},
            "log_service": {"log_service"},
            "tx_lifecycle": {"tx_lifecycle"},
            "ui_shell": {"ui_shell", "shared", "presentation_profile"},
            "ft8_engine": {"ft8_engine"},
        },
    },
    "keyer": {
        "enforced_roots": {"main", "include", "src"},
        "module_paths": {
            "main": ("main",),
            "shared": ("include",),
            "app_controller": ("src/app_controller",),
            "config_service": ("src/config_service",),
            "keyer_engine": ("src/keyer_engine",),
            "keyin": ("src/keyin",),
            "keyout": ("src/keyout",),
            "sidetone": ("src/sidetone",),
        },
        "private_headers": {},
        "forbidden_source_patterns": {},
        "allowed": {
            "main": {"main", "app_controller"},
            # keyer_types currently reuses the portable engine's paddle-mode
            # type. This is a type dependency only; orchestration remains in
            # app_controller.
            "shared": {"shared", "keyer_engine"},
            "app_controller": {
                "app_controller", "shared", "config_service",
                "keyer_engine", "keyin", "keyout", "sidetone",
            },
            "config_service": {"config_service", "shared"},
            "keyer_engine": {"keyer_engine"},
            "keyin": {"keyin", "shared"},
            "keyout": {"keyout", "shared"},
            "sidetone": {"sidetone"},
        },
    },
}


APP_RULES["ft8"]["module_paths"]["tools"] = ("tools",)
APP_RULES["ft8"]["allowed"]["tools"] = {"tools", "ft8_engine"}
APP_RULES["ft8"]["api_modules"] = {
    "main", "app_controller", "storage_service", "log_service", "rx_audio_adapter",
}
APP_RULES["keyer"]["api_modules"] = {
    "main", "app_controller", "config_service", "keyin", "keyout", "sidetone",
}
# The standalone host decoder reads a host WAV; it is not a runtime app.
# Only its fopen call is exempt, not the tools directory or other platform rules.
APP_RULES["ft8"]["native_exceptions"] = {"tools/ft8_decode.c": {"fopen"}}
APP_RULES["ft8"]["no_heap_modules"] = {"auto_seq"}
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
