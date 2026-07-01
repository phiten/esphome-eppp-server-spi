#include "eppp_server.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"

#include "esp_err.h"
#include "esp_netif_ip_addr.h"  // esp_ip4_addr_t, ESP_IP4TOADDR -- eppp_link.h assumes these are already visible

extern "C" {
#include "eppp_link.h"
}

namespace esphome {
namespace eppp_server {

static const char *const TAG = "eppp_server";

void EPPPServerComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up EPPP SPI server...");

  // ---------------------------------------------------------------------
  // Struct fields confirmed from the vendored eppp_link.h / eppp_transport_spi.h
  // (espressif/eppp_link, as fetched into managed_components/).
  // ---------------------------------------------------------------------
  eppp_config_t config = {};  // zero-init: unset fields (e.g. is_master) default sanely

  config.transport = EPPP_TRANSPORT_SPI;

  config.spi.host = 1;  // SPI2_HOST/HSPI -- matches eppp_link's own default; adjust if your
                         // Phase-0 firmware used a different SPI peripheral
  config.spi.is_master = false;  // this device is the SPI SLAVE: the external MCU drives the bus
  config.spi.mosi = this->mosi_pin_;
  config.spi.miso = this->miso_pin_;
  config.spi.sclk = this->sclk_pin_;
  config.spi.cs = this->cs_pin_;
  config.spi.intr = this->handshake_pin_;  // data-ready/handshake line, slave -> master
  config.spi.freq = 16 * 1000 * 1000;      // matches eppp_link's own default; lower if you see errors
  config.spi.input_delay_ns = 0;
  config.spi.cs_ena_pretrans = 0;
  config.spi.cs_ena_posttrans = 0;

  config.task.run_task = true;
  config.task.stack_size = 4096;
  config.task.priority = 8;

  config.ppp.our_ip4_addr.addr = EPPP_DEFAULT_SERVER_IP();
  config.ppp.their_ip4_addr.addr = EPPP_DEFAULT_CLIENT_IP();
  config.ppp.netif_prio = 0;
  config.ppp.netif_description = nullptr;

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

#endif  // USE_ESP32
