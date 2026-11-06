#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "monitor/protocol.h"

// The themes' alert sounds, synthesised here (no recordings): what the
// board plays when a tile turns red, and, for the theme it switches to,
// when the theme changes. Each theme has two:
//
//   classic   two notes the board plays itself with tones (rising for a
//             permission, falling for an error); nothing rendered here
//   anime     a HUD warning: three bursts of three sharp high beeps;
//             an error wails down twice with a buzzing edge
//   lcars     a starship's red-alert klaxon: three rising whoops; an
//             error is the computer's falling three-note "unable to comply"
//   galactic  a targeting computer locking on: beeps closing in faster and
//             higher, then a held tone; an error is a system powering down
//             with a stutter, and two blips
//
// Small speakers lose everything under ~400 Hz, so no sound lives there.
// A board may play a recording instead, from a WAV the user supplies for
// their own build (decodeWav below); nothing recorded ships with the code.

namespace monitor {

enum class Alert : uint8_t {
    Attention,   // a permission wanted (WaitingUser); also the theme switch's sound
    Failure,     // an error
};

// The alert a state change asks for
inline Alert alertFor(State s) { return s == State::Error ? Alert::Failure : Alert::Attention; }

// Mono 16-bit PCM at `sampleRate` (16 kHz up; the highest notes are at
// 2 kHz with harmonics above), its peak at 80 % of full scale, starting and
// ending at silence. Empty for the classic theme.
std::vector<int16_t> renderAlert(Theme theme, Alert alert, uint32_t sampleRate);

// A recording to play instead (a WAV the user drops in the board's sounds
// folder, embedded in that build only): uncompressed PCM, 8 or 16 bits,
// mono or stereo, read byte by byte (the embedded file need not be
// aligned) into mono 16-bit samples, stereo averaged. A data chunk that
// claims more than the file holds is cut to what is there. False, with
// `pcm` empty, for anything else (compressed, 24/32-bit, more channels,
// no data).
bool decodeWav(const uint8_t* file, size_t size, std::vector<int16_t>& pcm, uint32_t& sampleRate);

}  // namespace monitor
