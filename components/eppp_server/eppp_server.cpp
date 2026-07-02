#include "eppp_server.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

#include "esp_err.h"
#include "esp_netif_ip_addr.h"  // esp_ip4_addr_t, ESP_IP4TOADDR -- eppp_link.h assumes these are already visible
#include "driver/spi_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "eppp_link.h"
}

namespace esphome {
namespace eppp_server {

static const char *const TAG = "eppp_server";

static void eppp_perform_task(void *arg) {
  esp_netif_t *netif = static_cast<esp_netif_t *>(arg);
  while (eppp_perform(netif) != ESP_ERR_TIMEOUT) {
  }
  vTaskDelete(NULL);
}

void EPPPServerComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up EPPP SPI server...");

  // ---------------------------------------------------------------------
  // Use the reference standalone EPPP server defaults, then override the
  // transport-specific SPI fields to match the working main.c implementation.
  // ---------------------------------------------------------------------
  eppp_config_t config = EPPP_DEFAULT_SERVER_CONFIG();

  config.transport = EPPP_TRANSPORT_SPI;
  config.spi.host = SPI2_HOST;  // SPI2_HOST/HSPI -- matches the reference implementation
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

  config.task.run_task = false;

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

  if (xTaskCreate(eppp_perform_task, "eppp", 4096, this->eppp_netif_, 5, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "failed to create EPPP perform task");
    this->mark_failed();
    return;
  }

  ESP_LOGCONFIG(TAG, "EPPP SPI server started, waiting for peer + uplink");

  this->napt_enabled_ = false;
}

void EPPPServerComponent::loop() {
  // No periodic work is required here for generic EPPP SPI server operation.
}

void EPPPServerComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "EPPP SPI Server:");
  ESP_LOGCONFIG(TAG, "  MOSI Pin: %d", this->mosi_pin_);
  ESP_LOGCONFIG(TAG, "  MISO Pin: %d", this->miso_pin_);
  ESP_LOGCONFIG(TAG, "  SCLK Pin: %d", this->sclk_pin_);
  ESP_LOGCONFIG(TAG, "  CS Pin: %d", this->cs_pin_);
  ESP_LOGCONFIG(TAG, "  Handshake Pin: %d", this->handshake_pin_);
  ESP_LOGCONFIG(TAG, "  NAT enabled: %s", this->napt_enabled_ ? "yes" : "no");
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Setup failed!");
  }
}

}  // namespace eppp_server
}  // namespace esphome

#endif  // USE_ESP32
