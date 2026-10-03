import esphome.codegen as cg
from esphome import automation
from esphome.components import i2c, text_sensor, sensor, button
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TRIGGER_ID

AUTO_LOAD = ["text_sensor", "sensor", "button"]
DEPENDENCIES = ["i2c"]
CODEOWNERS = []

CONF_FILAMENT_TYPE = "filament_type"
CONF_FILAMENT_COLOR = "filament_color"
CONF_MIN_HOTEND_TEMP = "min_hotend_temp"
CONF_MAX_HOTEND_TEMP = "max_hotend_temp"
CONF_BED_TEMP = "bed_temp"
CONF_TRAY_UID = "tray_uid"
CONF_TRAY_INFO_IDX = "tray_info_idx"
CONF_PRODUCTION_DATE = "production_date"
CONF_LAST_SCAN_DATE = "last_scan_date"
CONF_SPOOL_WEIGHT = "spool_weight"
CONF_FILAMENT_DIAMETER = "filament_diameter"
CONF_DRYING_TEMP = "drying_temp"
CONF_DRYING_TIME = "drying_time"
CONF_NOZZLE_DIAMETER = "nozzle_diameter"
CONF_SPOOL_WIDTH = "spool_width"
CONF_FILAMENT_LENGTH = "filament_length"
CONF_MATERIAL_DENSITY = "material_density"
CONF_FILAMENT_COLOR_NAME = "filament_color_name"
CONF_FILAMENT_SUBTYPE = "filament_subtype"
CONF_VARIANT_ID = "variant_id"
CONF_CARD_UID = "card_uid"
CONF_RESET = "reset"
CONF_ON_BAMBU_SUCCESS = "on_bambu_success"
CONF_ON_BAMBU_ERROR = "on_bambu_error"
CONF_ON_TAG_REMOVED = "on_tag_removed"
CONF_ON_NTAG_TAG = "on_ntag_tag"
CONF_READER = "reader"

bambu_nfc_ns = cg.esphome_ns.namespace("bambu_rc522")
BambuNfc = bambu_nfc_ns.class_("BambuRc522", cg.PollingComponent, i2c.I2CDevice)
BambuNfcResetButton = bambu_nfc_ns.class_(
    "BambuRc522ResetButton", button.Button, cg.Parented.template(BambuNfc)
)
BambuSuccessTrigger = bambu_nfc_ns.class_("BambuSuccessTrigger", automation.Trigger.template())
BambuErrorTrigger = bambu_nfc_ns.class_("BambuErrorTrigger", automation.Trigger.template())
BambuTagRemovedTrigger = bambu_nfc_ns.class_("BambuTagRemovedTrigger", automation.Trigger.template())
BambuNtagTrigger = bambu_nfc_ns.class_("BambuNtagTrigger", automation.Trigger.template())
ReaderType = bambu_nfc_ns.enum("ReaderType")
READERS = {"ws1850s": ReaderType.READER_WS1850S, "pn532": ReaderType.READER_PN532}

TEXT_SENSORS = [
    (CONF_FILAMENT_TYPE, "set_filament_type_sensor", {"icon": "mdi:tag-text"}),
    (CONF_FILAMENT_COLOR, "set_filament_color_sensor", {"icon": "mdi:palette"}),
    (CONF_TRAY_UID, "set_tray_uid_sensor", {"icon": "mdi:identifier"}),
    (CONF_TRAY_INFO_IDX, "set_tray_info_idx_sensor", {"icon": "mdi:barcode"}),
    (CONF_PRODUCTION_DATE, "set_production_date_sensor", {"icon": "mdi:calendar"}),
    (CONF_LAST_SCAN_DATE, "set_last_scan_date_sensor", {"icon": "mdi:nfc-tap", "device_class": "timestamp"}),
    (CONF_FILAMENT_COLOR_NAME, "set_filament_color_name_sensor", {"icon": "mdi:format-color-fill"}),
    (CONF_FILAMENT_SUBTYPE, "set_filament_subtype_sensor", {"icon": "mdi:tag-text-outline"}),
    (CONF_VARIANT_ID, "set_variant_id_sensor", {"icon": "mdi:tag"}),
    (CONF_CARD_UID, "set_card_uid_sensor", {"icon": "mdi:nfc"}),
]

