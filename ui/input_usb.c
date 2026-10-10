// USB keyboards and gamepads on the native USB port, as menu keys.
//
// Adapted from galagino_pizero's usb_input.c: gamepads are decoded from their
// HID report descriptor (LUFA-derived parser from msx2pico), except for known
// cheap SNES clones, which don't describe themselves reliably and are decoded
// from fixed byte positions (maps measured for frank-snes). Boot protocol
// keyboards work too. XInput (Xbox style) pads are not supported.

#include <stdbool.h>
#include <string.h>

#include "pico/time.h"
#include "tusb.h"
#include "hidparser.h"

#include "key_event.h"
#include "input_usb.h"
#if USB_DEBUG
#include <stdio.h>
#include "text_directory_ui.h"
#endif

// Menu buttons
#define BTN_UP    0x01
#define BTN_DOWN  0x02
#define BTN_LEFT  0x04
#define BTN_RIGHT 0x08
#define BTN_OK    0x10
#define BTN_BACK  0x20

// Gamepad button usages (1-based, as in the HID report descriptor) on pads
// decoded generically. Layouts differ, so two buttons each select and go back.
#define PAD_OK_FIRST    1
#define PAD_OK_LAST     2
#define PAD_BACK_FIRST  3
#define PAD_BACK_LAST   4
#define PAD_SELECT_A    7
#define PAD_SELECT_B    9
#define PAD_START_A     8
#define PAD_START_B    10

#define REPEAT_DELAY_US (400 * 1000)
#define REPEAT_RATE_US (150 * 1000)

#define MAX_DEV (CFG_TUH_DEVICE_MAX + 2)  // device addresses start at 1, plus hub
#define MAX_ITF CFG_TUH_HID

static HID_ReportInfo_t *pad_info[MAX_DEV][MAX_ITF];
static uint8_t itf_buttons[MAX_DEV][MAX_ITF];

// Fixed-layout pads
typedef struct
{
  uint8_t byte, mask;
} pad_bit_t;

typedef struct
{
  uint16_t vid, pid;
  bool hat;  // false: axes at dx/dy (0x7f centre), true: hat in low nibble of dx
  uint8_t dx, dy;
  pad_bit_t a, b, start, select;
} pad_map_t;

#define SNES_CLONE_BUTTONS {5, 0x20}, {5, 0x40}, {6, 0x20}, {6, 0x10}

static const pad_map_t pad_maps[] = {
    {0x0079, 0x0006, false, 0, 1, SNES_CLONE_BUTTONS},  // DragonRise "USB Gamepad"
    {0x0079, 0x0011, false, 3, 4, SNES_CLONE_BUTTONS},  // SNES clone
    {0x081f, 0xe401, false, 0, 1, SNES_CLONE_BUTTONS},  // SNES clone
    {0x0810, 0xe501, false, 3, 4, SNES_CLONE_BUTTONS},  // SNES clone variant
    {0x046d, 0xc219, true, 5, 0, SNES_CLONE_BUTTONS},   // Logitech
    {0x11ff, 0x3331, false, 0, 1, {5, 0x80}, {5, 0x40}, {6, 0x20}, {6, 0x10}},
    {0x2563, 0x0575, true, 2, 0, {0, 0x04}, {0, 0x02}, {1, 0x02}, {1, 0x01}},
    {0xfeed, 0x2320, true, 5, 0, {6, 0x01}, {6, 0x02}, {7, 0x08}, {7, 0x04}},
};

static const pad_map_t *itf_map[MAX_DEV][MAX_ITF];

#if USB_DEBUG
// Shown in the status bar: devices and HID interfaces mounted, the last
// interface's protocol (k = keyboard, m = mouse, p = parsed pad, f = fixed map,
// - = ignored) and report count, and the start of its last report
static int dbg_devices, dbg_mounted;
static char dbg_proto = ' ';
static unsigned dbg_reports;

static void debug_status(const uint8_t *report, uint16_t len)
{
  char msg[48];
  int n = snprintf(msg, sizeof(msg), "USB d%d h%d %c r%u", dbg_devices, dbg_mounted, dbg_proto,
                   dbg_reports);
  for (int i = 0; report && i < len && i < 6 && n < (int)sizeof(msg) - 3; i++)
    n += snprintf(msg + n, sizeof(msg) - n, " %02x", report[i]);
  text_directory_ui_set_status(msg);
}

