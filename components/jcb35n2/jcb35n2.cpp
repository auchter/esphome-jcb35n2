#include "jcb35n2.h"

namespace esphome {
namespace jcb35n2 {

static const char *const TAG = "JCB35N2";

ButtonDetector::ButtonDetector()
{
  edge_time_.fill(0);
  last_edge_time_.fill(0);
  lockout_ = millis() + 1000;
  last_val_ = false;
}

void ButtonDetector::set_button(GPIOPin *pin)
{
  button_ = pin;
  button_->setup();
}

void ButtonDetector::on_double_click(std::function<void(void)> &&action)
{
  double_click_ = std::move(action);
}

void ButtonDetector::on_press(std::function<void(bool)> &&action)
{
  press_ = std::move(action);
}

void ButtonDetector::update()
{
  if (!button_)
    return;

  const bool val = !this->button_->digital_read();
  const auto ms = millis();

  if (val != last_val_) {
    last_edge_time_[val] = edge_time_[val];
    edge_time_[val] = ms;
    // ESP_LOGD(TAG, "%d %d", ms, val);
  }

  if ((edge_time_[EDGE_RISING] > edge_time_[EDGE_FALLING] && val) &&
      (ms - edge_time_[EDGE_RISING]) > 1000) {
    this->dispatch_press(true);
  } else {
    this->dispatch_press(false);
  }

  if (((edge_time_[EDGE_RISING] - last_edge_time_[EDGE_RISING]) < 1000)
      && ((ms - edge_time_[EDGE_FALLING]) < 1000) &&
      (last_edge_time_[EDGE_RISING] > lockout_)) {
    lockout_ = ms + 1000;
    if (this->double_click_)
      this->double_click_();
  }

  last_val_ = val;
}

void ButtonDetector::dispatch_press(bool val)
{
  if (val != last_press_) {
    press_(val);
    last_press_ = val;
  }
}

void DeskPresetSelect::height_changed()
{
  control("None");
}

void DeskPresetSelect::control(const std::string &value)
{
  auto index = this->index_of(value);
  if (index) {
    auto height = heights_[*index];

    // only set height if value is not None
    if (*index != 0)
      desk_->set_desk_height(height);

    this->publish_state(value);
  }
}

void DeskPresetSelect::update()
{
}

void DeskPresetSelect::add_preset_height(float value)
{
  heights_.push_back(value);
}

void DeskPresetSelect::set_desk(JCB35N2 *desk)
{
  desk_ = desk;
}

void DeskUart::set_uart(uart::UARTDevice *uart)
{
  this->uart_ = uart;
}

optional<uint8_t> DeskUart::process_height()
{
  int available = uart_->available();
  int v;
  optional<uint8_t> filtered_height;

  // JCB35N2 controller messages
  //   0x01 0x01 0x01 0xXX - XX is proportional to height, 0.1" per
  //   0x01 0x05 0x01 0xAA - end of messages
  // Messages are sent once movement begins (around 23ms apart), and
  // continue for approximately 10 seconds after movement stops.
  //
  // I've occasionally noticed glitches in the upper nibble of the height
  // report byte. To guard against this, require two consecutive heights
  // before accepting the value.
  while (available-- > 0) {
    v = uart_->read();
    switch (state_) {
    case STATE_UNSYNC:
      if (v == 0x01)
        state_ = STATE_PRE_1;
      break;
    case STATE_PRE_1:
      if (v == 0x01)
        state_ = STATE_PRE_2;
      else
        state_ = STATE_UNSYNC;
      break;
    case STATE_PRE_2:
      if (v == 0x01)
        state_ = STATE_HEIGHT;
      else
        state_ = STATE_UNSYNC;
      break;
    case STATE_HEIGHT:
      if (height_)
        last_height_ = height_;
      height_ = static_cast<uint8_t>(v);
      if (height_ == last_height_)
        filtered_height = *height_;

      state_ = STATE_UNSYNC;
      break;
    }
  }

  return filtered_height;
}

void JCB35N2::setup()
{
  this->desk_uart_.set_uart(this);

  this->up_output_->setup();
  this->down_output_->setup();
  this->move(Direction::Stop);

  // trigger uart output so we can obtain the current height
  this->move(Direction::MoveUp);
  delay(10);
  this->move(Direction::Stop);
}

void JCB35N2::loop()
{
  auto height = desk_uart_.process_height();
  if (height)
    cur_height_ = *height;

  if (target_height_ && cur_height_) {
    const auto slop = 5;
    int diff = *cur_height_ - *target_height_;
    if (diff < -slop) {
      this->move(Direction::MoveUp, false);
    } else if (diff > slop) {
      this->move(Direction::MoveDown, false);
    } else {
      this->move(Direction::Stop, false);
      target_height_.reset();
    }
  } else {
    this->up_detector_.update();
    this->down_detector_.update();
  }
}

void JCB35N2::update()
{
  if (cur_height_ && *cur_height_ != *last_reported_height_) {
    height_sensor_->publish_state(*cur_height_);
    this->publish_state(raw_to_inches(*cur_height_));
    last_reported_height_ = *cur_height_;
  }
}

void JCB35N2::set_desk_height(float value)
{
  if (value > 0) {
    target_height_ = inches_to_raw(value);
  } else {
    target_height_.reset();
  }
}

void JCB35N2::move(Direction dir, bool manual)
{
  bool up = dir == Direction::MoveUp;
  bool down = dir == Direction::MoveDown;

  this->up_output_->digital_write(!up);
  this->down_output_->digital_write(!down);

  if (manual && this->preset_select_)
    this->preset_select_->height_changed();
}

void JCB35N2::control(float value)
{
  if (this->preset_select_)
    this->preset_select_->height_changed();
  this->set_desk_height(value);
}

float JCB35N2::raw_to_inches(int val) const
{
  return (val - min_raw_) * 0.1f + min_inches_;
}

int JCB35N2::inches_to_raw(float val) const
{
  return (val - min_inches_) / 0.1f + min_raw_;
}

void JCB35N2::set_height_sensor(sensor::Sensor *height)
{
  height_sensor_ = height;
}

void JCB35N2::set_preset_select(DeskPresetSelect *select)
{
  select->set_desk(this);
  preset_select_ = select;
}

void JCB35N2::set_calibration(int min_raw, int max_raw, float min_inches)
{
  min_raw_ = min_raw;
  max_raw_ = max_raw;
  min_inches_ = min_inches;
}

void JCB35N2::set_up_button(GPIOPin *pin, const char *preset)
{
  up_detector_.set_button(pin);
  up_detector_.on_press([this](bool val){
    this->move(val ? Direction::MoveUp : Direction::Stop);
  });
  if (preset) {
    up_detector_.on_double_click([this, preset](){
      auto call = this->preset_select_->make_call();
      call.set_option(preset).perform();
    });
  }
}

void JCB35N2::set_down_button(GPIOPin *pin, const char *preset)
{
  down_detector_.set_button(pin);
  down_detector_.on_press([this](bool val){
    this->move(val ? Direction::MoveDown : Direction::Stop);
  });
  if (preset) {
    down_detector_.on_double_click([this, preset](){
      auto call = this->preset_select_->make_call();
      call.set_option(preset).perform();
    });
  }
}

void JCB35N2::set_up_output(GPIOPin *pin)
{
  up_output_ = pin;
}

void JCB35N2::set_down_output(GPIOPin *pin)
{
  down_output_ = pin;
}

}
}
