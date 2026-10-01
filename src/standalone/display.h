/* display.h -- Move's 128x64 1-bit OLED, driven directly over SPI.
 *
 * Framebuffer, a 5x7 font (from schwung-standalone-example, MIT, with the
 * glyphs it left as placeholders filled in) and the 7-phase slice protocol:
 * phase 0 announces a frame, phases 1-6 carry 172-byte slices of the packed
 * 1024-byte image (8 bands x 128 columns, bit 0 = top pixel of a band).
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include "../schwung_spi_lib.h"

namespace munchi
{

class Display
{
  public:
    static constexpr int W = 128, H = 64;

    void Clear() { memset(fb_, 0, sizeof(fb_)); }

    void Pixel(int x, int y, int on = 1)
    {
        if(x < 0 || x >= W || y < 0 || y >= H)
            return;
        fb_[y * W + x] = (uint8_t)on;
    }

    void Rect(int x, int y, int w, int h, int on = 1)
    {
        for(int j = y; j < y + h; j++)
            for(int i = x; i < x + w; i++)
                Pixel(i, j, on);
    }

    void Frame(int x, int y, int w, int h)
    {
        for(int i = x; i < x + w; i++)
        {
            Pixel(i, y);
            Pixel(i, y + h - 1);
        }
        for(int j = y; j < y + h; j++)
        {
            Pixel(x, j);
            Pixel(x + w - 1, j);
        }
    }

    void HLine(int x, int y, int w, int on = 1) { Rect(x, y, w, 1, on); }
    void VLine(int x, int y, int h, int on = 1) { Rect(x, y, 1, h, on); }

    /** Draws text; `scale` 1 or 2. Returns the width drawn. */
    int Text(int x, int y, const char *s, int scale = 1, int on = 1)
    {
        int ox = x;
        for(; *s; s++)
        {
            const uint8_t *g = Glyph(*s);
            for(int r = 0; r < 7; r++)
                for(int c = 0; c < 5; c++)
                    if(g[r] & (0x80 >> c))
                        Rect(x + c * scale, y + r * scale, scale, scale, on);
            x += 6 * scale;
        }
        return x - ox;
    }

    static int TextWidth(const char *s, int scale = 1)
    {
        int n = (int)strlen(s);
        return n ? n * 6 * scale - scale : 0;
    }

    void TextCentered(int y, const char *s, int scale = 1)
    {
        Text((W - TextWidth(s, scale)) / 2, y, s, scale);
    }

    void TextRight(int xr, int y, const char *s, int scale = 1)
    {
        Text(xr - TextWidth(s, scale), y, s, scale);
    }

    /** Invert a rectangle (for highlighted rows). */
    void Invert(int x, int y, int w, int h)
    {
        for(int j = y; j < y + h; j++)
            for(int i = x; i < x + w; i++)
                if(i >= 0 && i < W && j >= 0 && j < H)
                    fb_[j * W + i] ^= 1;
    }

    bool AtFrameStart() const { return phase_ == 0; }

    /** One SPI tick's worth of display: writes status + slice into the
     *  output region. Call once per transfer. Returns true at phase 0, the
     *  moment to have drawn the next frame. */
    bool PushSlice(uint8_t *spi)
    {
        bool new_frame = phase_ == 0;
        if(phase_ == 0)
        {
            spi[SCHWUNG_OFF_OUT_DISP_STAT] = 0;
            memset(spi + SCHWUNG_OFF_OUT_DISP_DATA, 0, SCHWUNG_OUT_DISP_CHUNK_LEN);
            Pack();
        }
        else
        {
            int slice = phase_ - 1;
            int start = slice * SCHWUNG_OUT_DISP_CHUNK_LEN;
            int bytes = slice == 5 ? 164 : SCHWUNG_OUT_DISP_CHUNK_LEN;
            spi[SCHWUNG_OFF_OUT_DISP_STAT] = (uint8_t)(slice + 1);
            memcpy(spi + SCHWUNG_OFF_OUT_DISP_DATA, packed_ + start, bytes);
        }
        phase_ = (phase_ + 1) % 7;
        return new_frame;
    }

  private:
    void Pack()
    {
        int i = 0;
        for(int band = 0; band < H / 8; band++)
            for(int x = 0; x < W; x++)
            {
                uint8_t b = 0;
                for(int j = 0; j < 8; j++)
                    if(fb_[(band * 8 + j) * W + x])
                        b |= (uint8_t)(1 << j);
                packed_[i++] = b;
            }
    }

    static const uint8_t *Glyph(char c);

    uint8_t fb_[W * H];
    uint8_t packed_[1024];
    int     phase_ = 0;
};

} // namespace munchi