void tuh_mount_cb(uint8_t dev_addr)
{
  (void)dev_addr;
  dbg_devices++;
  debug_status(NULL, 0);
}

void tuh_umount_cb(uint8_t dev_addr)
{
  (void)dev_addr;
  if (dbg_devices) dbg_devices--;
  debug_status(NULL, 0);
}
#endif

// True if one of the interface's top level collections is a gamepad or
// joystick. Only those go through the generic parser: keyboards often have
// extra interfaces (media keys, NKRO, vendor) with large descriptors.
static bool is_gamepad(uint8_t const *desc_report, uint16_t desc_len)
{
  tuh_hid_report_info_t info[4];
  uint8_t count = tuh_hid_parse_report_descriptor(info, count_of(info), desc_report, desc_len);

  for (uint8_t i = 0; i < count; i++)
  {
    if (info[i].usage_page == HID_USAGE_PAGE_DESKTOP &&
        (info[i].usage == HID_USAGE_DESKTOP_GAMEPAD || info[i].usage == HID_USAGE_DESKTOP_JOYSTICK))
      return true;
  }
  return false;
}

// hat switch: 0 = N, clockwise to 7 = NW, anything else = released
static const uint8_t hat_dirs[8] = {
    BTN_UP,   BTN_UP | BTN_RIGHT,   BTN_RIGHT, BTN_DOWN | BTN_RIGHT,
    BTN_DOWN, BTN_DOWN | BTN_LEFT, BTN_LEFT,  BTN_UP | BTN_LEFT};

/* ------------------------------- gamepads ------------------------------- */

// Called by the parser: keep only what a gamepad needs
bool CALLBACK_HIDParser_FilterHIDReportItem(HID_ReportItem_t *const item)
{
  if (item->ItemType != HID_REPORT_ITEM_In) return false;

  switch (item->Attributes.Usage.Page)
  {
    case HID_USAGE_PAGE_DESKTOP:
      switch (item->Attributes.Usage.Usage)
      {
        case HID_USAGE_DESKTOP_X:
        case HID_USAGE_DESKTOP_Y:
        case HID_USAGE_DESKTOP_HAT_SWITCH:
        case HID_USAGE_DESKTOP_DPAD_UP:
        case HID_USAGE_DESKTOP_DPAD_DOWN:
        case HID_USAGE_DESKTOP_DPAD_LEFT:
        case HID_USAGE_DESKTOP_DPAD_RIGHT:
          return true;
      }
      return false;
    case HID_USAGE_PAGE_BUTTON:
      return true;
  }
  return false;
}

// Item value as a signed number if its logical range is signed
static int32_t item_value(const HID_ReportItem_t *item)
{
  int32_t v = item->Value;
  int32_t min = (int32_t)item->Attributes.Logical.Minimum;
  int bits = item->Attributes.BitSize;

  if (min < 0 && bits > 0 && bits < 32 && (v & (1 << (bits - 1)))) v -= 1 << bits;
  return v;
}

// -1, 0 or 1 for an axis, with a dead zone of a quarter of each half
static int axis_dir(const HID_ReportItem_t *item)
{
  int32_t min = (int32_t)item->Attributes.Logical.Minimum;
  int32_t max = (int32_t)item->Attributes.Logical.Maximum;
  if (max <= min) return 0;

  int32_t center = min + (max - min) / 2;
  int32_t dead = (max - min) / 8;
  int32_t v = item_value(item);
  if (v < center - dead) return -1;
  if (v > center + dead) return 1;
  return 0;
}

