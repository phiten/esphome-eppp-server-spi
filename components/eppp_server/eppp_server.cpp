#include "eppp_server.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"

#include "esp_err.h"

extern "C" {
#include "eppp_link.h"
}

namespace esphome {
namespace eppp_server {

static const char *const TAG = "eppp_server";

void EPPPServerComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up EPPP SPI server...");

  // ---------------------------------------------------------------------
  // TODO: verify against the ACTUAL vendored header before relying on this.
  // Open (after a build that has fetched the dependency):
  //   <build_dir>/managed_components/espressif__eppp_link/include/eppp_link.h
  // and confirm:
  //   - the exact name/shape of the config struct (eppp_config_t or similar)
  //   - how it distinguishes SPI vs UART vs SDIO transport config
  //   - the field names for MOSI/MISO/SCLK/CS and the handshake/interrupt pin
  //   - whether there's a default-config helper/macro to start from
  //
  // The block below is written to the *documented* eppp_init() contract
  // (endpoint role + config pointer -> esp_netif_t*), but the internal
  // struct field names are almost certainly going to need adjusting to
  // match your vendored version. Treat this as a compile-time TODO, not
  // as verified-working code.
  // ---------------------------------------------------------------------

  eppp_config_t config = {};  // TODO: replace with real default-config call if one exists

  // TODO: set transport = SPI and fill in the pins, e.g. something along the
  // lines of (field names are illustrative, NOT confirmed):
  //
  //   config.transport_config.spi.pin_mosi = this->mosi_pin_;
  //   config.transport_config.spi.pin_miso = this->miso_pin_;
  //   config.transport_config.spi.pin_sclk = this->sclk_pin_;
  //   config.transport_config.spi.pin_cs   = this->cs_pin_;
  //   config.transport_config.spi.pin_intr = this->handshake_pin_;  // handshake/data-ready
  //
  // and match whatever role/task/queue defaults the reference
  // esp32-spi-eppp-server firmware used, since you've already confirmed
  // those work on this hardware in Phase 0.

  this->eppp_netif_ = eppp_init(EPPP_SERVER, &config);
  if (this->eppp_netif_ == nullptr) {
    ESP_LOGE(TAG, "eppp_init() failed");
    this->mark_failed();
    return;
  }

  if (eppp_netif_start(this->eppp_netif_) != ESP_OK) {
    ESP_LOGE(TAG, "eppp_netif_start() failed");
    this->mark_failed();
    return;
  }

  ESP_LOGCONFIG(TAG, "EPPP SPI server started, waiting for peer + WiFi uplink");
}

void EPPPServerComponent::loop() {
  bool wifi_connected = wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected();

  if (wifi_connected && !this->napt_enabled_) {
    this->try_enable_napt_();
  } else if (!wifi_connected && this->napt_enabled_) {
    ESP_LOGW(TAG, "WiFi uplink lost -- NAT bridge is stale until WiFi reconnects");
    this->napt_enabled_ = false;
    // NOTE: intentionally not calling esp_netif_napt_disable() here yet --
    // confirm experimentally in your Phase 3 testing whether re-enabling
    // NAPT on reconnect works without an explicit disable first, or
    // whether that call needs to happen on disconnect too.
  }

  this->wifi_was_connected_ = wifi_connected;
}

void EPPPServerComponent::try_enable_napt_() {
  esp_netif_t *wifi_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (wifi_netif == nullptr) {
    // WiFi netif not created yet -- will retry next loop() iteration.
    return;
  }

  esp_err_t err = esp_netif_napt_enable(wifi_netif);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_netif_napt_enable() failed: %s", esp_err_to_name(err));
    return;
  }

  this->napt_enabled_ = true;
  ESP_LOGI(TAG, "NAT bridge enabled: EPPP peer -> WiFi uplink");
}

void EPPPServerComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "EPPP SPI Server:");
  ESP_LOGCONFIG(TAG, "  MOSI Pin: %d", this->mosi_pin_);
  ESP_LOGCONFIG(TAG, "  MISO Pin: %d", this->miso_pin_);
  ESP_LOGCONFIG(TAG, "  SCLK Pin: %d", this->sclk_pin_);
  ESP_LOGCONFIG(TAG, "  CS Pin: %d", this->cs_pin_);
  ESP_LOGCONFIG(TAG, "  Handshake Pin: %d", this->handshake_pin_);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Setup failed!");
  }
}

}  // namespace eppp_server
}  // namespace esphome

#endif  // USE_ESP_IDF