SENSORS = [
    (CONF_MIN_HOTEND_TEMP, "set_min_temp_sensor", {"unit_of_measurement": "°C", "accuracy_decimals": 0, "device_class": "temperature", "icon": "mdi:thermometer-low"}),
    (CONF_MAX_HOTEND_TEMP, "set_max_temp_sensor", {"unit_of_measurement": "°C", "accuracy_decimals": 0, "device_class": "temperature", "icon": "mdi:thermometer-high"}),
    (CONF_BED_TEMP, "set_bed_temp_sensor", {"unit_of_measurement": "°C", "accuracy_decimals": 0, "device_class": "temperature", "icon": "mdi:heat-wave"}),
    (CONF_SPOOL_WEIGHT, "set_spool_weight_sensor", {"unit_of_measurement": "g", "accuracy_decimals": 0, "device_class": "weight", "icon": "mdi:weight-gram"}),
    (CONF_FILAMENT_DIAMETER, "set_filament_diameter_sensor", {"unit_of_measurement": "mm", "accuracy_decimals": 2, "device_class": "distance", "icon": "mdi:diameter-variant"}),
    (CONF_DRYING_TEMP, "set_drying_temp_sensor", {"unit_of_measurement": "°C", "accuracy_decimals": 0, "device_class": "temperature", "icon": "mdi:fan"}),
    (CONF_DRYING_TIME, "set_drying_time_sensor", {"unit_of_measurement": "h", "accuracy_decimals": 0, "device_class": "duration", "icon": "mdi:timer-outline"}),
    (CONF_NOZZLE_DIAMETER, "set_nozzle_diameter_sensor", {"unit_of_measurement": "mm", "accuracy_decimals": 2, "device_class": "distance", "icon": "mdi:printer-3d-nozzle"}),
    (CONF_SPOOL_WIDTH, "set_spool_width_sensor", {"unit_of_measurement": "mm", "accuracy_decimals": 1, "device_class": "distance", "icon": "mdi:ruler"}),
    (CONF_FILAMENT_LENGTH, "set_filament_length_sensor", {"unit_of_measurement": "m", "accuracy_decimals": 0, "device_class": "distance", "icon": "mdi:ruler"}),
    (CONF_MATERIAL_DENSITY, "set_material_density_sensor", {"unit_of_measurement": "g/cm³", "accuracy_decimals": 2, "icon": "mdi:cube-outline"}),
]

schema_dict = {
    cv.GenerateID(): cv.declare_id(BambuNfc),
    cv.Optional(CONF_RESET): button.button_schema(BambuNfcResetButton),
    cv.Optional(CONF_ON_BAMBU_SUCCESS): automation.validate_automation(
        {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(BambuSuccessTrigger)}
    ),
    cv.Optional(CONF_ON_BAMBU_ERROR): automation.validate_automation(
        {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(BambuErrorTrigger)}
    ),
    cv.Optional(CONF_ON_TAG_REMOVED): automation.validate_automation(
        {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(BambuTagRemovedTrigger)}
    ),
    cv.Optional(CONF_ON_NTAG_TAG): automation.validate_automation(
        {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(BambuNtagTrigger)}
    ),
    # ws1850s : lecteur interne du Dial (0x28) ; pn532 : module externe sur le port Grove (0x24)
    cv.Optional(CONF_READER, default="ws1850s"): cv.enum(READERS, lower=True),
}

for key, _, kwargs in TEXT_SENSORS:
    schema_dict[cv.Optional(key)] = text_sensor.text_sensor_schema(**kwargs)

for key, _, kwargs in SENSORS:
    schema_dict[cv.Optional(key)] = sensor.sensor_schema(**kwargs)

CONFIG_SCHEMA = (
    cv.Schema(schema_dict)
    .extend(cv.polling_component_schema("500ms"))
    .extend(i2c.i2c_device_schema(0x28))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_reader(config[CONF_READER]))

    for key, setter, _ in TEXT_SENSORS:
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(var, setter)(sens))

    for key, setter, _ in SENSORS:
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(var, setter)(sens))

    if CONF_RESET in config:
        btn = await button.new_button(config[CONF_RESET])
        await cg.register_parented(btn, var)

    for conf in config.get(CONF_ON_BAMBU_SUCCESS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_bambu_success_trigger(trigger))
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_BAMBU_ERROR, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_bambu_error_trigger(trigger))
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_TAG_REMOVED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_tag_removed_trigger(trigger))
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_NTAG_TAG, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_ntag_trigger(trigger))
        await automation.build_automation(trigger, [], conf)
