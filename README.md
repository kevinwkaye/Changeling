# Changeling

A multi-voice instrument synthesizer VST3, recreating the Meris Enzo in the Flip Fella look. Play guitar, bass or a MIDI keyboard into it and every note comes back as a synth voice. It's built to track tight and low, down to a 5-string's low B.

## Install

**Windows**

1. Open the **builds** branch on GitHub and download `Changeling-Windows.zip`.
2. Unzip it and copy `Changeling.vst3` into `C:\Program Files\Common Files\VST3`.
3. In Ableton, open Preferences > Plug-Ins, turn on "Use VST3 Plug-In System Folders" and press Rescan. Changeling appears under Audio Effects.

`Changeling-standalone.exe` in the same zip runs without Ableton.

**Linux ARM64 (the Pi pedalboard)**

1. Download `Changeling-Linux-ARM64.zip` from the **builds** branch.
2. Unzip it and copy `Changeling.vst3` into `~/.vst3`.
3. Or run `Changeling-standalone` directly. Pick your audio interface and MIDI keyboard in its Options menu.

## The modes

| Mode | What it does |
| --- | --- |
| Poly | Every note of a chord gets its own voice, up to six |
| Mono | One voice with two oscillators. The tightest tracking, and the one for bass |
| Arp | Turns what you hold into a pattern at the tapped tempo, quarter notes as on the pedal |
| Dry | The synth is off. Pitch shifts your own signal, Sustain compresses it, Modulation wobbles the delay |

Tap the big mode card, or footswitch C, to step through them.

## The knobs

The Enzo hides a second job behind each knob, reached by holding ALT. Changeling shows every one on screen, with a small amber tag naming the knob it hides behind on the pedal. ALT in the header dims the main knobs so the alt ones stand out.

| Knob | What it does | Alt job |
| --- | --- | --- |
| Pitch | Synth pitch, or your pitch in Dry, in semitones | Glide between notes |
| Cutoff | The filter | Type: ladder low, band or high-pass, or state-variable low, band or high-pass |
| Env | How far the envelope opens the filter | Bandwidth: wide and gentle to narrow and peaky |
| Sustain | How long synth notes hold: a short pluck at zero, held for as long as you hold the note at the top. Notes come in at full level and stay steady, as on the pedal | Ring mod frequency. Fully left is off |
| Mod | Detunes the two oscillators | Delay feedback |
| Mix | Your signal and the synth. Both are at full level at noon | Delay level |

The two switch alt jobs are on screen too. **Wave** is saw or square (Alt + Bypass on the pedal). **Envelope** is Trigger or Follow (Alt + Tap): Trigger opens the filter on each new note, Follow makes it track how hard you play, like an auto-wah.

**Sustain** in the Delay + Arp column latches whatever is sounding into a drone. Turn it off to let go. If nothing is sounding when you latch, it catches the next notes you play.

## Pitch tracking

The original lags and retriggers below the guitar register. Changeling's tracker is built for the low end:

- **Input** sets the range. Bass reaches low B (31 Hz). Guitar starts at drop C. Synth covers everything.
- A note is called once three readings in a row agree, so the first pitch you hear is the right one. On the test phrases that takes about 20 ms on guitar and about 45 ms on bass (54 ms on low B). One cycle of the note is the physical floor for any tracker.
- Bends, slides, vibrato and hammer-ons are followed continuously, without retriggering.
- The gate follows your noise floor, so quiet playing and a turned-down volume knob track the same as loud playing.
- Hum and hiss never play notes. Changeling listens for its first 0.6 seconds after loading, learns any level that holds dead steady as background (a string always fades, hum never does), and remembers it while you roll your volume down and back up. If your rig is very noisy, turn Sensitivity down on the Deep page.
- The octave guard keeps the real note when the second harmonic is louder than the fundamental, which happens a lot on bass.
- A fading note stays one note, with no retriggers and no octave flips.
- **Character** on the Deep page runs from Tight to Wild. Wild brings back the original's glitches: octave flips on the decay, grabbing harmonics, nervous retriggers. Holding footswitch C turns it fully wild while ring chaos plays.

