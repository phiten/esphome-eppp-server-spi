#pragma once

#include "esphome/core/component.h"

#ifdef USE_ESP32

#include "esp_netif.h"

namespace esphome {
namespace eppp_server {

class EPPPServerComponent : public Component {
 public:
  void set_mosi_pin(int8_t pin) { this->mosi_pin_ = pin; }
  void set_miso_pin(int8_t pin) { this->miso_pin_ = pin; }
  void set_sclk_pin(int8_t pin) { this->sclk_pin_ = pin; }
  void set_cs_pin(int8_t pin) { this->cs_pin_ = pin; }
  void set_handshake_pin(int8_t pin) { this->handshake_pin_ = pin; }

  void setup() override;
  void loop() override;
  void dump_config() override;

  // Must come up after WiFi has had a chance to start associating, but
  // doesn't strictly need to wait for an IP -- NAT enabling itself waits
  // for that separately in loop().
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

 protected:
  void try_enable_napt_();

  int8_t mosi_pin_{-1};
  int8_t miso_pin_{-1};
  int8_t sclk_pin_{-1};
  int8_t cs_pin_{-1};
  int8_t handshake_pin_{-1};

  esp_netif_t *eppp_netif_{nullptr};
  bool napt_enabled_{false};
  bool wifi_was_connected_{false};
};

}  // namespace eppp_server
}  // namespace esphome

#endif  // USE_ESP32
