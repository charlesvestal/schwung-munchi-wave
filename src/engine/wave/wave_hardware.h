/* wave_hardware.h -- the slice of WAVE's Hardware class its engine code uses.
 *
 * The firmware's Sequencer and pages queue MIDI through hw_->queueMidiNote(),
 * hw_->queueMidiTransport() and hw_->SendCC(); on CHOMPI those went to the
 * UART and USB ports. Here they land in a ring that the standalone loop
 * drains to Move's USB-A port. Single-threaded: the SPI loop writes and
 * reads it.
 */
#pragma once
#include <stdint.h>

namespace daisy
{
enum MidiMessageType
{
    NoteOff,
    NoteOn,
    ControlChange,
    SystemRealTime,
};
}
using daisy::ControlChange;
using daisy::NoteOff;
using daisy::NoteOn;

class Hardware
{
  public:
    struct Msg
    {
        uint8_t bytes[3];
        uint8_t len;
    };

    bool cc_out_enabled = true;

    void queueMidiNote(uint8_t ch, uint8_t note, uint8_t vel, daisy::MidiMessageType type)
    {
        // MidiManager::ProcessMidiOut sent every note at velocity 127 and
        // every note-off at 0
        (void)vel;
        if(type == daisy::NoteOn)
            Push(0x90 | (ch & 15), note & 127, 127, 3);
        else
            Push(0x80 | (ch & 15), note & 127, 0, 3);
    }

    void queueMidiTransport(bool start) { Push(start ? 0xFA : 0xFC, 0, 0, 1); }

    void queueMidiClock() { Push(0xF8, 0, 0, 1); }

    void SendCC(uint8_t ch, uint8_t cc, float val)
    {
        if(!cc_out_enabled)
            return;
        int v = (int)val;
        Push(0xB0 | (ch & 15), cc & 127, (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v), 3);
    }

    bool Pop(Msg *m)
    {
        if(r_ == w_)
            return false;
        *m = ring_[r_];
        r_  = (r_ + 1) & 127;
        return true;
    }

  private:
    void Push(uint8_t a, uint8_t b, uint8_t c, uint8_t len)
    {
        int w = (w_ + 1) & 127;
        if(w == r_)
            return;
        ring_[w_] = {{a, b, c}, len};
        w_        = w;
    }
    Msg ring_[128];
    int r_ = 0, w_ = 0;
};
