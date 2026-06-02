// =====================================================================================
//  hid_descriptor.h  --  "MagicHID" Ultimate Chimera HID Device
// =====================================================================================
//
//  Target  : ESP32-S3  +  Adafruit TinyUSB library
//  Purpose : A single USB HID interface whose Report Descriptor embeds ONE Report ID
//            for every Usage Page that the official "HID Usage Tables for USB" (HUT)
//            version 1.7 defines (35 pages in total).
//
//  Reference: USB-IF "HID Usage Tables for USB" version 1.7 (download from usb.org).
//             Every Usage Page value, Usage ID, and Usage Type below was taken directly
//             from that specification; section numbers in comments (e.g. "HUT 23.12")
//             point at the relevant table. (The spec is copyrighted and not bundled here.)
//
//  Design rules followed (per project goal):
//    1. Every byte is a raw HID short-item literal — no TinyUSB macros remain.
//       This allows tools/gen_universal_reports.py to parse the entire descriptor
//       automatically without any macro special-casing.
//    2. Each page is given a realistic, representative Input / Output / Feature layout
//       (e.g. Sensors -> a 3-axis accelerometer, Medical -> ultrasound acquisition
//       controls, FIDO -> 64-byte CTAPHID packets).
//    3. Every raw byte is annotated on its own line: which HID item it is, which
//       Usage Page / Usage ID it selects, and the intent behind every Report Size /
//       Report Count.  The goal is that a human can diff this file against the HUT
//       table-by-table.
//    4. All 35 reports live inside a single `uint8_t const desc_hid_report[]`.
//    5. The Report ID symbols live in the `enum` directly below.
//    6. The descriptor is balanced (every Collection 0xA1 has a matching End 0xC0) and
//       is valid C array-initializer syntax (no missing commas, no duplicate items).
//
//  HID short-item primer (used throughout, so the hex makes sense):
//    Item prefix byte = (tag << 4) | (type << 2) | size,  where size is the number of
//    data bytes that follow (0,1,2 or 4 -> encoded as 0,1,2,3).
//      Main : Input 0x80 | Output 0x90 | Feature 0xB0 | Collection 0xA0 | EndColl 0xC0
//      Global: UsagePage 0x04 | LogicalMin 0x14 | LogicalMax 0x24 | PhysMin 0x34
//              PhysMax 0x44 | UnitExp 0x54 | Unit 0x64 | ReportSize 0x74 | ReportID 0x84
//              ReportCount 0x94 | Push 0xA4 | Pop 0xB4
//      Local : Usage 0x08 | UsageMin 0x18 | UsageMax 0x28
//    Input/Output/Feature data flag byte (low 8 bits): bit0 Const(1)/Data(0),
//      bit1 Var(1)/Array(0), bit2 Rel(1)/Abs(0), bit6 NullState, bit7 Volatile.
//      0x02 = Data,Var,Abs   0x03 = Const,Var,Abs (padding)   0x00 = Data,Array,Abs
//
//  Multi-byte constants used a lot (little-endian):
//      Logical Max 255   -> 0x26,0xFF,0x00  (2-byte form; 0x25,0xFF would be -1 signed)
//      Logical Max 32767 -> 0x26,0xFF,0x7F
//      Logical Min -32768-> 0x16,0x00,0x80
//      Logical Max 65535 -> 0x27,0xFF,0xFF,0x00,0x00 (4-byte form)
//      Logical Max 2^31-1-> 0x27,0xFF,0xFF,0xFF,0x7F
// =====================================================================================
//
//  /!\ RUNTIME / BUILD NOTES for ESP32-S3 + Adafruit TinyUSB
//  ------------------------------------------------------------------------------------
//  * HID single-report ceiling = CONFIG_TINYUSB_HID_BUFSIZE (default 64 on
//    arduino-esp32 3.3.x). The buffer holds 1 Report-ID byte + payload, so usable data
//    is 63 bytes. THREE reports here exceed that and will NOT transmit unmodified:
//        Report 24 Monitor (EDID Feature 130 B), Report 31 MSR (Input 229 B),
//        Report 35 FIDO (64 B data + 1 ID = 65 B).  The other 32 reports work as-is.
//    (Not changeable via a -D flag in Arduino IDE -- the buffer lives in the precompiled
//     core lib; enlarge via ESP-IDF menuconfig or a rebuilt core.)
//  * Reports 27 Power / 28 Battery make a host OS bind its power/UPS stack. Send safe
//    values (100% / AC present) or don't transmit them during bring-up, or the OS may
//    show a battery icon / act on a low-battery reading.
//  * Report 35 FIDO only functions as a real authenticator on its OWN HID interface
//    with NO Report ID (CTAPHID needs exact 64-byte frames). It enumerates here but
//    will not work as FIDO on this shared multi-report interface.
// =====================================================================================

#ifndef HID_DESCRIPTOR_H_
#define HID_DESCRIPTOR_H_

// Needed for the TinyUSB HID runtime (tud_hid_*, HID_REPORT_ID).  The descriptor
// itself no longer uses any TinyUSB macros — every byte is a raw HID short-item literal.
// This header is meant to be compiled inside an Arduino / ESP32-S3 sketch that has the
// Adafruit TinyUSB library installed.
#include "Adafruit_TinyUSB.h"

#include <stdint.h>

// -------------------------------------------------------------------------------------
//  Report ID enumeration.
//  One Report ID per HUT Usage Page.  The numeric value of the Report ID (1..35) is a
//  plain sequential index -- it is intentionally NOT the page's hex value, because HID
//  Report IDs must fit in a single byte (1..255) while some page numbers (0xF1D0) do not.
//  The hex page each report represents is written in the trailing comment.
// -------------------------------------------------------------------------------------
enum {
  REPORT_ID_GENERIC_DESKTOP = 1, // 0x01  Generic Desktop Page          (TinyUSB MOUSE macro)
  REPORT_ID_SIMULATION,          // 0x02  Simulation Controls Page
  REPORT_ID_VR,                  // 0x03  VR Controls Page
  REPORT_ID_SPORT,               // 0x04  Sport Controls Page
  REPORT_ID_GAME,                // 0x05  Game Controls Page
  REPORT_ID_GENERIC_DEVICE,      // 0x06  Generic Device Controls Page
  REPORT_ID_KEYBOARD,            // 0x07  Keyboard/Keypad Page          (TinyUSB KEYBOARD macro)
  REPORT_ID_LED,                 // 0x08  LED Page
  REPORT_ID_BUTTON,              // 0x09  Button Page
  REPORT_ID_ORDINAL,             // 0x0A  Ordinal Page
  REPORT_ID_TELEPHONY,           // 0x0B  Telephony Device Page
  REPORT_ID_CONSUMER,            // 0x0C  Consumer Page                 (TinyUSB CONSUMER macro)
  REPORT_ID_DIGITIZER,           // 0x0D  Digitizers Page
  REPORT_ID_HAPTICS,             // 0x0E  Haptics Page
  REPORT_ID_PID,                 // 0x0F  Physical Input Device Page (PID)
  REPORT_ID_UNICODE,             // 0x10  Unicode Page
  REPORT_ID_SOC,                 // 0x11  SoC Page
  REPORT_ID_EYE_HEAD_TRACKER,    // 0x12  Eye and Head Trackers Page
  REPORT_ID_AUX_DISPLAY,         // 0x14  Auxiliary Display Page
  REPORT_ID_SENSOR,              // 0x20  Sensors Page
  REPORT_ID_MEDICAL,             // 0x40  Medical Instrument Page
  REPORT_ID_BRAILLE,             // 0x41  Braille Display Page
  REPORT_ID_LIGHTING,            // 0x59  Lighting And Illumination Page (LampArray)
  REPORT_ID_MONITOR,             // 0x80  Monitor Page
  REPORT_ID_MONITOR_ENUM,        // 0x81  Monitor Enumerated Page
  REPORT_ID_VESA_VC,             // 0x82  VESA Virtual Controls Page
  REPORT_ID_POWER,               // 0x84  Power Page
  REPORT_ID_BATTERY,             // 0x85  Battery System Page
  REPORT_ID_BARCODE,             // 0x8C  Barcode Scanner Page
  REPORT_ID_SCALE,               // 0x8D  Scales Page
  REPORT_ID_MSR,                 // 0x8E  Magnetic Stripe Reader Page
  REPORT_ID_CAMERA,              // 0x90  Camera Control Page
  REPORT_ID_ARCADE,              // 0x91  Arcade Page
  REPORT_ID_GAMING_DEVICE,       // 0x92  Gaming Device Page (GSA-reserved)
  REPORT_ID_FIDO,                // 0xF1D0 FIDO Alliance Page
  REPORT_ID_COUNT                // sentinel == 36 (one past the last real ID)
};