static uint8_t parse_gamepad(HID_ReportInfo_t *info, const uint8_t *report)
{
  uint8_t b = 0;

  for (HID_ReportItem_t *item = info->FirstReportItem; item; item = item->Next)
  {
    // skip items belonging to another report id
    const uint8_t *data = report;
    if (item->ReportID)
    {
      if (item->ReportID != report[0]) continue;
      data++;
    }
    if (!USB_GetHIDReportItemInfo(item->ReportID, data, item)) continue;

    if (item->Attributes.Usage.Page == HID_USAGE_PAGE_DESKTOP)
    {
      switch (item->Attributes.Usage.Usage)
      {
        case HID_USAGE_DESKTOP_X: {
          int d = axis_dir(item);
          if (d < 0) b |= BTN_LEFT;
          if (d > 0) b |= BTN_RIGHT;
          break;
        }
        case HID_USAGE_DESKTOP_Y: {
          int d = axis_dir(item);
          if (d < 0) b |= BTN_UP;
          if (d > 0) b |= BTN_DOWN;
          break;
        }
        case HID_USAGE_DESKTOP_HAT_SWITCH: {
          uint32_t h = item->Value - item->Attributes.Logical.Minimum;
          if (h < 8) b |= hat_dirs[h];
          break;
        }
        case HID_USAGE_DESKTOP_DPAD_UP:
          if (item->Value) b |= BTN_UP;
          break;
        case HID_USAGE_DESKTOP_DPAD_DOWN:
          if (item->Value) b |= BTN_DOWN;
          break;
        case HID_USAGE_DESKTOP_DPAD_LEFT:
          if (item->Value) b |= BTN_LEFT;
          break;
        case HID_USAGE_DESKTOP_DPAD_RIGHT:
          if (item->Value) b |= BTN_RIGHT;
          break;
      }
    }
    else if (item->Attributes.Usage.Page == HID_USAGE_PAGE_BUTTON && item->Value)
    {
      int u = item->Attributes.Usage.Usage;
      if ((u >= PAD_OK_FIRST && u <= PAD_OK_LAST) || u == PAD_START_A || u == PAD_START_B)
        b |= BTN_OK;
      if ((u >= PAD_BACK_FIRST && u <= PAD_BACK_LAST) || u == PAD_SELECT_A || u == PAD_SELECT_B)
        b |= BTN_BACK;
    }
  }
  return b;
}

static inline bool pad_bit(const uint8_t *r, uint16_t len, pad_bit_t b)
{
  return b.byte < len && (r[b.byte] & b.mask);
}

static uint8_t parse_mapped(const pad_map_t *m, const uint8_t *r, uint16_t len)
{
  uint8_t b = 0;

  if (!m->hat)
  {
    if (m->dx < len && r[m->dx] < 0x40) b |= BTN_LEFT;
    if (m->dx < len && r[m->dx] > 0xc0) b |= BTN_RIGHT;
    if (m->dy < len && r[m->dy] < 0x40) b |= BTN_UP;
    if (m->dy < len && r[m->dy] > 0xc0) b |= BTN_DOWN;
  }
  else if (m->dx < len)
  {
    uint8_t h = r[m->dx] & 0x0f;
    if (h < 8) b |= hat_dirs[h];
  }

  if (pad_bit(r, len, m->a) || pad_bit(r, len, m->start)) b |= BTN_OK;
  if (pad_bit(r, len, m->b) || pad_bit(r, len, m->select)) b |= BTN_BACK;
  return b;
}

/* ------------------------------- keyboards ------------------------------ */

static uint8_t parse_keyboard(const hid_keyboard_report_t *r)
{
  uint8_t b = 0;

  for (int i = 0; i < 6; i++)
  {
    switch (r->keycode[i])
    {
      case HID_KEY_ARROW_UP:
        b |= BTN_UP;
        break;
      case HID_KEY_ARROW_DOWN:
        b |= BTN_DOWN;
        break;
      case HID_KEY_ARROW_LEFT:
        b |= BTN_LEFT;
        break;
      case HID_KEY_ARROW_RIGHT:
        b |= BTN_RIGHT;
        break;
      case HID_KEY_ENTER:
      case HID_KEY_KEYPAD_ENTER:
        b |= BTN_OK;
        break;
      case HID_KEY_BACKSPACE:
      case HID_KEY_ESCAPE:
        b |= BTN_BACK;
        break;
    }
  }
  return b;
}

