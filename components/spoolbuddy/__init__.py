import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

DEPENDENCIES = ["network", "http_request"]
AUTO_LOAD = ["sensor"]
CODEOWNERS = []

CONF_URL = "url"
CONF_API_KEY = "api_key"
CONF_DEVICE_ID = "device_id"
CONF_HOSTNAME = "hostname"
CONF_HEARTBEAT_INTERVAL = "heartbeat_interval"
CONF_NFC_READER = "nfc_reader"
CONF_NFC_READER_TYPE = "nfc_reader_type"
CONF_NFC_CONNECTION = "nfc_connection"
CONF_SCALE = "scale"

spoolbuddy_ns = cg.esphome_ns.namespace("spoolbuddy")
SpoolBuddy = spoolbuddy_ns.class_("SpoolBuddy", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SpoolBuddy),
        # BambuBuddy base URL, e.g. https://bambuddy.example.lan
        cv.Required(CONF_URL): cv.string,
        cv.Required(CONF_API_KEY): cv.string,
        # Must match the device_id sent with /nfc/tag-scanned so scans are attributed to this device
        cv.Required(CONF_DEVICE_ID): cv.All(cv.string, cv.Length(min=1, max=50)),
        cv.Required(CONF_HOSTNAME): cv.All(cv.string, cv.Length(min=1, max=100)),
        # BambuBuddy marks a device offline after 30 s without a heartbeat
        cv.Optional(CONF_HEARTBEAT_INTERVAL, default="15s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(seconds=5), max=cv.TimePeriod(seconds=25)),
        ),
        # NFC reader component, used to report nfc_ok
        cv.Optional(CONF_NFC_READER): cv.use_id(cg.Component),
        cv.Optional(CONF_NFC_READER_TYPE, default="pn532"): cv.All(cv.string, cv.Length(max=20)),
        cv.Optional(CONF_NFC_CONNECTION, default="i2c"): cv.All(cv.string, cv.Length(max=20)),
        # Raw load-cell sensor (e.g. hx711 without filters): enables the SpoolBuddy scale protocol
        cv.Optional(CONF_SCALE): cv.use_id(sensor.Sensor),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_url(config[CONF_URL].rstrip("/")))
    cg.add(var.set_api_key(config[CONF_API_KEY]))
    cg.add(var.set_device_id(config[CONF_DEVICE_ID]))
    cg.add(var.set_hostname(config[CONF_HOSTNAME]))
    cg.add(var.set_heartbeat_interval(config[CONF_HEARTBEAT_INTERVAL]))
    cg.add(var.set_nfc_reader_type(config[CONF_NFC_READER_TYPE]))
    cg.add(var.set_nfc_connection(config[CONF_NFC_CONNECTION]))
    if CONF_NFC_READER in config:
        reader = await cg.get_variable(config[CONF_NFC_READER])
        cg.add(var.set_nfc_reader(reader))
    if CONF_SCALE in config:
        scale = await cg.get_variable(config[CONF_SCALE])
        cg.add(var.set_scale(scale))