// =====================================================================================
//  THE descriptor.  One contiguous array; the host parses it report-by-report keyed on
//  each Report ID (0x85, n) item.
// =====================================================================================
uint8_t const desc_hid_report[] =
{
  // ===================================================================================
  //  Report 1 : Generic Desktop Page (0x01)  --  Standard HID mouse.
  //  5 buttons + X/Y/Wheel/Pan (all 8-bit relative).  Formerly TUD_HID_REPORT_DESC_MOUSE
  //  macro; expanded inline so the code generator can parse every byte without macro
  //  special-casing.
  // ===================================================================================
  0x05, 0x01,        // Usage Page (Generic Desktop)              -- HUT 1, page 0x01
  0x09, 0x02,        // Usage (Mouse)                             -- HUT 1, ID 0x02 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_GENERIC_DESKTOP, // Report ID (1)
    0x09, 0x01,                 //   Usage (Pointer)               -- HUT 1, ID 0x01 (CP)
    0xA1, 0x00,                 //   Collection (Physical)
      0x05, 0x09,               //     Usage Page (Button)         -- HUT 12, page 0x09
      0x19, 0x01,               //     Usage Minimum (1)           -- Button 1
      0x29, 0x05,               //     Usage Maximum (5)           -- Button 5
      0x15, 0x00,               //     Logical Minimum (0)
      0x25, 0x01,               //     Logical Maximum (1)
      0x75, 0x01,               //     Report Size (1 bit)
      0x95, 0x05,               //     Report Count (5 buttons)
      0x81, 0x02,               //     Input (Data,Var,Abs)        -- 5 button bits
      0x95, 0x01,               //     Report Count (1)
      0x75, 0x03,               //     Report Size (3 bits)
      0x81, 0x03,               //     Input (Const,Var,Abs)       -- 3-bit pad
      0x05, 0x01,               //     Usage Page (Generic Desktop)
      0x09, 0x30,               //     Usage (X)                   -- HUT 1, ID 0x30 (DV)
      0x09, 0x31,               //     Usage (Y)                   -- HUT 1, ID 0x31 (DV)
      0x15, 0x81,               //     Logical Minimum (-127)
      0x25, 0x7F,               //     Logical Maximum (127)
      0x75, 0x08,               //     Report Size (8 bits)
      0x95, 0x02,               //     Report Count (2 axes)
      0x81, 0x06,               //     Input (Data,Var,Rel)        -- X, Y relative
      0x09, 0x38,               //     Usage (Wheel)               -- HUT 1, ID 0x38 (DV)
      0x15, 0x81,               //     Logical Minimum (-127)
      0x25, 0x7F,               //     Logical Maximum (127)
      0x75, 0x08,               //     Report Size (8 bits)
      0x95, 0x01,               //     Report Count (1)
      0x81, 0x06,               //     Input (Data,Var,Rel)        -- Wheel relative
      0x05, 0x0C,               //     Usage Page (Consumer)       -- HUT 15, page 0x0C
      0x0A, 0x38, 0x02,         //     Usage (AC Pan)              -- HUT 15, ID 0x0238
      0x15, 0x81,               //     Logical Minimum (-127)
      0x25, 0x7F,               //     Logical Maximum (127)
      0x75, 0x08,               //     Report Size (8 bits)
      0x95, 0x01,               //     Report Count (1)
      0x81, 0x06,               //     Input (Data,Var,Rel)        -- Pan relative
    0xC0,                       //   End Collection (Physical)
  0xC0,              // End Collection (Application)

  // ===================================================================================
  //  Report 2 : Simulation Controls Page (0x02)   [HUT section 5]
  //  Models a flight yoke: 5 analog control surfaces + a fire trigger.
  // ===================================================================================
  0x05, 0x02,        // Usage Page (Simulation Controls)            -- HUT 5, page 0x02
  0x09, 0x01,        // Usage (Flight Simulation Device)            -- HUT 5, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_SIMULATION, // Report ID (2)
    0x16, 0x00, 0x80,           //   Logical Minimum (-32768)        -- full signed 16-bit swing
    0x26, 0xFF, 0x7F,           //   Logical Maximum (32767)
    0x75, 0x10,                 //   Report Size (16 bits per axis)
    0x95, 0x05,                 //   Report Count (5 axes)
    0x09, 0xB0,                 //   Usage (Aileron)                 -- HUT 5, ID 0xB0 (DV)
    0x09, 0xB8,                 //   Usage (Elevator)                -- HUT 5, ID 0xB8 (DV)
    0x09, 0xBA,                 //   Usage (Rudder)                  -- HUT 5, ID 0xBA (DV)
    0x09, 0xBB,                 //   Usage (Throttle)                -- HUT 5, ID 0xBB (DV)
    0x09, 0xC3,                 //   Usage (Wing Flaps)              -- HUT 5, ID 0xC3 (DV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)            -- 5 x 16-bit = 10 bytes
    0x15, 0x00,                 //   Logical Minimum (0)             -- trigger button range
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x01,                 //   Report Count (1 button)
    0x09, 0xC0,                 //   Usage (Trigger)                 -- HUT 5, ID 0xC0 (MC)
    0x81, 0x02,                 //   Input (Data,Var,Abs)            -- fire trigger bit
    0x95, 0x07,                 //   Report Count (7)                -- pad remaining bits
    0x81, 0x03,                 //   Input (Const,Var,Abs)           -- 7-bit byte alignment pad
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 3 : VR Controls Page (0x03)            [HUT section 6]
  //  A head-mounted display: 3-axis head orientation (borrowed from Generic Desktop,
  //  which is how real VR HMDs report pose) + 2 device On/Off flags from the VR page.
  // ===================================================================================
  0x05, 0x03,        // Usage Page (VR Controls)                    -- HUT 6, page 0x03
  0x09, 0x06,        // Usage (Head Mounted Display)                -- HUT 6, ID 0x06 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_VR,         // Report ID (3)
    0x05, 0x01,                 //   Usage Page (Generic Desktop)    -- pose axes live on 0x01
    0x16, 0x00, 0x80,           //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,           //   Logical Maximum (32767)
    0x75, 0x10,                 //   Report Size (16 bits)
    0x95, 0x03,                 //   Report Count (3 rotation axes)
    0x09, 0x33,                 //   Usage (Rx)                      -- GD ID 0x33  (roll)
    0x09, 0x34,                 //   Usage (Ry)                      -- GD ID 0x34  (pitch)
    0x09, 0x35,                 //   Usage (Rz)                      -- GD ID 0x35  (yaw)
    0x81, 0x02,                 //   Input (Data,Var,Abs)            -- 3 x 16-bit = 6 bytes
    0x05, 0x03,                 //   Usage Page (VR Controls)        -- back to 0x03
    0x15, 0x00,                 //   Logical Minimum (0)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x02,                 //   Report Count (2 flags)
    0x09, 0x20,                 //   Usage (Stereo Enable)           -- HUT 6, ID 0x20 (OOC)
    0x09, 0x21,                 //   Usage (Display Enable)          -- HUT 6, ID 0x21 (OOC)
    0x81, 0x02,                 //   Input (Data,Var,Abs)            -- 2 control bits
    0x95, 0x06,                 //   Report Count (6)
    0x81, 0x03,                 //   Input (Const,Var,Abs)           -- 6-bit pad
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 4 : Sport Controls Page (0x04)         [HUT section 7]
  //  An instrumented golf club: 4 swing metrics as signed 16-bit values.
  // ===================================================================================
  0x05, 0x04,        // Usage Page (Sport Controls)                 -- HUT 7, page 0x04
  0x09, 0x02,        // Usage (Golf Club)                           -- HUT 7, ID 0x02 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_SPORT,      // Report ID (4)
    0x16, 0x00, 0x80,           //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,           //   Logical Maximum (32767)
    0x75, 0x10,                 //   Report Size (16 bits)
    0x95, 0x04,                 //   Report Count (4 metrics)
    0x09, 0x33,                 //   Usage (Stick Speed)             -- HUT 7, ID 0x33 (DV)
    0x09, 0x34,                 //   Usage (Stick Face Angle)        -- HUT 7, ID 0x34 (DV)
    0x09, 0x35,                 //   Usage (Stick Heel/Toe)          -- HUT 7, ID 0x35 (DV)
    0x09, 0x37,                 //   Usage (Stick Tempo)             -- HUT 7, ID 0x37 (DV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)            -- 4 x 16-bit = 8 bytes
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 5 : Game Controls Page (0x05)          [HUT section 8]
  //  A 3D game controller: 6 relative motion axes (8-bit signed deltas).
  // ===================================================================================
  0x05, 0x05,        // Usage Page (Game Controls)                  -- HUT 8, page 0x05
  0x09, 0x01,        // Usage (3D Game Controller)                  -- HUT 8, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_GAME,       // Report ID (5)
    0x15, 0x81,                 //   Logical Minimum (-127)
    0x25, 0x7F,                 //   Logical Maximum (127)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x06,                 //   Report Count (6 axes)
    0x09, 0x21,                 //   Usage (Turn Right/Left)         -- HUT 8, ID 0x21 (DV)
    0x09, 0x22,                 //   Usage (Pitch Forward/Backward)  -- HUT 8, ID 0x22 (DV)
    0x09, 0x23,                 //   Usage (Roll Right/Left)         -- HUT 8, ID 0x23 (DV)
    0x09, 0x24,                 //   Usage (Move Right/Left)         -- HUT 8, ID 0x24 (DV)
    0x09, 0x25,                 //   Usage (Move Forward/Backward)   -- HUT 8, ID 0x25 (DV)
    0x09, 0x26,                 //   Usage (Move Up/Down)            -- HUT 8, ID 0x26 (DV)
    0x81, 0x06,                 //   Input (Data,Var,Rel)            -- relative deltas, 6 bytes
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 6 : Generic Device Controls Page (0x06)   [HUT section 9]
  //  Background/maintenance telemetry common to wireless peripherals.
  // ===================================================================================
  0x05, 0x06,        // Usage Page (Generic Device Controls)        -- HUT 9, page 0x06
  0x09, 0x01,        // Usage (Background Nonuser Controls)         -- HUT 9, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_GENERIC_DEVICE, // Report ID (6)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x01,                 //   Report Count (1) per usage below
    0x09, 0x20,                 //   Usage (Battery Strength)        -- HUT 9, ID 0x20 (DV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)            -- 0..255 charge level
    0x09, 0x21,                 //   Usage (Wireless Channel)        -- HUT 9, ID 0x21 (DV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)            -- current RF channel
    0x09, 0x29,                 //   Usage (RF Signal Strength)      -- HUT 9, ID 0x29 (DV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)            -- RSSI byte
    0x09, 0x27,                 //   Usage (Sequence ID)             -- HUT 9, ID 0x27 (DV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)            -- rolling packet counter
    0x09, 0x22,                 //   Usage (Wireless ID)             -- HUT 9, ID 0x22 (DV)
    0x27, 0xFF, 0xFF, 0xFF, 0x7F, // Logical Maximum (2147483647)    -- 32-bit unique ID
    0x75, 0x20,                 //   Report Size (32 bits)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)          -- 4-byte pairing/ID datum
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 7 : Keyboard/Keypad Page (0x07)  --  Standard HID boot keyboard.
  //  8 modifier bits + reserved byte + 6-key rollover array + 5 LED output bits.
  //  Formerly TUD_HID_REPORT_DESC_KEYBOARD macro; expanded inline so the code
  //  generator can parse every byte without macro special-casing.
  // ===================================================================================
  0x05, 0x01,        // Usage Page (Generic Desktop)              -- HUT 1, page 0x01
  0x09, 0x06,        // Usage (Keyboard)                          -- HUT 1, ID 0x06 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_KEYBOARD,  // Report ID (7)
    0x05, 0x07,                 //   Usage Page (Keyboard/Keypad)  -- HUT 10, page 0x07
    0x19, 0xE0,                 //   Usage Minimum (224)           -- Left Control
    0x29, 0xE7,                 //   Usage Maximum (231)           -- Right GUI
    0x15, 0x00,                 //   Logical Minimum (0)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x08,                 //   Report Count (8 modifiers)
    0x81, 0x02,                 //   Input (Data,Var,Abs)          -- 8 modifier bits
    0x95, 0x01,                 //   Report Count (1)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x81, 0x03,                 //   Input (Const,Var,Abs)         -- reserved byte
    0x05, 0x08,                 //   Usage Page (LED)              -- HUT 11, page 0x08
    0x19, 0x01,                 //   Usage Minimum (1)             -- Num Lock
    0x29, 0x05,                 //   Usage Maximum (5)             -- Kana
    0x15, 0x00,                 //   Logical Minimum (0)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x05,                 //   Report Count (5 LEDs)
    0x91, 0x02,                 //   Output (Data,Var,Abs)         -- 5 LED bits
    0x95, 0x01,                 //   Report Count (1)
    0x75, 0x03,                 //   Report Size (3 bits)
    0x91, 0x03,                 //   Output (Const,Var,Abs)        -- 3-bit pad
    0x05, 0x07,                 //   Usage Page (Keyboard/Keypad)  -- HUT 10, page 0x07
    0x19, 0x00,                 //   Usage Minimum (0)
    0x2A, 0xFF, 0x00,           //   Usage Maximum (255)           -- 2-byte form
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)         -- 2-byte form
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x06,                 //   Report Count (6 key slots)
    0x81, 0x00,                 //   Input (Data,Array,Abs)        -- 6-key rollover array
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 8 : LED Page (0x08)                    [HUT section 11]
  //  The LED page defines no Application usage (all entries are On/Off indicators), so
  //  we host an OUTPUT report under a representative usage (Generic Indicator 0x4B).
  //  5 simple indicator bits + an RGB LED logical collection (3 x 8-bit channels).
  // ===================================================================================
  0x05, 0x08,        // Usage Page (LED)                            -- HUT 11, page 0x08
  0x09, 0x4B,        // Usage (Generic Indicator)                   -- HUT 11, ID 0x4B (OOC)
  0xA1, 0x01,        // Collection (Application)  (page has no CA; representative usage)
    0x85, REPORT_ID_LED,        // Report ID (8)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x05,                 //   Report Count (5 indicators)
    0x09, 0x01,                 //   Usage (Num Lock)               -- HUT 11, ID 0x01 (OOC)
    0x09, 0x02,                 //   Usage (Caps Lock)              -- HUT 11, ID 0x02 (OOC)
    0x09, 0x03,                 //   Usage (Scroll Lock)            -- HUT 11, ID 0x03 (OOC)
    0x09, 0x09,                 //   Usage (Mute)                   -- HUT 11, ID 0x09 (OOC)
    0x09, 0x4B,                 //   Usage (Generic Indicator)      -- HUT 11, ID 0x4B (OOC)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- host drives 5 LEDs
    0x95, 0x03,                 //   Report Count (3)
    0x91, 0x03,                 //   Output (Const,Var,Abs)         -- 3-bit pad to a byte
    0x09, 0x52,                 //   Usage (RGB LED)                -- HUT 11.7, ID 0x52 (CL)
    0xA1, 0x02,                 //   Collection (Logical)
      0x26, 0xFF, 0x00,         //     Logical Maximum (255)        -- 8-bit channel intensity
      0x75, 0x08,               //     Report Size (8 bits)
      0x95, 0x03,               //     Report Count (3 channels)
      0x09, 0x53,               //     Usage (Red LED Channel)      -- HUT 11.7, ID 0x53 (DV)
      0x09, 0x55,               //     Usage (Green LED Channel)    -- HUT 11.7, ID 0x55 (DV)
      0x09, 0x54,               //     Usage (Blue LED Channel)     -- HUT 11.7, ID 0x54 (DV)
      0x91, 0x02,               //     Output (Data,Var,Abs)        -- R,G,B = 3 bytes
    0xC0,                       //   End Collection (Logical)
  0xC0,              // End Collection (Application)

  // ===================================================================================
  //  Report 9 : Button Page (0x09)                 [HUT section 12]
  //  The Button page is normally nested, but here it stands alone: 8 push buttons.
  // ===================================================================================
  0x05, 0x09,        // Usage Page (Button)                         -- HUT 12, page 0x09
  0x09, 0x01,        // Usage (Button 1)                            -- HUT 12, ID 0x01
  0xA1, 0x01,        // Collection (Application)  (page has no CA; representative usage)
    0x85, REPORT_ID_BUTTON,     // Report ID (9)
    0x19, 0x01,                 //   Usage Minimum (Button 1)        -- HUT 12, ID 0x01
    0x29, 0x08,                 //   Usage Maximum (Button 8)        -- HUT 12, ID 0x08
    0x15, 0x00,                 //   Logical Minimum (0)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x08,                 //   Report Count (8 buttons)
    0x81, 0x02,                 //   Input (Data,Var,Abs)            -- 8 button bits = 1 byte
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 10 : Ordinal Page (0x0A)               [HUT section 13]
  //  Ordinal usages are Usage Modifiers (UM) that index identical sibling controls.
  //  Illustratively we report 4 byte-wide values, each tagged with Instance 1..4, as
  //  would label e.g. four identical analog inputs.
  // ===================================================================================
  0x05, 0x0A,        // Usage Page (Ordinal)                        -- HUT 13, page 0x0A
  0x09, 0x01,        // Usage (Instance 1)                          -- HUT 13, ID 0x01 (UM)
  0xA1, 0x01,        // Collection (Application)  (page has no CA; representative usage)
    0x85, REPORT_ID_ORDINAL,    // Report ID (10)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x01,                 //   Report Count (1) per instance usage
    0x09, 0x01,                 //   Usage (Instance 1)             -- HUT 13, ID 0x01
    0x09, 0x02,                 //   Usage (Instance 2)             -- HUT 13, ID 0x02
    0x09, 0x03,                 //   Usage (Instance 3)             -- HUT 13, ID 0x03
    0x09, 0x04,                 //   Usage (Instance 4)             -- HUT 13, ID 0x04
    0x95, 0x04,                 //   Report Count (4 instances)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- 4 indexed bytes
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 11 : Telephony Device Page (0x0B)      [HUT section 14]
  //  A USB phone: hook/flash/hold/mute/send control bits, a 12-key dial pad array,
  //  and three call-status LED outputs (drawn from the LED page).
  // ===================================================================================
  0x05, 0x0B,        // Usage Page (Telephony Device)               -- HUT 14, page 0x0B
  0x09, 0x01,        // Usage (Phone)                               -- HUT 14, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_TELEPHONY,  // Report ID (11)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x05,                 //   Report Count (5 call controls)
    0x09, 0x20,                 //   Usage (Hook Switch)            -- HUT 14, ID 0x20 (OOC)
    0x09, 0x21,                 //   Usage (Flash)                  -- HUT 14, ID 0x21 (MC)
    0x09, 0x23,                 //   Usage (Hold)                   -- HUT 14, ID 0x23 (OOC)
    0x09, 0x2F,                 //   Usage (Phone Mute)             -- HUT 14, ID 0x2F (OOC)
    0x09, 0x31,                 //   Usage (Send)                   -- HUT 14, ID 0x31 (OOC)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- 5 control bits
    0x95, 0x03,                 //   Report Count (3)
    0x81, 0x03,                 //   Input (Const,Var,Abs)          -- 3-bit pad
    0x15, 0x00,                 //   Logical Minimum (0)
    0x25, 0x0C,                 //   Logical Maximum (12)           -- 0=idle, 1..12 = key index
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x01,                 //   Report Count (1 keypress)
    0x19, 0xB0,                 //   Usage Minimum (Phone Key 0)    -- HUT 14, ID 0xB0
    0x29, 0xBB,                 //   Usage Maximum (Phone Key Pound)-- HUT 14, ID 0xBB (0..9,*,#)
    0x81, 0x00,                 //   Input (Data,Array,Abs)         -- selector: which key
    0x05, 0x08,                 //   Usage Page (LED)               -- status lamps on page 0x08
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x03,                 //   Report Count (3 lamps)
    0x09, 0x17,                 //   Usage (Off-Hook)               -- HUT 11, ID 0x17 (OOC)
    0x09, 0x18,                 //   Usage (Ring)                   -- HUT 11, ID 0x18 (OOC)
    0x09, 0x19,                 //   Usage (Message Waiting)        -- HUT 11, ID 0x19 (OOC)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- 3 status LEDs
    0x95, 0x05,                 //   Report Count (5)
    0x91, 0x03,                 //   Output (Const,Var,Abs)         -- 5-bit pad
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 12 : Consumer Page (0x0C)  --  Standard HID consumer control.
  //  Single 16-bit array selector covering Usage 0x0000..0x03FF.  Formerly
  //  TUD_HID_REPORT_DESC_CONSUMER macro; expanded inline so the code generator can
  //  parse every byte without macro special-casing.
  // ===================================================================================
  0x05, 0x0C,        // Usage Page (Consumer)                     -- HUT 15, page 0x0C
  0x09, 0x01,        // Usage (Consumer Control)                  -- HUT 15, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_CONSUMER,  // Report ID (12)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x03,           //   Logical Maximum (1023)       -- 2-byte form
    0x19, 0x00,                 //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,           //   Usage Maximum (1023)         -- 2-byte form
    0x75, 0x10,                 //   Report Size (16 bits)
    0x95, 0x01,                 //   Report Count (1)
    0x81, 0x00,                 //   Input (Data,Array,Abs)        -- consumer usage selector
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 13 : Digitizers Page (0x0D)            [HUT section 16]
  //  A single-finger touchscreen contact: Tip Switch + In Range flags, contact id,
  //  X/Y (Generic Desktop), tip pressure, plus a contact-count and a feature reporting
  //  the maximum simultaneous contacts.
  // ===================================================================================
  0x05, 0x0D,        // Usage Page (Digitizers)                     -- HUT 16, page 0x0D
  0x09, 0x04,        // Usage (Touch Screen)                        -- HUT 16, ID 0x04 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_DIGITIZER,  // Report ID (13)
    0x09, 0x22,                 //   Usage (Finger)                 -- HUT 16, ID 0x22 (CL)
    0xA1, 0x02,                 //   Collection (Logical)
      0x09, 0x42,               //     Usage (Tip Switch)           -- HUT 16, ID 0x42 (MC)
      0x09, 0x32,               //     Usage (In Range)             -- HUT 16, ID 0x32 (MC)
      0x15, 0x00,               //     Logical Minimum (0)
      0x25, 0x01,               //     Logical Maximum (1)
      0x75, 0x01,               //     Report Size (1 bit)
      0x95, 0x02,               //     Report Count (2 flags)
      0x81, 0x02,               //     Input (Data,Var,Abs)         -- tip + range bits
      0x95, 0x06,               //     Report Count (6)
      0x81, 0x03,               //     Input (Const,Var,Abs)        -- 6-bit pad
      0x09, 0x51,               //     Usage (Contact Identifier)   -- HUT 16, ID 0x51 (DV)
      0x26, 0xFF, 0x00,         //     Logical Maximum (255)
      0x75, 0x08,               //     Report Size (8 bits)
      0x95, 0x01,               //     Report Count (1)
      0x81, 0x02,               //     Input (Data,Var,Abs)         -- contact id byte
      0x05, 0x01,               //     Usage Page (Generic Desktop) -- X/Y come from 0x01
      0x15, 0x00,               //     Logical Minimum (0)
      0x26, 0xFF, 0x7F,         //     Logical Maximum (32767)      -- normalized surface coords
      0x75, 0x10,               //     Report Size (16 bits)
      0x09, 0x30,               //     Usage (X)                    -- GD ID 0x30
      0x81, 0x02,               //     Input (Data,Var,Abs)         -- X position
      0x09, 0x31,               //     Usage (Y)                    -- GD ID 0x31
      0x81, 0x02,               //     Input (Data,Var,Abs)         -- Y position
      0x05, 0x0D,               //     Usage Page (Digitizers)      -- back to 0x0D
      0x09, 0x30,               //     Usage (Tip Pressure)         -- HUT 16, ID 0x30 (DV)
      0x81, 0x02,               //     Input (Data,Var,Abs)         -- 16-bit pressure
    0xC0,                       //   End Collection (Logical)
    0x09, 0x54,                 //   Usage (Contact Count)          -- HUT 16, ID 0x54 (DV)
    0x25, 0x7F,                 //   Logical Maximum (127)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x01,                 //   Report Count (1)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- # contacts this report
    0x09, 0x55,                 //   Usage (Contact Count Maximum)  -- HUT 16, ID 0x55 (SV)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- max supported contacts
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 14 : Haptics Page (0x0E)               [HUT section 17]
  //  A Simple Haptic Controller: host writes a waveform ordinal + intensity (Output),
  //  and the device advertises its supported waveform/duration lists (Feature).
  // ===================================================================================
  0x05, 0x0E,        // Usage Page (Haptics)                        -- HUT 17, page 0x0E
  0x09, 0x01,        // Usage (Simple Haptic Controller)            -- HUT 17, ID 0x01 (CA/CL)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_HAPTICS,    // Report ID (14)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x01,                 //   Report Count (1)
    0x09, 0x21,                 //   Usage (Manual Trigger)         -- HUT 17, ID 0x21 (DV)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- waveform ordinal to play
    0x09, 0x23,                 //   Usage (Intensity)              -- HUT 17, ID 0x23 (DV)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- 0..255 strength
    0x09, 0x10,                 //   Usage (Waveform List)          -- HUT 17, ID 0x10 (NAry)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- supported-waveform ordinal
    0x09, 0x11,                 //   Usage (Duration List)          -- HUT 17, ID 0x11 (NAry)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- supported-duration ordinal
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 15 : Physical Input Device Page / PID (0x0F)   [HUT section 18]
  //  A force-feedback "Set Effect" Output report: an effect-type selector array plus a
  //  16-bit duration and an 8-bit gain.
  // ===================================================================================
  0x05, 0x0F,        // Usage Page (Physical Input Device)          -- HUT 18, page 0x0F
  0x09, 0x01,        // Usage (Physical Input Device)               -- HUT 18, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_PID,        // Report ID (15)
    0x09, 0x25,                 //   Usage (Effect Type)            -- HUT 18, ID 0x25 (NAry)
    0xA1, 0x02,                 //   Collection (Logical)           -- the named-array body
      0x15, 0x01,               //     Logical Minimum (1)
      0x25, 0x03,               //     Logical Maximum (3)          -- 3 selectable effects
      0x75, 0x08,               //     Report Size (8 bits)
      0x95, 0x01,               //     Report Count (1)
      0x09, 0x26,               //     Usage (ET Constant Force)    -- HUT 18, ID 0x26 (Sel)
      0x09, 0x31,               //     Usage (ET Sine)              -- HUT 18, ID 0x31 (Sel)
      0x09, 0x40,               //     Usage (ET Spring)            -- HUT 18, ID 0x40 (Sel)
      0x91, 0x00,               //     Output (Data,Array,Abs)      -- chosen effect type
    0xC0,                       //   End Collection (Logical)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x7F,           //   Logical Maximum (32767)
    0x75, 0x10,                 //   Report Size (16 bits)
    0x95, 0x01,                 //   Report Count (1)
    0x09, 0x50,                 //   Usage (Duration)               -- HUT 18, ID 0x50 (DV)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- effect duration (ms)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x09, 0x52,                 //   Usage (Gain)                   -- HUT 18, ID 0x52 (DV)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- effect gain 0..255
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 16 : Unicode Page (0x10)               [HUT section 19]
  //  The Unicode page assigns NO named usages; instead every Usage ID *is* a UTF-16
  //  (UCS-2) code point.  So we declare a 6-slot array over the whole code-point range
  //  0x0000..0xFFFF -- a "Unicode keyboard" analogous to the keyboard key array but in
  //  code-point space.  The collection is nominally tagged with U+0041 ('A').
  // ===================================================================================
  0x05, 0x10,        // Usage Page (Unicode)                        -- HUT 19, page 0x10
  0x0A, 0x41, 0x00,  // Usage (U+0041 'A', nominal)                 -- code point as Usage ID
  0xA1, 0x01,        // Collection (Application)  (page defines no CA; code-point usage)
    0x85, REPORT_ID_UNICODE,    // Report ID (16)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, // Logical Maximum (65535)        -- full UCS-2 range
    0x75, 0x10,                 //   Report Size (16 bits per char)
    0x95, 0x06,                 //   Report Count (6 chars)
    0x1A, 0x00, 0x00,           //   Usage Minimum (U+0000)         -- code point low bound
    0x2A, 0xFF, 0xFF,           //   Usage Maximum (U+FFFF)         -- code point high bound
    0x81, 0x00,                 //   Input (Data,Array,Abs)         -- up to 6 Unicode chars
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 17 : SoC Page (0x11)                   [HUT section 20]
  //  System-on-Chip firmware transfer.  A Feature report carrying a file id, a 32-bit
  //  byte offset, the chunk size, a 32-byte payload, and a "last chunk" flag.
  // ===================================================================================
  0x05, 0x11,        // Usage Page (SoC)                            -- HUT 20, page 0x11
  0x09, 0x01,        // Usage (SocControl)                          -- HUT 20, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_SOC,        // Report ID (17)
    0x09, 0x03,                 //   Usage (FirmwareFileId)         -- HUT 20, ID 0x03 (DV)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, // Logical Maximum (65535)
    0x75, 0x10,                 //   Report Size (16 bits)
    0x95, 0x01,                 //   Report Count (1)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- which firmware file
    0x09, 0x04,                 //   Usage (FileOffsetInBytes)      -- HUT 20, ID 0x04 (DV)
    0x27, 0xFF, 0xFF, 0xFF, 0x7F, // Logical Maximum (2147483647)
    0x75, 0x20,                 //   Report Size (32 bits)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- offset of this chunk
    0x09, 0x07,                 //   Usage (FilePayloadSizeInBytes) -- HUT 20, ID 0x07 (DV)
    0x27, 0xFF, 0xFF, 0x00, 0x00, // Logical Maximum (65535)
    0x75, 0x10,                 //   Report Size (16 bits)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- valid bytes in payload
    0x09, 0x06,                 //   Usage (FilePayload)            -- HUT 20, ID 0x06 (DV)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x20,                 //   Report Count (32 payload bytes)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- firmware chunk data
    0x09, 0x08,                 //   Usage (FilePayloadContainsLastBytes) -- HUT 20, ID 0x08 (DF)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x01,                 //   Report Count (1)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- final-chunk flag
    0x95, 0x07,                 //   Report Count (7)
    0xB1, 0x03,                 //   Feature (Const,Var,Abs)        -- 7-bit pad
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 18 : Eye and Head Trackers Page (0x12)   [HUT section 21]
  //  An eye tracker: 32-bit sensor timestamp plus a Gaze Point physical collection
  //  holding signed 16-bit X/Y gaze coordinates.
  // ===================================================================================
  0x05, 0x12,        // Usage Page (Eye and Head Trackers)          -- HUT 21, page 0x12
  0x09, 0x01,        // Usage (Eye Tracker)                         -- HUT 21, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_EYE_HEAD_TRACKER, // Report ID (18)
    0x09, 0x20,                 //   Usage (Sensor Timestamp)       -- HUT 21, ID 0x20 (DV)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0xFF, 0x7F, // Logical Maximum (2147483647)
    0x75, 0x20,                 //   Report Size (32 bits)
    0x95, 0x01,                 //   Report Count (1)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- microsecond timestamp
    0x09, 0x24,                 //   Usage (Gaze Point)             -- HUT 21, ID 0x24 (CP)
    0xA1, 0x00,                 //   Collection (Physical)
      0x16, 0x00, 0x80,         //     Logical Minimum (-32768)
      0x26, 0xFF, 0x7F,         //     Logical Maximum (32767)
      0x75, 0x10,               //     Report Size (16 bits)
      0x09, 0x21,               //     Usage (Position X)           -- HUT 21, ID 0x21 (DV)
      0x81, 0x02,               //     Input (Data,Var,Abs)         -- gaze X
      0x09, 0x22,               //     Usage (Position Y)           -- HUT 21, ID 0x22 (DV)
      0x81, 0x02,               //     Input (Data,Var,Abs)         -- gaze Y
    0xC0,                       //   End Collection (Physical)
  0xC0,              // End Collection (Application)

  // ===================================================================================
  //  Report 19 : Auxiliary Display Page (0x14)     [HUT section 22]
  //  A 16-character alphanumeric display: Output character buffer + Feature brightness
  //  and contrast controls.
  // ===================================================================================
  0x05, 0x14,        // Usage Page (Auxiliary Display)              -- HUT 22, page 0x14
  0x09, 0x01,        // Usage (Alphanumeric Display)                -- HUT 22, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_AUX_DISPLAY, // Report ID (19)
    0x09, 0x2C,                 //   Usage (Display Data)           -- HUT 22, ID 0x2C (DV)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)          -- one char code per byte
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x10,                 //   Report Count (16 characters)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- 16-byte text buffer
    0x09, 0x46,                 //   Usage (Display Brightness)     -- HUT 22, ID 0x46 (DV)
    0x25, 0x64,                 //   Logical Maximum (100)          -- percent
    0x95, 0x01,                 //   Report Count (1)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- backlight brightness %
    0x09, 0x47,                 //   Usage (Display Contrast)       -- HUT 22, ID 0x47 (DV)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- contrast %
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 20 : Sensors Page (0x20)               [HUT section 23]
  //  A 3-axis accelerometer (HUT 23.1 "Motion: Accelerometer 3D", ID 0x73).  Note the
  //  Sensors page uses 16-bit Usage IDs, so every Usage item uses the 0x0A prefix.
  // ===================================================================================
  0x05, 0x20,        // Usage Page (Sensors)                        -- HUT 23, page 0x20
  0x09, 0x73,        // Usage (Motion: Accelerometer 3D)            -- HUT 23.1, ID 0x73 (CA/CP)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_SENSOR,     // Report ID (20)
    0x0A, 0x0E, 0x03,           //   Usage (Property: Report Interval) -- HUT 23.5, ID 0x030E (DV)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0xFF, 0x7F, // Logical Maximum (2147483647)
    0x75, 0x20,                 //   Report Size (32 bits)
    0x95, 0x01,                 //   Report Count (1)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- sample interval (ms)
    0x0A, 0x01, 0x02,           //   Usage (Event: Sensor State)    -- HUT 23.3, ID 0x0201 (NAry)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- sensor state enum
    0x16, 0x00, 0x80,           //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,           //   Logical Maximum (32767)        -- signed milli-g
    0x75, 0x10,                 //   Report Size (16 bits)
    0x0A, 0x53, 0x04,           //   Usage (Data Field: Accel Axis X) -- HUT 23.12, ID 0x0453 (SV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- acceleration X
    0x0A, 0x54, 0x04,           //   Usage (Data Field: Accel Axis Y) -- HUT 23.12, ID 0x0454 (SV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- acceleration Y
    0x0A, 0x55, 0x04,           //   Usage (Data Field: Accel Axis Z) -- HUT 23.12, ID 0x0455 (SV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- acceleration Z
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 21 : Medical Instrument Page (0x40)    [HUT section 24]
  //  An ultrasound scanner control surface: freeze/acquire toggles + analog
  //  depth / focus / transmit-power / cine sliders.
  // ===================================================================================
  0x05, 0x40,        // Usage Page (Medical Instrument)             -- HUT 24, page 0x40
  0x09, 0x01,        // Usage (Medical Ultrasound)                  -- HUT 24, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_MEDICAL,    // Report ID (21)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x02,                 //   Report Count (2 toggles)
    0x09, 0x20,                 //   Usage (VCR/Acquisition)        -- HUT 24, ID 0x20 (OOC)
    0x09, 0x21,                 //   Usage (Freeze/Thaw)            -- HUT 24, ID 0x21 (OOC)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- acquire + freeze bits
    0x95, 0x06,                 //   Report Count (6)
    0x81, 0x03,                 //   Input (Const,Var,Abs)          -- 6-bit pad
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x01,                 //   Report Count (1) per slider
    0x09, 0x44,                 //   Usage (Depth)                  -- HUT 24, ID 0x44 (LC)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- scan depth
    0x09, 0x43,                 //   Usage (Focus)                  -- HUT 24, ID 0x43 (LC)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- focal depth
    0x09, 0x41,                 //   Usage (Transmit Power)         -- HUT 24, ID 0x41 (LC)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- acoustic power
    0x09, 0x40,                 //   Usage (Cine)                   -- HUT 24, ID 0x40 (LC)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- cine-loop scrub
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 22 : Braille Display Page (0x41)       [HUT section 25]
  //  An 8-cell refreshable braille line: Output cells (8 dots each), Input keyboard
  //  dots, and a Feature reporting the number of cells.
  // ===================================================================================
  0x05, 0x41,        // Usage Page (Braille Display)                -- HUT 25, page 0x41
  0x09, 0x01,        // Usage (Braille Display)                     -- HUT 25, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_BRAILLE,    // Report ID (22)
    0x09, 0x03,                 //   Usage (8 Dot Braille Cell)     -- HUT 25, ID 0x03 (DV)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)          -- 8 dots = one bitmap byte
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x08,                 //   Report Count (8 cells)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- 8-cell braille line
    0x15, 0x00,                 //   Logical Minimum (0)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x08,                 //   Report Count (8 chord keys)
    0x1A, 0x01, 0x02,           //   Usage Minimum (Braille Keyboard Dot 1) -- HUT 25, ID 0x0201
    0x2A, 0x08, 0x02,           //   Usage Maximum (Braille Keyboard Dot 8) -- HUT 25, ID 0x0208
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- Perkins chord input
    0x09, 0x05,                 //   Usage (Number of Braille Cells)-- HUT 25, ID 0x05 (DV)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x01,                 //   Report Count (1)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- cells advertised to host
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 23 : Lighting And Illumination Page (0x59)   [HUT section 26]
  //  A LampArray (RGB lighting).  Feature: array attributes (count + bounding box).
  //  Output: a multi-update record carrying a LampId and an RGBI tuple.
  // ===================================================================================
  0x05, 0x59,        // Usage Page (Lighting And Illumination)      -- HUT 26, page 0x59
  0x09, 0x01,        // Usage (LampArray)                           -- HUT 26, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_LIGHTING,   // Report ID (23)
    0x09, 0x02,                 //   Usage (LampArrayAttributesReport) -- HUT 26, ID 0x02 (CL)
    0xA1, 0x02,                 //   Collection (Logical)
      0x09, 0x03,               //     Usage (LampCount)            -- HUT 26, ID 0x03 (SV/DV)
      0x15, 0x00,               //     Logical Minimum (0)
      0x27, 0xFF, 0xFF, 0x00, 0x00, // Logical Maximum (65535)
      0x75, 0x10,               //     Report Size (16 bits)
      0x95, 0x01,               //     Report Count (1)
      0xB1, 0x02,               //     Feature (Data,Var,Abs)       -- number of lamps
      0x09, 0x04,               //     Usage (BoundingBoxWidthInMicrometers)  -- HUT 26, ID 0x04
      0x09, 0x05,               //     Usage (BoundingBoxHeightInMicrometers) -- HUT 26, ID 0x05
      0x09, 0x06,               //     Usage (BoundingBoxDepthInMicrometers)  -- HUT 26, ID 0x06
      0x27, 0xFF, 0xFF, 0xFF, 0x7F, // Logical Maximum (2147483647)
      0x75, 0x20,               //     Report Size (32 bits)
      0x95, 0x03,               //     Report Count (3 dimensions)
      0xB1, 0x02,               //     Feature (Data,Var,Abs)       -- W,H,D of the array (um)
    0xC0,                       //   End Collection (Logical)
    0x09, 0x50,                 //   Usage (LampMultiUpdateReport)  -- HUT 26, ID 0x50 (CL)
    0xA1, 0x02,                 //   Collection (Logical)
      0x09, 0x21,               //     Usage (LampId)               -- HUT 26, ID 0x21 (SV/DV)
      0x27, 0xFF, 0xFF, 0x00, 0x00, // Logical Maximum (65535)
      0x75, 0x10,               //     Report Size (16 bits)
      0x95, 0x01,               //     Report Count (1)
      0x91, 0x02,               //     Output (Data,Var,Abs)        -- which lamp to set
      0x09, 0x51,               //     Usage (RedUpdateChannel)     -- HUT 26, ID 0x51 (DV)
      0x09, 0x52,               //     Usage (GreenUpdateChannel)   -- HUT 26, ID 0x52 (DV)
      0x09, 0x53,               //     Usage (BlueUpdateChannel)    -- HUT 26, ID 0x53 (DV)
      0x09, 0x54,               //     Usage (IntensityUpdateChannel) -- HUT 26, ID 0x54 (DV)
      0x26, 0xFF, 0x00,         //     Logical Maximum (255)
      0x75, 0x08,               //     Report Size (8 bits)
      0x95, 0x04,               //     Report Count (4 channels)
      0x91, 0x02,               //     Output (Data,Var,Abs)        -- R,G,B,Intensity
    0xC0,                       //   End Collection (Logical)
  0xC0,              // End Collection (Application)

  // ===================================================================================
  //  Report 24 : Monitor Page (0x80)               [HUT section 27]
  //  A monitor reporting its identity: 128-byte EDID block + VESA DDC/CI version,
  //  delivered via Feature reports.
  //  *** OVERSIZE: this EDID Feature is 130 B, over the 63 B usable payload of the
  //  default 64 B HID buffer -- truncated/rejected until the buffer is enlarged. ***
  // ===================================================================================
  0x05, 0x80,        // Usage Page (Monitor)                        -- HUT 27, page 0x80
  0x09, 0x01,        // Usage (Monitor Control)                     -- HUT 27, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_MONITOR,    // Report ID (24)
    0x09, 0x02,                 //   Usage (EDID Information)        -- HUT 27, ID 0x02 (SV)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x80,                 //   Report Count (128 EDID bytes)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- raw EDID block
    0x09, 0x04,                 //   Usage (VESA Version)           -- HUT 27, ID 0x04 (SV)
    0x27, 0xFF, 0xFF, 0x00, 0x00, // Logical Maximum (65535)
    0x75, 0x10,                 //   Report Size (16 bits)
    0x95, 0x01,                 //   Report Count (1)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- DDC/CI version word
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 25 : Monitor Enumerated Page (0x81)    [HUT section 28]
  //  This page holds only enumerated "Enum N" selector values consumed by Monitor /
  //  VESA controls.  We host a Feature array selector (Enum 1..16) under a Monitor
  //  Control application collection (page 0x80), with the selector field drawn from
  //  page 0x81 -- exactly how an enumerated control is built.
  // ===================================================================================
  0x05, 0x80,        // Usage Page (Monitor)                        -- parent app collection
  0x09, 0x01,        // Usage (Monitor Control)                     -- HUT 27, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_MONITOR_ENUM, // Report ID (25)
    0x05, 0x81,                 //   Usage Page (Monitor Enumerated)-- HUT 28, page 0x81
    0x15, 0x01,                 //   Logical Minimum (1)
    0x25, 0x10,                 //   Logical Maximum (16)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x01,                 //   Report Count (1)
    0x19, 0x01,                 //   Usage Minimum (Enum 1)         -- HUT 28, ID 0x01 (Sel)
    0x29, 0x10,                 //   Usage Maximum (Enum 16)        -- HUT 28, ID 0x10 (Sel)
    0xB1, 0x00,                 //   Feature (Data,Array,Abs)       -- chosen enumerated value
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 26 : VESA Virtual Controls Page (0x82)   [HUT section 29]
  //  Monitor adjustment controls (DDC/CI VCP codes): brightness, contrast and per-color
  //  video gains as 16-bit Feature values, plus a momentary Degauss Output.  Hosted in
  //  a Monitor Control application collection per convention.
  // ===================================================================================
  0x05, 0x80,        // Usage Page (Monitor)                        -- parent app collection
  0x09, 0x01,        // Usage (Monitor Control)                     -- HUT 27, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_VESA_VC,    // Report ID (26)
    0x05, 0x82,                 //   Usage Page (VESA Virtual Controls) -- HUT 29, page 0x82
    0x15, 0x00,                 //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, // Logical Maximum (65535)
    0x75, 0x10,                 //   Report Size (16 bits)
    0x95, 0x05,                 //   Report Count (5 controls)
    0x09, 0x10,                 //   Usage (Brightness)             -- HUT 29, ID 0x10 (DV)
    0x09, 0x12,                 //   Usage (Contrast)               -- HUT 29, ID 0x12 (DV)
    0x09, 0x16,                 //   Usage (Red Video Gain)         -- HUT 29, ID 0x16 (DV)
    0x09, 0x18,                 //   Usage (Green Video Gain)       -- HUT 29, ID 0x18 (DV)
    0x09, 0x1A,                 //   Usage (Blue Video Gain)        -- HUT 29, ID 0x1A (DV)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- 5 x 16-bit VCP values
    0x09, 0x01,                 //   Usage (Degauss)                -- HUT 29, ID 0x01 (DV)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x01,                 //   Report Count (1)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- one-shot degauss
    0x95, 0x07,                 //   Report Count (7)
    0x91, 0x03,                 //   Output (Const,Var,Abs)         -- 7-bit pad
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 27 : Power Page (0x84)                 [HUT section 30]
  //  A UPS: live electrical measures (Input) + on/off switch controls (Output).
  //  *** OS POWER-BINDING WARNING ***  A host (esp. Windows) may bind this as a system
  //  UPS. If the firmware streams a critical / on-battery / 0% state (or sends garbage
  //  at startup), the OS can pop a battery icon and even move toward sleep/shutdown.
  //  Send safe values (100% / AC present) ASAP, or don't transmit this report ID while
  //  testing.
  // ===================================================================================
  0x05, 0x84,        // Usage Page (Power)                          -- HUT 30, page 0x84
  0x09, 0x04,        // Usage (UPS)                                 -- HUT 30, ID 0x04 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_POWER,      // Report ID (27)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, // Logical Maximum (65535)
    0x75, 0x10,                 //   Report Size (16 bits)
    0x95, 0x04,                 //   Report Count (4 measures)
    0x09, 0x30,                 //   Usage (Voltage)               -- HUT 30, ID 0x30 (DV)
    0x09, 0x31,                 //   Usage (Current)               -- HUT 30, ID 0x31 (DV)
    0x09, 0x32,                 //   Usage (Frequency)             -- HUT 30, ID 0x32 (DV)
    0x09, 0x35,                 //   Usage (Percent Load)          -- HUT 30, ID 0x35 (DV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)          -- V, I, Hz, %load
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x02,                 //   Report Count (2 switches)
    0x09, 0x50,                 //   Usage (Switch On Control)     -- HUT 30, ID 0x50 (DV)
    0x09, 0x51,                 //   Usage (Switch Off Control)    -- HUT 30, ID 0x51 (DV)
    0x91, 0x02,                 //   Output (Data,Var,Abs)         -- remote power on/off
    0x95, 0x06,                 //   Report Count (6)
    0x91, 0x03,                 //   Output (Const,Var,Abs)        -- 6-bit pad
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 28 : Battery System Page (0x85)        [HUT section 31]
  //  Smart-battery gauge.  The page has no Application usage of its own, so the
  //  collection is opened with the Power page's "Battery System" usage (0x84/0x10) and
  //  the data fields then switch to the Battery System page (0x85).
  //  *** Same OS power-binding caveat as Report 27 (Power): report a healthy charge
  //  state, or stay silent during bring-up, so the host does not act on a low reading.
  // ===================================================================================
  0x05, 0x84,        // Usage Page (Power)                          -- parent app collection
  0x09, 0x10,        // Usage (Battery System)                      -- HUT 30, ID 0x10 (CP)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_BATTERY,    // Report ID (28)
    0x05, 0x85,                 //   Usage Page (Battery System)    -- HUT 31, page 0x85
    0x15, 0x00,                 //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, // Logical Maximum (65535)
    0x75, 0x10,                 //   Report Size (16 bits)
    0x95, 0x04,                 //   Report Count (4 gauges)
    0x09, 0x66,                 //   Usage (Remaining Capacity)     -- HUT 31, ID 0x66 (DV)
    0x09, 0x67,                 //   Usage (Full Charge Capacity)   -- HUT 31, ID 0x67 (DV)
    0x09, 0x68,                 //   Usage (Run Time To Empty)      -- HUT 31, ID 0x68 (DV)
    0x09, 0x6B,                 //   Usage (Cycle Count)            -- HUT 31, ID 0x6B (DV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- mAh, mAh, min, count
    0x09, 0x64,                 //   Usage (Relative State Of Charge)-- HUT 31, ID 0x64 (DV)
    0x25, 0x64,                 //   Logical Maximum (100)          -- percent
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x01,                 //   Report Count (1)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- charge %
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x03,                 //   Report Count (3 status flags)
    0x09, 0x44,                 //   Usage (Charging)               -- HUT 31, ID 0x44 (Sel)
    0x09, 0x45,                 //   Usage (Discharging)            -- HUT 31, ID 0x45 (Sel)
    0x09, 0xD0,                 //   Usage (AC Present)             -- HUT 31, ID 0xD0 (DV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- charge state bits
    0x95, 0x05,                 //   Report Count (5)
    0x81, 0x03,                 //   Input (Const,Var,Abs)          -- 5-bit pad
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 29 : Barcode Scanner Page (0x8C)       [HUT section 32]
  //  A handheld scanner: trigger-state Input + a 32-byte decoded-data Input buffer,
  //  a host-driven Output to initiate a read, and an Aiming/Pointer Mode capability
  //  served as a Feature -- per HUT 32.3 it is a Static Flag (SF) scanner attribute,
  //  which 32.2 places in the Attribute Report (a Feature report), not an Output.
  // ===================================================================================
  0x05, 0x8C,        // Usage Page (Barcode Scanner)                -- HUT 32, page 0x8C
  0x09, 0x02,        // Usage (Barcode Scanner)                     -- HUT 32, ID 0x02 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_BARCODE,    // Report ID (29)
    0x09, 0x61,                 //   Usage (Trigger State)          -- HUT 32, ID 0x61 (OOC)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x01,                 //   Report Count (1)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- trigger pressed?
    0x95, 0x07,                 //   Report Count (7)
    0x81, 0x03,                 //   Input (Const,Var,Abs)          -- 7-bit pad
    0x09, 0xFE,                 //   Usage (Decoded Data)           -- HUT 32, ID 0xFE (DV)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x20,                 //   Report Count (32 chars)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- decoded symbol payload
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x01,                 //   Report Count (1)
    0x09, 0x60,                 //   Usage (Initiate Barcode Read)  -- HUT 32, ID 0x60 (DF)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- host triggers a scan
    0x95, 0x07,                 //   Report Count (7)
    0x91, 0x03,                 //   Output (Const,Var,Abs)         -- 7-bit pad (1-byte Output)
    0x09, 0x30,                 //   Usage (Aiming/Pointer Mode)    -- HUT 32, ID 0x30 (SF, 32.3)
    0x95, 0x01,                 //   Report Count (1)
    0xB1, 0x02,                 //   Feature (Data,Var,Abs)         -- "supports aiming" attribute
    0x95, 0x07,                 //   Report Count (7)
    0xB1, 0x03,                 //   Feature (Const,Var,Abs)        -- 7-bit pad (1-byte Feature)
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 30 : Scales Page (0x8D)                [HUT section 33]
  //  A weighing scale: 32-bit weight + signed scaling exponent + weight-unit selector
  //  (Input), and a Zero-Scale command (Output).
  // ===================================================================================
  0x05, 0x8D,        // Usage Page (Scales)                         -- HUT 33, page 0x8D
  0x09, 0x01,        // Usage (Scales)                              -- HUT 33, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_SCALE,      // Report ID (30)
    0x09, 0x40,                 //   Usage (Data Weight)            -- HUT 33, ID 0x40 (DV)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0xFF, 0x7F, // Logical Maximum (2147483647)
    0x75, 0x20,                 //   Report Size (32 bits)
    0x95, 0x01,                 //   Report Count (1)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- raw weight count
    0x09, 0x41,                 //   Usage (Data Scaling)           -- HUT 33, ID 0x41 (DV)
    0x15, 0x80,                 //   Logical Minimum (-128)
    0x25, 0x7F,                 //   Logical Maximum (127)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- base-10 exponent
    0x09, 0x50,                 //   Usage (Weight Unit)            -- HUT 33, ID 0x50 (CL)
    0xA1, 0x02,                 //   Collection (Logical)
      0x15, 0x00,               //     Logical Minimum (0)
      0x26, 0xFF, 0x00,         //     Logical Maximum (255)
      0x75, 0x08,               //     Report Size (8 bits)
      0x95, 0x01,               //     Report Count (1)
      0x19, 0x51,               //     Usage Minimum (Weight Unit Milligram) -- HUT 33, ID 0x51
      0x29, 0x5C,               //     Usage Maximum (Weight Unit Pound)     -- HUT 33, ID 0x5C (0x5B=Ounce)
      0x81, 0x00,               //     Input (Data,Array,Abs)       -- which unit
    0xC0,                       //   End Collection (Logical)
    0x09, 0x80,                 //   Usage (Zero Scale)             -- HUT 33, ID 0x80 (OOC)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x01,                 //   Report Count (1)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- tare/zero command
    0x95, 0x07,                 //   Report Count (7)
    0x91, 0x03,                 //   Output (Const,Var,Abs)         -- 7-bit pad
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 31 : Magnetic Stripe Reader Page (0x8E)   [HUT section 34]
  //  A 3-track MSR: per-track byte-length fields followed by the decoded track data.
  //  Track sizes follow ISO/IEC 7811 maxima (79 / 40 / 107 characters).
  //  *** OVERSIZE: this Input report is 229 B, far over the 63 B usable payload of the
  //  default 64 B HID buffer -- won't transmit until the buffer is enlarged, or shrink/
  //  split the track-data fields. ***
  // ===================================================================================
  0x05, 0x8E,        // Usage Page (Magnetic Stripe Reader)         -- HUT 34, page 0x8E
  0x09, 0x01,        // Usage (MSR Device Read-Only)                -- HUT 34, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_MSR,        // Report ID (31)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x01,                 //   Report Count (1) per length
    0x09, 0x11,                 //   Usage (Track 1 Length)         -- HUT 34, ID 0x11 (DV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- bytes valid in track 1
    0x09, 0x12,                 //   Usage (Track 2 Length)         -- HUT 34, ID 0x12 (DV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- bytes valid in track 2
    0x09, 0x13,                 //   Usage (Track 3 Length)         -- HUT 34, ID 0x13 (DV)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- bytes valid in track 3
    0x09, 0x21,                 //   Usage (Track 1 Data)           -- HUT 34, ID 0x21 (DV)
    0x95, 0x4F,                 //   Report Count (79)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- track 1 characters
    0x09, 0x22,                 //   Usage (Track 2 Data)           -- HUT 34, ID 0x22 (DV)
    0x95, 0x28,                 //   Report Count (40)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- track 2 characters
    0x09, 0x23,                 //   Usage (Track 3 Data)           -- HUT 34, ID 0x23 (DV)
    0x95, 0x6B,                 //   Report Count (107)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- track 3 characters
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 32 : Camera Control Page (0x90)        [HUT section 35]
  //  The page defines only two One-Shot triggers; a camera-button device reports them
  //  as momentary Inputs.  The collection is opened with Camera Auto-focus (no CA on
  //  this page).
  // ===================================================================================
  0x05, 0x90,        // Usage Page (Camera Control)                 -- HUT 35, page 0x90
  0x09, 0x20,        // Usage (Camera Auto-focus)                   -- HUT 35, ID 0x20 (OSC)
  0xA1, 0x01,        // Collection (Application)  (page has no CA; representative usage)
    0x85, REPORT_ID_CAMERA,     // Report ID (32)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x02,                 //   Report Count (2 triggers)
    0x09, 0x20,                 //   Usage (Camera Auto-focus)      -- HUT 35, ID 0x20 (OSC)
    0x09, 0x21,                 //   Usage (Camera Shutter)         -- HUT 35, ID 0x21 (OSC)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- half/full shutter press
    0x95, 0x06,                 //   Report Count (6)
    0x81, 0x03,                 //   Input (Const,Var,Abs)          -- 6-bit pad
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 33 : Arcade Page (0x91)                [HUT section 36]
  //  A JAMMA-style I/O board: 8 digital inputs + 1 analog input (Input), 8 digital
  //  outputs + a coin-door lockout (Output).
  // ===================================================================================
  0x05, 0x91,        // Usage Page (Arcade)                         -- HUT 36, page 0x91
  0x09, 0x01,        // Usage (General Purpose IO Card)             -- HUT 36, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_ARCADE,     // Report ID (33)
    0x09, 0x31,                 //   Usage (GP Digital Input State) -- HUT 36, ID 0x31 (DV)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x08,                 //   Report Count (8 digital ins)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- buttons/switches
    0x09, 0x30,                 //   Usage (GP Analog Input State)  -- HUT 36, ID 0x30 (DV)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x01,                 //   Report Count (1)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- e.g. wheel pot
    0x09, 0x33,                 //   Usage (GP Digital Output State)-- HUT 36, ID 0x33 (DV)
    0x25, 0x01,                 //   Logical Maximum (1)
    0x75, 0x01,                 //   Report Size (1 bit)
    0x95, 0x08,                 //   Report Count (8 digital outs)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- lamps/solenoids
    0x09, 0x40,                 //   Usage (Coin Door Lockout)      -- HUT 36, ID 0x40 (OOC)
    0x95, 0x01,                 //   Report Count (1)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- disable coin slot
    0x95, 0x07,                 //   Report Count (7)
    0x91, 0x03,                 //   Output (Const,Var,Abs)         -- 7-bit pad
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 34 : Gaming Device Page (0x92)         [HUT section 37]
  //  The HUT assigns this entire page to the Gaming Standards Association (GSA) for its
  //  own slot-machine / casino peripheral standard; HUT 1.7 defines NO usages here.
  //  We therefore emit a vendor-style 8-byte In / 8-byte Out report tagged with the
  //  page and a placeholder Usage 0x01, whose meaning is defined externally by the GSA
  //  (https://www.gamingstandards.com).
  // ===================================================================================
  0x05, 0x92,        // Usage Page (Gaming Device)                  -- HUT 37, page 0x92
  0x09, 0x01,        // Usage (vendor/GSA-defined 0x01)             -- no HUT name (GSA-owned)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_GAMING_DEVICE, // Report ID (34)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x08,                 //   Report Count (8 bytes)
    0x09, 0x01,                 //   Usage (0x01)                   -- GSA payload
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- 8 opaque input bytes
    0x09, 0x01,                 //   Usage (0x01)                   -- GSA payload
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- 8 opaque output bytes
  0xC0,              // End Collection

  // ===================================================================================
  //  Report 35 : FIDO Alliance Page (0xF1D0)       [HUT section 38]
  //  FIDO U2F / CTAPHID authenticator.  0xF1D0 is a 16-bit page value, so Usage Page
  //  uses the 2-byte (0x06) form.  64-byte Input + 64-byte Output carry raw CTAPHID
  //  frames.  (Real U2FHID interfaces omit a Report ID; here a Report ID is required
  //  because this is one shared interface, so REPORT_ID_FIDO is included.)
  //  *** Two consequences of the shared Report ID: (1) 64 B data + 1 ID = 65 B exceeds
  //  the 63 B usable payload of the default 64 B HID buffer, so it won't send as-is;
  //  (2) CTAPHID forbids a Report ID and needs exact 64 B frames, so it won't work as a
  //  real authenticator here. For real FIDO use a dedicated HID interface. ***
  // ===================================================================================
  0x06, 0xD0, 0xF1,  // Usage Page (FIDO Alliance, 0xF1D0)          -- HUT 38, page 0xF1D0
  0x09, 0x01,        // Usage (U2F Authenticator Device)            -- HUT 38, ID 0x01 (CA)
  0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_FIDO,       // Report ID (35)
    0x09, 0x20,                 //   Usage (Input Report Data)      -- HUT 38, ID 0x20 (DV)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x40,                 //   Report Count (64 bytes)
    0x81, 0x02,                 //   Input (Data,Var,Abs)           -- CTAPHID -> host frame
    0x09, 0x21,                 //   Usage (Output Report Data)     -- HUT 38, ID 0x21 (DV)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8 bits)
    0x95, 0x40,                 //   Report Count (64 bytes)
    0x91, 0x02,                 //   Output (Data,Var,Abs)          -- host -> CTAPHID frame
  0xC0               // End Collection
};

#endif // HID_DESCRIPTOR_H_
