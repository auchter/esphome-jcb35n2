import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import sensor, uart, number, select
from esphome.const import CONF_ID, UNIT_EMPTY, ICON_EMPTY, DEVICE_CLASS_DISTANCE, CONF_OPTIONS

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "number", "select"]

jcb35n2_ns = cg.esphome_ns.namespace('jcb35n2')

JCB35N2 = jcb35n2_ns.class_('JCB35N2', uart.UARTDevice, cg.PollingComponent)

DeskPresetSelect = jcb35n2_ns.class_('DeskPresetSelect', select.Select, cg.PollingComponent)

preset_entry = {
    cv.Required("name"): cv.string,
    cv.Required("height"): cv.float_,
    cv.Optional("double_click"): cv.string,
}

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(JCB35N2),
            cv.Required("buttons"):
            {
                cv.Required("up"): pins.gpio_input_pullup_pin_schema,
                cv.Required("down"): pins.gpio_input_pullup_pin_schema,
            },
            cv.Required("outputs"):
            {
                cv.Required("up"): pins.gpio_output_pin_schema,
                cv.Required("down"): pins.gpio_output_pin_schema,
            },
            cv.Optional("raw_height"): sensor.sensor_schema(
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_DISTANCE,
            ),
            cv.Optional("setpoint"): number.NUMBER_SCHEMA.extend(
                {
                    cv.Required("min_inches"): cv.float_,
                    cv.Required("min_raw"): cv.int_range(1, 500),
                    cv.Required("max_raw"): cv.int_range(1, 500),
                }
            ),
            cv.Optional("presets"): select.SELECT_SCHEMA.extend(
                {
                    cv.GenerateID(): cv.declare_id(DeskPresetSelect),
                    cv.Required(CONF_OPTIONS): cv.ensure_list(preset_entry),
                }
            ),
        }
    )
    .extend(cv.polling_component_schema("1s"))
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA),
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    sens = await sensor.new_sensor(config["raw_height"])
    cg.add(var.set_height_sensor(sens))

    if 'setpoint' in config:
        config['setpoint']['unit_of_measurement'] = "in"
        setpoint_conf = config['setpoint']
        cg.add(var.set_calibration(setpoint_conf['min_raw'],
                                   setpoint_conf['max_raw'],
                                   setpoint_conf['min_inches']))

        delta = setpoint_conf["max_raw"] - setpoint_conf["min_raw"]
        step = 0.1
        num = await number.register_number(
                var,
                config['setpoint'],
                min_value=setpoint_conf['min_inches'],
                max_value=setpoint_conf['min_inches'] + (step * delta),
                step=step)

    double_click_preset = {"up": None, "down": None}

    if 'presets' in config:
        assert 'setpoint' in config
        # First preset, None, is a placeholder.
        preset_options = ["None"] + [p['name'] for p in config['presets'][CONF_OPTIONS]]
        p = cg.new_Pvariable(config['presets'][CONF_ID])
        await cg.register_component(p, config['presets'])
        await select.register_select(p, config['presets'], options=preset_options)
        cg.add(p.add_preset_height(-1.0))
        for preset in config['presets'][CONF_OPTIONS]:
            cg.add(p.add_preset_height(preset['height']))
            if 'double_click' in preset:
                key = preset['double_click']
                assert key in double_click_preset.keys()
                assert double_click_preset[key] is None
                double_click_preset[key] = preset["name"]

        cg.add(var.set_preset_select(p))

    up_pin = await cg.gpio_pin_expression(config['buttons']['up'])
    cg.add(var.set_up_button(up_pin, double_click_preset['up']))

    down_pin = await cg.gpio_pin_expression(config['buttons']['down'])
    cg.add(var.set_down_button(down_pin, double_click_preset['down']))

    up_out = await cg.gpio_pin_expression(config['outputs']['up'])
    cg.add(var.set_up_output(up_out))

    down_out = await cg.gpio_pin_expression(config['outputs']['down'])
    cg.add(var.set_down_output(down_out))

def validate_uart(config):
    uart.final_validate_device_schema("jcb35n2", baud_rate=9600, require_rx=True, require_tx=False)(config)

FINAL_VALIDATE_SCHEMA = validate_uart