The tracking strip under Pebble shows the note it heard, its frequency and how quickly it locked. Click the note for the tuner, which mutes the output while it's open.

Poly mode listens to the first 40 ms after a strum, finds the notes and takes each one's overtones out before looking for the next. It then holds the chord until the strings fade or you play again. On test chords it plays almost no wrong notes, but it catches about half of a six-string chord's exact notes: you hear a clean, smaller voicing. Use Mono for bass lines and leads.

## Playing from a MIDI keyboard

Notes, pitch bend, the mod wheel (more Mod) and the sustain pedal all play the synth. **Source** in the header picks Audio, MIDI or Both. With Both, a keyboard drone and your bass play together, each on its own voices. In Arp mode, held keys join the pattern.

In Ableton, Changeling is an audio effect that also listens to MIDI. Make a MIDI track, set **MIDI From** to your keyboard and **MIDI To** to the track holding Changeling (choose Changeling in the second box), and arm it.

## The Deep page

- **Oscillator 2**, taken from the Enzo X: interval, wave, level and detune. On Same and Unison it moves with oscillator 1, as on the original.
- **Tracking**: Character, lowest note on bass, sensitivity, how long a fading note holds, poly speed, octave guard.
- **Arp**: pattern (Original, Up, Down, Bounce, Played, Random), step (1/4, 1/8, 1/8T, 1/16), octaves and gate.
- **Ring + MIDI**: ring carrier Fixed (clangs against your notes) or Follow (locked to each note), velocity, bend range, mono priority, and tempo from your taps or from Ableton.

## Presets

Sixteen slots in red, yellow, green and blue banks. Tap a slot to load it, hold it to save. Presets save the sound and leave your instrument settings alone. Everything saves with your Ableton set.

Twelve slots come loaded, each matched to a passage in the pedal's demos by measuring its waveform, brightness, pitch range and note rate. The blue bank is empty for your own.

| Slot | Name | Mode | What it is |
| --- | --- | --- | --- |
| 1 | Low Saw | Mono | A warm, held saw bass with a little glide |
| 2 | Hollow Square | Mono | A bright square that slides between notes |
| 3 | Sub Octave | Mono | Dark saw with a second oscillator an octave down |
| 4 | Acid Sweep | Mono | A peaky filter that opens hard on every note |
| 5 | Glass Stabs | Poly | Soft, rounded square chords with a short echo |
| 6 | Bright Wash | Poly | Detuned saws held for as long as you hold, in long echoes |
| 7 | Soft Organ | Poly | Square with an octave on top, steady and plain |
| 8 | Touch Brass | Poly | The filter follows how hard you play |
| 9 | Sparkle Arp | Arp | Your chord as a two-octave square pattern at 246 BPM |
| 10 | High Plucks | Mono | Short square blips an octave up, with echo |
| 11 | Glide Lead | Mono | A near-pure tone an octave up with long, slow glides |
| 12 | Ring Noise | Poly | Ring mod and loose tracking, for the clangy passages |

Loading a preset shows its name beside FOOTSWITCHES for a moment.

## Footswitches and expression (optional)

Everything works from the screen, so you don't need any pedals. If you have a MIDI footswitch:

1. Set its first four switches to send **momentary CC 20, 21, 22 and 23 on MIDI channel 16** (127 when pressed, 0 when released). Or use **Learn** on the Pedal page to use whatever yours sends.
2. Set an expression pedal input to **CC 11 on channel 16**, or press Learn under the expression bar.
3. Press **Calibrate** under the expression bar, then rock the pedal from heel to toe once.

These settings are shared by every Flip Fella plugin on your computer, so you set them up once.

A and B are the Enzo's own two switches, in the same order, and holding each does that switch's second job. C steps the synth mode and D is sustain.

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | Bypass | Saw or square |
| B | Tap tempo (delay and arp) | Runaway delay while held |
| C | Next synth mode | Ring chaos while held |
| D | Sustain latch on or off | Sustain while held, like a piano pedal |
| A+B | Next preset | |
| C+D | Hand the footswitches to the next Changeling | Hold a second: save a Keeper |

