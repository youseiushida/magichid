// AUTO-GENERATED from spec/protocol.yaml by tools/gen_protocol.py. Do not edit.
#ifndef MH_PROTOCOL_DEFS_H_
#define MH_PROTOCOL_DEFS_H_

#define MH_PROTO_VERSION    3
#define MH_DELIM            0x00
#define MH_MAX_PAYLOAD      192
#define MH_HID_MAX_PAYLOAD  63
#define MH_DEDUP_WINDOW     16

// message types
#define MH_T_SEND_REPORT    0x01
#define MH_T_PING           0x02
#define MH_T_RELEASE_ALL    0x03
#define MH_T_GET_CAPS       0x04
#define MH_T_SET_IDENTITY   0x05
#define MH_T_SET_FEATURE    0x06
#define MH_T_SESSION_OPEN   0x07
#define MH_T_STATUS         0x81
#define MH_T_ACK            0x82
#define MH_T_NACK           0x83
#define MH_T_HOST_EVENT     0x84
#define MH_T_LOG            0x85
#define MH_T_CAPS           0x86
#define MH_T_SESSION        0x87

// STATUS flag bits
#define MH_ST_MOUNTED      0x01
#define MH_ST_SUSPENDED    0x02
#define MH_ST_READY        0x04
#define MH_ST_WATCHDOG     0x08

// NACK reasons
#define MH_NACK_BAD_CRC       1
#define MH_NACK_BAD_LEN       2
#define MH_NACK_UNKNOWN_ID    3
#define MH_NACK_NOT_READY     4
#define MH_NACK_NOT_SENDABLE  5
#define MH_NACK_BAD_FRAME     6
#define MH_NACK_TOO_BIG       7

// report flags (the [flags] byte of each CAPS entry)
#define MH_REPORT_FLAG_RELATIVE  0x01

#endif // MH_PROTOCOL_DEFS_H_