/* --------------------------- TinyUSB callbacks -------------------------- */

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report,
                      uint16_t desc_len)
{
  uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
  uint16_t vid = 0, pid = 0;

  tuh_vid_pid_get(dev_addr, &vid, &pid);

  if (dev_addr < MAX_DEV && instance < MAX_ITF)
  {
    itf_buttons[dev_addr][instance] = 0;
    itf_map[dev_addr][instance] = NULL;
    for (unsigned i = 0; i < count_of(pad_maps); i++)
    {
      if (pad_maps[i].vid == vid && pad_maps[i].pid == pid) itf_map[dev_addr][instance] = &pad_maps[i];
    }

    if (proto == HID_ITF_PROTOCOL_KEYBOARD)
    {
      tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_BOOT);
    }
    else if (proto == HID_ITF_PROTOCOL_NONE && !itf_map[dev_addr][instance] &&
             is_gamepad(desc_report, desc_len))
    {
      if (pad_info[dev_addr][instance]) USB_FreeReportInfo(pad_info[dev_addr][instance]);
      pad_info[dev_addr][instance] = NULL;
      if (USB_ProcessHIDReport(desc_report, desc_len, &pad_info[dev_addr][instance]) !=
          HID_PARSE_Successful)
      {
        if (pad_info[dev_addr][instance]) USB_FreeReportInfo(pad_info[dev_addr][instance]);
        pad_info[dev_addr][instance] = NULL;
      }
    }

#if USB_DEBUG
    dbg_proto = proto == HID_ITF_PROTOCOL_KEYBOARD ? 'k'
                : proto == HID_ITF_PROTOCOL_MOUSE  ? 'm'
                : itf_map[dev_addr][instance]      ? 'f'
                : pad_info[dev_addr][instance]     ? 'p'
                                                   : '-';
#endif
  }

#if USB_DEBUG
  dbg_mounted++;
  debug_status(NULL, 0);
#endif

  tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
#if USB_DEBUG
  if (dbg_mounted) dbg_mounted--;
  debug_status(NULL, 0);
#endif
  if (dev_addr < MAX_DEV && instance < MAX_ITF)
  {
    itf_buttons[dev_addr][instance] = 0;
    if (pad_info[dev_addr][instance]) USB_FreeReportInfo(pad_info[dev_addr][instance]);
    pad_info[dev_addr][instance] = NULL;
  }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report,
                                uint16_t len)
{
  if (dev_addr < MAX_DEV && instance < MAX_ITF)
  {
    uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
    const pad_map_t *map = itf_map[dev_addr][instance];

    if (proto == HID_ITF_PROTOCOL_KEYBOARD && len >= sizeof(hid_keyboard_report_t))
      itf_buttons[dev_addr][instance] = parse_keyboard((const hid_keyboard_report_t *)report);
    else if (map)
      itf_buttons[dev_addr][instance] = parse_mapped(map, report, len);
    else if (pad_info[dev_addr][instance])
      itf_buttons[dev_addr][instance] = parse_gamepad(pad_info[dev_addr][instance], report);
  }

#if USB_DEBUG
  dbg_reports++;
  debug_status(report, len);
#endif

  tuh_hid_receive_report(dev_addr, instance);
}

/* ------------------------------ menu keys ------------------------------- */

static uint8_t last_buttons;
static uint8_t repeat_button;
static uint32_t repeat_time;

void usb_input_init(void)
{
  tuh_init(BOARD_TUH_RHPORT);
}

int usb_input_get_key(void)
{
  uint32_t now = time_us_32();
  uint8_t buttons = 0;

  tuh_task();

  for (int d = 0; d < MAX_DEV; d++)
  {
    for (int i = 0; i < MAX_ITF; i++) buttons |= itf_buttons[d][i];
  }

  uint8_t pressed = buttons & ~last_buttons;
  last_buttons = buttons;

  if (!(buttons & repeat_button)) repeat_button = 0;

  if (pressed & (BTN_UP | BTN_DOWN))
  {
    repeat_button = (pressed & BTN_UP) ? BTN_UP : BTN_DOWN;
    repeat_time = now + REPEAT_DELAY_US;
    return repeat_button == BTN_UP ? KEY_ARROW_UP : KEY_ARROW_DOWN;
  }
  if (pressed & BTN_OK) return KEY_ENTER;
  if (pressed & (BTN_BACK | BTN_LEFT)) return KEY_BACKSPACE;

  if (repeat_button && (int32_t)(now - repeat_time) >= 0)
  {
    repeat_time = now + REPEAT_RATE_US;
    return repeat_button == BTN_UP ? KEY_ARROW_UP : KEY_ARROW_DOWN;
  }

  return -1;
}
