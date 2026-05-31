"""AUTO-GENERATED from spec/protocol.yaml by tools/gen_protocol.py. Do not edit."""
PROTO_VERSION = 1
DELIM = 0x00
MAX_PAYLOAD = 192
HID_MAX_PAYLOAD = 63

# message types
T_SEND_REPORT = 0x01
T_PING = 0x02
T_RELEASE_ALL = 0x03
T_GET_CAPS = 0x04
T_SET_IDENTITY = 0x05
T_SET_FEATURE = 0x06
T_STATUS = 0x81
T_ACK = 0x82
T_NACK = 0x83
T_HOST_EVENT = 0x84
T_LOG = 0x85
T_CAPS = 0x86

# STATUS flag bits
ST_MOUNTED = 0x01
ST_SUSPENDED = 0x02
ST_READY = 0x04
ST_WATCHDOG = 0x08

# NACK reasons (code -> name)
NACK_REASONS = {
    1: 'BAD_CRC',
    2: 'BAD_LEN',
    3: 'UNKNOWN_ID',
    4: 'NOT_READY',
    5: 'NOT_SENDABLE',
    6: 'BAD_FRAME',
    7: 'TOO_BIG',
}

# HID report types
HID_INPUT = 1
HID_OUTPUT = 2
HID_FEATURE = 3
