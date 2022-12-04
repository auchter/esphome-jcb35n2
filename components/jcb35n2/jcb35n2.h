#pragma once

#include <array>

#include "esphome/core/component.h"
#include "esphome/core/optional.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/uart/uart_component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/number/number.h"

namespace esphome {
namespace jcb35n2 {

class JCB35N2;

class ButtonDetector
{
public:
  ButtonDetector();

  void set_button(GPIOPin *pin);
  void update();

  // called each time the user double clicks the button
  void on_double_click(std::function<void(void)> &&action);

  // called with "true" once the button begins being pressed,
  // and "false" once the user releases the button
  void on_press(std::function<void(bool)> &&action);
private:
  void dispatch_press(bool val);

  GPIOPin *button_;

  enum {
    EDGE_FALLING = 0,
    EDGE_RISING = 1,
    EDGE_COUNT,
  };

  std::array<uint32_t, EDGE_COUNT> edge_time_;
  std::array<uint32_t, EDGE_COUNT> last_edge_time_;
  uint32_t lockout_;
  bool last_val_{false};
  bool last_press_{false};

  std::function<void(void)> double_click_;
  std::function<void(bool)> press_;
};

class DeskPresetSelect
  : public PollingComponent
  , public select::Select
{
public:
  void height_changed();
  void control(const std::string &value) override;
  void update() override;
  void add_preset_height(float value);
  void set_desk(JCB35N2 *desk);

private:
  std::vector<float> heights_;
  JCB35N2 *desk_;
};

class DeskUart
{
public:
  DeskUart() = default;

  void set_uart(uart::UARTDevice *uart);
  optional<uint8_t> process_height();

private:
  uart::UARTDevice *uart_{nullptr};

  optional<uint8_t> height_;
  optional<uint8_t> last_height_;

  enum {
    STATE_UNSYNC,
    STATE_PRE_1,
    STATE_PRE_2,
    STATE_HEIGHT,
  } state_{STATE_UNSYNC};
};

class JCB35N2
  : public PollingComponent
  , public number::Number
  , public uart::UARTDevice {
public:
  JCB35N2() = default;

  void setup() override;
  void loop() override;
  void update() override;

  void set_desk_height(float value);

  enum class Direction {
    Stop,
    MoveUp,
    MoveDown,
  };

  void move(Direction dir, bool manual = true);

protected:
  void control(float value) override;

private:
  optional<uint8_t> process_uart_height();

  float raw_to_inches(int val) const;
  int inches_to_raw(float val) const;

public:
  void set_height_sensor(sensor::Sensor *height);
  void set_preset_select(DeskPresetSelect *select);
  void set_calibration(int min_raw, int max_raw, float min_inches);
  void set_up_button(GPIOPin *pin, const char *preset = nullptr);
  void set_down_button(GPIOPin *pin, const char *preset = nullptr);
  void set_up_output(GPIOPin *pin);
  void set_down_output(GPIOPin *pin);

private:
  DeskUart desk_uart_;
  sensor::Sensor *height_sensor_{nullptr};
  DeskPresetSelect *preset_select_{nullptr};

  ButtonDetector up_detector_;
  ButtonDetector down_detector_;

  GPIOPin *up_output_{nullptr};
  GPIOPin *down_output_{nullptr};

  optional<int> cur_height_;
  optional<int> target_height_;
  optional<int> last_reported_height_;

  int min_raw_;
  int max_raw_;
  float min_inches_;
};

}
}
