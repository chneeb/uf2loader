// HDMI output for the menu (LCD_DVI boards): the lcdspi drawing calls the menu
// uses, drawn into a 320x240 RGB332 framebuffer that core 1 sends out as
// 640x480 DVI with every pixel doubled in both directions (PicoDVI).
//
// The menu runs from RAM, so core 1 never reads flash, and the menu can write
// flash while the picture keeps running.

#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "tmds_encode.h"

#include "lcdspi.h"
#include "fonts/font1.h"

#define FB_WIDTH LCD_WIDTH
#define FB_HEIGHT LCD_HEIGHT

static uint8_t __attribute__((aligned(4))) framebuf[FB_WIDTH * FB_HEIGHT];

static struct dvi_inst dvi0;

static const unsigned char *font = font1;
static int current_x, current_y;

// RGB888 as used by the menu (RGB() in lcdspi.h) to RGB332
static inline uint8_t rgb332(int c)
{
  return ((c >> 16) & 0xE0) | (((c >> 8) & 0xE0) >> 3) | ((c & 0xC0) >> 6);
}

// Core 1: TMDS-encode one framebuffer line per scanline pair, forever. The
// DVI DMA interrupt also runs on this core.
static void __not_in_flash_func(core1_main)(void)
{
  const uint words_per_channel = dvi0.timing->h_active_pixels / DVI_SYMBOLS_PER_WORD;

  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  dvi_start(&dvi0);

  while (true)
  {
    for (uint y = 0; y < FB_HEIGHT; y++)
    {
      const uint32_t *line = (const uint32_t *)&framebuf[y * FB_WIDTH];
      uint32_t *tmdsbuf;

      queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
      tmds_encode_data_channel_8bpp(line, tmdsbuf + 0 * words_per_channel, FB_WIDTH,
                                    DVI_8BPP_BLUE_MSB, DVI_8BPP_BLUE_LSB);
      tmds_encode_data_channel_8bpp(line, tmdsbuf + 1 * words_per_channel, FB_WIDTH,
                                    DVI_8BPP_GREEN_MSB, DVI_8BPP_GREEN_LSB);
      tmds_encode_data_channel_8bpp(line, tmdsbuf + 2 * words_per_channel, FB_WIDTH,
                                    DVI_8BPP_RED_MSB, DVI_8BPP_RED_LSB);
      queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
    }
  }
}

void lcd_init(void)
{
  memset(framebuf, 0, sizeof(framebuf));

  // 640x480p60 needs a bit clock of exactly 10x the 25.2 MHz pixel clock, which
  // the PIO serialiser derives from the system clock
  dvi0.timing = &dvi_timing_640x480p_60hz;
  dvi0.ser_cfg = (struct dvi_serialiser_cfg){
      .pio = pio0,
      .sm_tmds = {0, 1, 2},
      .pins_tmds = {DVI_PIN_D0, DVI_PIN_D1, DVI_PIN_D2},
      .pins_clk = DVI_PIN_CLK,
      .invert_diffpairs = DVI_INVERT_DIFFPAIRS,
  };

  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(10);
  set_sys_clock_khz(dvi0.timing->bit_clk_khz, true);

  dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
  multicore_launch_core1(core1_main);
}

void lcd_clear(void) { memset(framebuf, rgb332(BLACK), sizeof(framebuf)); }

// Filled rectangle, corners inclusive and in any order
void draw_rect_spi(int x1, int y1, int x2, int y2, int c)
{
  int t;

  if (x2 < x1) { t = x1; x1 = x2; x2 = t; }
  if (y2 < y1) { t = y1; y1 = y2; y2 = t; }
  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 >= FB_WIDTH) x2 = FB_WIDTH - 1;
  if (y2 >= FB_HEIGHT) y2 = FB_HEIGHT - 1;
  if (x1 > x2 || y1 > y2) return;

  uint8_t colour = rgb332(c);
  for (int y = y1; y <= y2; y++)
  {
    memset(&framebuf[y * FB_WIDTH + x1], colour, x2 - x1 + 1);
  }
}

void draw_line_spi(int x1, int y1, int x2, int y2, int color)
{
  int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
  int dy = abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
  int err = (dx > dy ? dx : -dy) / 2, e2;

  while (1)
  {
    draw_rect_spi(x1, y1, x1, y1, color);
    if (x1 == x2 && y1 == y2) break;
    e2 = err;
    if (e2 > -dx) { err -= dy; x1 += sx; }
    if (e2 < dy) { err += dx; y1 += sy; }
  }
}

// 16x6, same as lcdspi
void draw_battery_icon(int x0, int y0, int level)
{
  draw_rect_spi(x0, y0, x0 + 14, y0 + 6, WHITE);
  draw_rect_spi(x0 + 1, y0 + 1, x0 + 12, y0 + 5, BLACK);
  draw_rect_spi(x0 + 14, y0 + 2, x0 + 14 + 2, y0 + 2 + 2, WHITE);
  for (int i = 0; i <= 13; i++)
  {
    if (i < level)
    {
      draw_rect_spi(x0 + 1 + i, y0 + 1, x0 + 1 + i + 1, y0 + 1 + 4, WHITE);
    }
  }
}

void lcd_set_cursor(int x, int y)
{
  current_x = x;
  current_y = y;
}

// Font format (as in lcdspi): width, height, first char, char count, then each
// char's bitmap with its rows packed MSB first
static void draw_char(char c, uint8_t fg, uint8_t bg)
{
  int width = font[0], height = font[1];
  unsigned char ch = (unsigned char)c;

  if (ch < font[2] || ch >= font[2] + font[3])
  {
    ch = ' ';
  }

  const unsigned char *bitmap = font + 4 + (ch - font[2]) * height * width / 8;

  for (int i = 0; i < height; i++)
  {
    int y = current_y + i;
    if (y < 0 || y >= FB_HEIGHT) continue;

    for (int k = 0; k < width; k++)
    {
      int x = current_x + k;
      if (x < 0 || x >= FB_WIDTH) continue;

      int bit = i * width + k;
      bool set = (bitmap[bit / 8] >> (7 - bit % 8)) & 1;
      framebuf[y * FB_WIDTH + x] = set ? fg : bg;
    }
  }

  current_x += width;
}

void lcd_print_string_color(char *s, int fg, int bg)
{
  uint8_t f = rgb332(fg), b = rgb332(bg);

  while (*s)
  {
    switch (*s)
    {
      case '\r':
        current_x = 0;
        break;
      case '\n':
        current_x = 0;
        current_y += font[1];
        break;
      default:
        draw_char(*s, f, b);
        break;
    }
    s++;
  }
}

void lcd_print_string(char *s) { lcd_print_string_color(s, WHITE, BLACK); }
