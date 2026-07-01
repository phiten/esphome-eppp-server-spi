import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

#CODEOWNERS = ["@your-github-handle"]
#DEPENDENCIES = ["wifi"]

eppp_server_ns = cg.esphome_ns.namespace("eppp_server")
EPPPServerComponent = eppp_server_ns.class_("EPPPServerComponent", cg.Component)

CONF_MOSI_PIN = "mosi_pin"
CONF_MISO_PIN = "miso_pin"
CONF_SCLK_PIN = "sclk_pin"
CONF_CS_PIN = "cs_pin"
CONF_HANDSHAKE_PIN = "handshake_pin"


def _require_esp_idf(config):
    # SPI-slave EPPP transport is only available in the esp-idf framework.
    # NOTE: CORE.using_esp_idf was removed in ESPHome 2026.6.0 in favor of
    # CORE.is_esp32 (since Arduino-on-ESP32 is itself built on ESP-IDF now).
    # "Pure ESP-IDF, no Arduino layer" is expressed as below.
    if not CORE.is_esp32 or CORE.using_arduino:
        raise cv.Invalid("eppp_server requires 'esp32: framework: type: esp-idf' (Arduino not supported)")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(EPPPServerComponent),
            cv.Required(CONF_MOSI_PIN): cv.int_range(min=0, max=39),
            cv.Required(CONF_MISO_PIN): cv.int_range(min=0, max=39),
            cv.Required(CONF_SCLK_PIN): cv.int_range(min=0, max=39),
            cv.Required(CONF_CS_PIN): cv.int_range(min=0, max=39),
            cv.Required(CONF_HANDSHAKE_PIN): cv.int_range(min=0, max=39),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _require_esp_idf,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_mosi_pin(config[CONF_MOSI_PIN]))
    cg.add(var.set_miso_pin(config[CONF_MISO_PIN]))
    cg.add(var.set_sclk_pin(config[CONF_SCLK_PIN]))
    cg.add(var.set_cs_pin(config[CONF_CS_PIN]))
    cg.add(var.set_handshake_pin(config[CONF_HANDSHAKE_PIN]))

    # Required lwIP options for PPP-over-SPI + NAT gatewaying.
    # (Kept here so users don't have to remember to set these by hand.)
    cg.add_define("USE_EPPP_SERVER")
    if CORE.is_esp32 and not CORE.using_arduino:
        from esphome.components.esp32 import add_idf_component, add_idf_sdkconfig_option

        # Confirmed signature (2026.6.x):
        #   add_idf_component(*, name: str, repo: str | None = None,
        #                      ref: str | None = None, path: str | None = None)
        # No `repo` -> resolved against the ESP Component Registry by `name`.
        # `ref` here pins the registry version constraint.
        add_idf_component(name="espressif/eppp_link", ref="1.1.5")

        add_idf_sdkconfig_option("CONFIG_LWIP_IP_FORWARD", True)
        add_idf_sdkconfig_option("CONFIG_LWIP_IPV4_NAPT", True)
        # NOTE: this is a Kconfig *choice* among UART/SPI/SDIO/ETH transports.
        # eppp_init() resolves its transport via a compile-time macro gated on
        # this exact symbol name -- getting this wrong silently falls back to
        # UART regardless of what eppp_config_t.transport is set to at runtime.
        add_idf_sdkconfig_option("CONFIG_EPPP_LINK_DEVICE_SPI", True)