Holding C+D never fires C's ring chaos or D's sustain, so a Keeper can't change the sound.

The expression pedal sweeps the **filter cutoff**, heel closed and toe open. Click the expression bar to sweep any knob instead. **Morph** brings back the original's heel-to-toe sweep of the whole patch: set the knobs, choose Morph > Set heel, set them again, choose Set toe, then pick Morph as the target.

### Getting footswitch MIDI to the plugin in Ableton

Changeling reads your footswitch straight from its USB cable, so every copy you load works with no MIDI track. The dot next to **FOOTSWITCHES** in the bottom strip turns green when it's connected.

1. In Ableton's Preferences, Link Tempo MIDI, turn **Track**, **Sync** and **Remote** off for your footswitch's input. Windows lets only one program use a MIDI input at a time, so Ableton has to let go of it.
2. Load Changeling. That's it.

If the dot stays grey, click **FOOTSWITCHES**. The menu says what's wrong, and you can pick your footswitch from the list if it wasn't found automatically.

With several copies loaded, the footswitches drive the one you opened or clicked last. Tap C+D to hand them to the next copy.

Prefer routing through Ableton? Choose **Only from a MIDI track in Ableton** in that menu, then turn **Track** back on for the footswitch, make a MIDI track with **MIDI From** set to the footswitch and **MIDI To** set to Changeling, and arm it.

### Kevin's setup (M-Vave Chocolate Plus and M-Audio EX-P)

In CubeSuite: footswitches A to D as momentary CC 20 to 23, the pedal jack set to expression input on CC 11. It always sends on channel 1 whatever CubeSuite says; read directly from its cable, Changeling accepts any channel. Put the Chocolate Plus switch on **U** and connect with the USB cable, not Bluetooth. The EX-P needs a 3.5 mm to 6.35 mm TRS adapter and reads backwards on the Chocolate Plus, which Calibrate fixes.

## Original pedal MIDI map

On the Pedal page, pick a channel under **Original pedal MIDI map**. Changeling then answers the Enzo's own CC chart and program changes on that channel, so templates made for the pedal work here: CC 4 expression, 14 bypass, 15 tempo, 16 pitch, 17 filter, 18 mix, 19 sustain, 20 filter envelope, 21 modulation, 22 portamento, 23 filter type, 24 delay level, 25 ring mod, 26 bandwidth, 27 feedback, 28 tap, 29 synth mode, 30 waveshape, 31 envelope type. Program changes 0 to 15 load the 16 slots. It is off by default, because the Enzo uses CC 20 to 23 for knobs.

## Keeper

Hold C+D for a second, or press Keep This Moment on the Pedal page. Changeling saves the last 8 seconds in and out, plus every setting, to Documents / Flip Fella Keepers.

## Building

GitHub Actions builds the Windows VST3 and the Linux ARM64 VST3 on every push and publishes both to the `builds` branch. To build locally you need CMake 3.22+ and a C++17 compiler; JUCE downloads automatically.

```
cmake -S . -B build
cmake --build build --config Release
```

`changeling_render_test` plays bass and guitar through the tracker and every mode, MIDI chords, arpeggios, the latch, ring chaos and the runaway delay, and fails on wrong notes, slow locks, retriggers, octave flips, NaNs, clipping, clicks or silence. Pass a folder to write WAV files.

`changeling_track_bench` plays modelled bass and guitar phrases (pick, pickup, bends, vibrato, slides, hammer-ons, palm mutes, hum and noise) and scores what the synth plays every 10 ms against what was really played. It fails if the right pitch sounds less than 97% of the time, on any octave error or missed note, or if chords play more than 6% wrong tones.

`changeling_foot_test` sends plain 127/0 messages and checks every footswitch tap, hold and combo does its job and nothing else, including with the original pedal map on. `changeling_foot_test --snapshot <folder>` writes the three panel pages as PNGs.
