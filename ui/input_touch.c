#include <stdbool.h>
#include <stdio.h>

#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

#include "board_config.h"
#include "lcdspi.h"
#include "key_event.h"
#include "input_touch.h"
#if TOUCH_DEBUG
#include "text_directory_ui.h"
#endif

// The XPT2046 is limited to 2.5MHz; it shares the bus with the LCD and SD card,
// which set their own clock before each access
#define TP_SPI_BAUDRATE (1000 * 1000)

// 12-bit differential conversions, powered down in between with PENIRQ enabled
#define TP_CMD_X 0xD0
#define TP_CMD_Y 0x90

#define TP_SAMPLES 4

#define TP_PRESS_DEBOUNCE_US (20 * 1000)
#define TP_RELEASE_DEBOUNCE_US (30 * 1000)
#define TP_REPEAT_DELAY_US (400 * 1000)
#define TP_REPEAT_RATE_US (150 * 1000)

enum touch_zone_e
{
  ZONE_UP,
  ZONE_DOWN,
  ZONE_BACK,
  ZONE_ENTER,
};

enum touch_state_e
{
  TOUCH_WAIT_RELEASE,  // ignore the touch that was held to enter the menu
  TOUCH_IDLE,
  TOUCH_DEBOUNCE,
  TOUCH_HELD,
};

static enum touch_state_e state = TOUCH_WAIT_RELEASE;
static enum touch_zone_e held_zone;
static uint32_t state_time;
static uint32_t repeat_time;
static uint32_t release_time;
static bool releasing;

static inline bool touch_down(void) { return !gpio_get(TP_IRQ_PIN); }

static uint16_t read_channel(uint8_t cmd)
{
  uint8_t tx[3] = {cmd, 0, 0};
  uint8_t rx[3];

  spi_write_read_blocking(TP_SPI_INST, tx, rx, sizeof(tx));

  return ((rx[1] << 8) | rx[2]) >> 3;
}

static int scale(int raw, int raw_start, int raw_end, int size)
{
  int pos = (raw - raw_start) * size / (raw_end - raw_start);

  if (pos < 0) return 0;
  if (pos >= size) return size - 1;
  return pos;
}

// Reads the touch position in screen coordinates. Returns false if the panel
// was released while sampling.
static bool touch_read(int *x, int *y)
{
  uint32_t sum_x = 0, sum_y = 0;

  spi_set_baudrate(TP_SPI_INST, TP_SPI_BAUDRATE);
  gpio_put(TP_CS_PIN, 0);
  for (int i = 0; i < TP_SAMPLES; i++)
  {
    sum_x += read_channel(TP_CMD_X);
    sum_y += read_channel(TP_CMD_Y);
  }
  gpio_put(TP_CS_PIN, 1);

  // PENIRQ is not valid during conversions
  busy_wait_us(20);
  if (!touch_down()) return false;

  int raw_x = sum_x / TP_SAMPLES;
  int raw_y = sum_y / TP_SAMPLES;

#if TP_SWAP_XY
  *x = scale(raw_y, TP_RAW_LEFT, TP_RAW_RIGHT, LCD_WIDTH);
  *y = scale(raw_x, TP_RAW_TOP, TP_RAW_BOTTOM, LCD_HEIGHT);
#else
  *x = scale(raw_x, TP_RAW_LEFT, TP_RAW_RIGHT, LCD_WIDTH);
  *y = scale(raw_y, TP_RAW_TOP, TP_RAW_BOTTOM, LCD_HEIGHT);
#endif

#if TOUCH_DEBUG
  char msg[40];
  snprintf(msg, sizeof(msg), "X%4d Y%4d -> %3d,%3d", raw_x, raw_y, *x, *y);
  text_directory_ui_set_status(msg);
#endif

  return true;
}

static enum touch_zone_e zone_at(int x, int y)
{
  if (y < LCD_HEIGHT / 3) return ZONE_UP;
  if (y >= LCD_HEIGHT - LCD_HEIGHT / 3) return ZONE_DOWN;
  return (x < LCD_WIDTH / 2) ? ZONE_BACK : ZONE_ENTER;
}

static bool zone_repeats(enum touch_zone_e zone) { return zone == ZONE_UP || zone == ZONE_DOWN; }

static int zone_key(enum touch_zone_e zone)
{
  switch (zone)
  {
    case ZONE_UP:
      return KEY_ARROW_UP;
    case ZONE_DOWN:
      return KEY_ARROW_DOWN;
    case ZONE_BACK:
      return KEY_BACKSPACE;
    case ZONE_ENTER:
      return KEY_ENTER;
  }
  return -1;
}

// True once the panel has been released for longer than the debounce time
static bool released(uint32_t now)
{
  if (touch_down())
  {
    releasing = false;
    return false;
  }
  if (!releasing)
  {
    releasing = true;
    release_time = now;
  }
  return (now - release_time) >= TP_RELEASE_DEBOUNCE_US;
}

void touch_init(void)
{
  gpio_init(TP_CS_PIN);
  gpio_put(TP_CS_PIN, 1);
  gpio_set_dir(TP_CS_PIN, GPIO_OUT);

  gpio_init(TP_IRQ_PIN);
  gpio_set_dir(TP_IRQ_PIN, GPIO_IN);
  gpio_pull_up(TP_IRQ_PIN);

  // One conversion puts the controller into power-down with PENIRQ enabled
  spi_set_baudrate(TP_SPI_INST, TP_SPI_BAUDRATE);
  gpio_put(TP_CS_PIN, 0);
  read_channel(TP_CMD_X);
  gpio_put(TP_CS_PIN, 1);

  state = TOUCH_WAIT_RELEASE;
  releasing = false;
}

int touch_get_key(void)
{
  uint32_t now = time_us_32();
  int x, y;

  switch (state)
  {
    case TOUCH_WAIT_RELEASE:
      if (released(now)) state = TOUCH_IDLE;
      break;

    case TOUCH_IDLE:
      if (touch_down())
      {
        state = TOUCH_DEBOUNCE;
        state_time = now;
      }
      break;

    case TOUCH_DEBOUNCE:
      if (!touch_down())
      {
        state = TOUCH_IDLE;
        break;
      }
      if ((now - state_time) < TP_PRESS_DEBOUNCE_US) break;

      if (!touch_read(&x, &y))
      {
        state = TOUCH_IDLE;
        break;
      }

      held_zone = zone_at(x, y);
      state = TOUCH_HELD;
      releasing = false;
      if (zone_repeats(held_zone))
      {
        repeat_time = now + TP_REPEAT_DELAY_US;
        return zone_key(held_zone);
      }
      break;

    case TOUCH_HELD:
      if (released(now))
      {
        state = TOUCH_IDLE;
        if (!zone_repeats(held_zone)) return zone_key(held_zone);
        break;
      }
      if (zone_repeats(held_zone) && (int32_t)(now - repeat_time) >= 0)
      {
        repeat_time = now + TP_REPEAT_RATE_US;
        return zone_key(held_zone);
      }
      break;
  }

  return -1;
}
