# 📻❤️ Chipra - FM

A complete FM radio simulation running entirely in the browser, from signal modulation to audio playback.

Five stations broadcast simultaneously over a simulated 1 MHz spectrum. A virtual receiver tunes, demodulates, and plays them back in real time.

See it in [Github Pages](https://alex461538.github.io/chipra-fm/).

The full pipeline happens on every chunk:

- **Modulation**: each station encodes its audio into the instantaneous frequency of a carrier wave.
- **Superposition**: all four modulated signals are summed into a single composite signal.
- **Tuning**: the composite is multiplied by a complex oscillator at the chosen carrier frequency, centering the target station at DC.
- **Lowpass filter**: removes all other stations from the spectrum.
- **I/Q demodulation**: the instantaneous phase is extracted via atan2(Q, I).
- **Downsampling + output**: the recovered audio is resampled and streamed to the Web Audio API.

## Running

Open `index.html` in live server or anything similar.
Audio requires a user gesture to start (browser policy). Click the dial.

## FM module recompile

If you want to do extra things, you can recompile the `fm.c` file:

    clang --target=wasm32-unknown-unknown -O3 -fno-builtin-memset -fno-builtin-memcpy -nostdlib -Wl,--no-entry -Wl,--export-all -Wl,--allow-undefined -o fm.wasm fm.c

#### Protect chocobos worldwide 🐥