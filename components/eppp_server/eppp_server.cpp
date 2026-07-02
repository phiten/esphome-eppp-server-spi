#include "eppp_server.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"  // esp_ip4_addr_t, ESP_IP4TOADDR -- eppp_link.h assumes these are already visible
#include "driver/spi_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_wifi.h"

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

static void eppp_status_task(void *arg) {
  esp_netif_t *eppp_netif = static_cast<esp_netif_t *>(arg);

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10000));

    uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000);
    bool eppp_up = esp_netif_is_netif_up(eppp_netif);
    uint32_t heap = esp_get_free_heap_size();

    /* WiFi RSSI */
    int rssi = 0;
    bool wifi_connected = false;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
      rssi = ap_info.rssi;
      wifi_connected = true;
    }

    /* WiFi IP */
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif)
      esp_netif_get_ip_info(sta_netif, &ip_info);

    /* EPPP IP and peer info */
    esp_netif_ip_info_t eppp_ip_info;
    memset(&eppp_ip_info, 0, sizeof(eppp_ip_info));
    if (eppp_netif)
      esp_netif_get_ip_info(eppp_netif, &eppp_ip_info);

    bool eppp_session_active = eppp_up && eppp_ip_info.gw.u_addr.ip4.addr != 0;

    ESP_LOGI(TAG, "[up=%lus] eppp_netif=%s eppp_session=%s local=" IPSTR " peer=" IPSTR " wifi=%ddBm ip=" IPSTR " heap=%lu",
             uptime,
             eppp_up ? "UP" : "DOWN",
             eppp_session_active ? "ACTIVE" : "inactive",
             IP2STR(&eppp_ip_info.ip),
             IP2STR(&eppp_ip_info.gw),
             rssi,
             IP2STR(&ip_info.ip),
             heap);
  }
}

void EPPPServerComponent::eppp_init_task(void *arg) {
  auto *self = static_cast<EPPPServerComponent *>(arg);

  // Small delay to allow ESPHome/IDF subsystems to initialize and avoid
  // racing with system queue creation in other components.
  vTaskDelay(pdMS_TO_TICKS(3000));

  ESP_LOGCONFIG(TAG, "EPPP init task starting...");

  eppp_config_t config = EPPP_DEFAULT_SERVER_CONFIG();
  config.transport = EPPP_TRANSPORT_SPI;
  config.spi.host = SPI2_HOST;
  config.spi.is_master = false;
  config.spi.mosi = self->mosi_pin_;
  config.spi.miso = self->miso_pin_;
  config.spi.sclk = self->sclk_pin_;
  config.spi.cs = self->cs_pin_;
  config.spi.intr = self->handshake_pin_;
  config.spi.freq = 16 * 1000 * 1000;
  config.spi.input_delay_ns = 0;
  config.spi.cs_ena_pretrans = 0;
  config.spi.cs_ena_posttrans = 0;

  ESP_LOGI(TAG, "EPPP SPI transport config: host=%d master=%d mosi=%d miso=%d sclk=%d cs=%d intr=%d freq=%uHz delay=%u ns",
           config.spi.host,
           config.spi.is_master ? 1 : 0,
           config.spi.mosi,
           config.spi.miso,
           config.spi.sclk,
           config.spi.cs,
           config.spi.intr,
           config.spi.freq,
           config.spi.input_delay_ns);

  // Use library perform task (matching the standalone reference behavior).
  config.task.run_task = false;

  self->eppp_netif_ = eppp_init(EPPP_SERVER, &config);
  if (self->eppp_netif_ == nullptr) {
    ESP_LOGE(TAG, "eppp_init() failed");
    self->mark_failed();
    vTaskDelete(NULL);
    return;
  }

  if (eppp_netif_start(self->eppp_netif_) != ESP_OK) {
    ESP_LOGE(TAG, "eppp_netif_start() failed");
    self->mark_failed();
    vTaskDelete(NULL);
    return;
  }

  esp_netif_ip_info_t eppp_ip_info;
  memset(&eppp_ip_info, 0, sizeof(eppp_ip_info));
  esp_err_t ip_err = esp_netif_get_ip_info(self->eppp_netif_, &eppp_ip_info);
  if (ip_err == ESP_OK) {
    ESP_LOGI(TAG, "EPPP interface started, local IP=" IPSTR " netmask=" IPSTR " peer=" IPSTR,
             IP2STR(&eppp_ip_info.ip), IP2STR(&eppp_ip_info.netmask), IP2STR(&eppp_ip_info.gw));
  } else {
    ESP_LOGW(TAG, "EPPP interface started but ip info unavailable: %s", esp_err_to_name(ip_err));
  }

  if (xTaskCreate(eppp_perform_task, "eppp", 4096, self->eppp_netif_, 5, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "failed to create EPPP perform task");
    self->mark_failed();
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGCONFIG(TAG, "EPPP SPI server started from init task");

  esp_err_t nat_err = esp_netif_napt_enable(self->eppp_netif_);
  if (nat_err != ESP_OK) {
    ESP_LOGW(TAG, "esp_netif_napt_enable() failed: %s", esp_err_to_name(nat_err));
    self->napt_enabled_ = false;
  } else {
    self->napt_enabled_ = true;
    ESP_LOGI(TAG, "NAT enabled on EPPP interface");
  }

  // Start periodic status logger (helps debug uplink/netif state)
  if (xTaskCreate(eppp_status_task, "eppp_srv_status", 3072, self->eppp_netif_, 1, nullptr) != pdPASS) {
    ESP_LOGW(TAG, "failed to create EPPP status task");
  }

  vTaskDelete(NULL);
}

void EPPPServerComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up EPPP SPI server...");

  ESP_LOGCONFIG(TAG, "Scheduling EPPP init task...");
  // Start the deferred init task so setup() returns quickly to ESPHome.
  if (xTaskCreate(EPPPServerComponent::eppp_init_task, "eppp_init", 3072, this, 5, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "failed to create EPPP init task");
    this->mark_failed();
  }
  // Defer all EPPP initialization to the init task above.
  return;
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
