# Changeling feature map

Written 4 November 2026. Changeling is a VST3 recreating the Meris Enzo multi-voice instrument synthesizer in the Flip Fella family look (Speckle, Jetstream, Murmur). Prism Patch is the reference for foot control, Keeper and the Pedal page.

## Enzo or Enzo X?

Meris released the **Enzo X** in February 2025 (current firmware v1.5.1, July 2026). Over the original it adds:

| Enzo X adds | Detail |
| --- | --- |
| Six voices, dual independent oscillators | Each oscillator has its own wave, tuning and level. The original's two oscillators move together, with Modulation detuning them |
| Fifth mode, Dry Poly | Polyphonic pitch shifting of the dry signal, taken from the LVX's Poly Chroma. The original Dry mode is mono pitch shift |
| Drive | Five types, including Bitcrush |
| Modulation | Five types, including Meris's own flanger and phaser |
| Delay | Full stereo, up to 2.5 seconds, with divisions per side |
| Reverb | Three reverbs taken from the Mercury X |
| Modifiers | Envelopes, LFOs and a footswitch-held "Hold Modifier" routed to any parameter |
| MIDI notes | Notes play the synth in Mono, Poly and Arp. Audio or MIDI drives the synth, never both at once |
| Tuner, presets, I/O | Tuner with adjustable reference, 99 presets in 33 banks, stereo in and out, the LVX screen and menus |

Owners report tracking that is no better on bass than the original's: good in mono down to about F#, octave-hopping on a low E.

**Recommendation: recreate the original Enzo, and borrow three things from the X.**

- What you listed (Mono, Arp, Poly and Dry, the alt knobs, filter envelope, ring mod, delay, glide) is the original's panel, and its glitchy tracking and ring-mod chaos are what people love.
- The X's extra drive, modulation and reverb overlap what Prism Patch, Murmur and Speckle already do. Put Changeling before them in a chain instead.
- From the X, take MIDI note input (and let audio and MIDI play together, which the X can't do), independent oscillator 2 tuning and wave on the Deep page, and the tuner.

## Feature map

"Alt" is the original's hidden function: hold ALT and turn the knob or press the footswitch. Changeling shows every alt control on screen beside its main control, so nothing is hidden. An ALT button still swaps the knobs in place for anyone who wants the pedal's feel.

### Synth modes

| Original | What it does | Changeling |
| --- | --- | --- |
| Poly | Polyphonic tracking. Each note you play gets its own synth voice | Up to 6 voices. A spectrum of the audio since the strum, scored by harmonic sum with each found note's overtones removed, finds chord notes and holds them; the fast mono tracker leads single notes. MIDI notes take free voices |
| Mono | One voice, two oscillators, monophonic tracking. The tightest mode | The new low-latency tracker (see Pitch tracking). Last-note priority from MIDI, with legato glide |
| Arp | Turns the held chord into a pattern at the tap tempo. Owners report it is fixed at quarter notes of the tap | Same, locked to tap or host tempo. Original pattern and quarter notes by default, with subdivision and pattern on the Deep page (see Questions) |
| Dry | Synth off. Pitch shifts the dry signal, Sustain compresses it, Modulation modulates the delay | Same. Filter, envelope, ring mod and delay all still work on the dry signal |
| Synth Mode button | Steps Poly, Mono, Arp, Dry | A flip card on screen. Footswitch C steps it |

### Knobs and their alt functions

| Original knob | What it does | Alt function | Changeling |
| --- | --- | --- | --- |
| Pitch | Synth or dry pitch in semitones | **Portamento**: glide between synth notes | Pitch knob with a semitone flip card (−24 to +24), Glide beside it. Glide also follows MIDI legato |
| Filter | Cutoff | **Filter Type**: ladder low-pass, ladder band-pass (shelving), ladder high-pass, state-variable low-, band- and high-pass | Cutoff knob, six-way type pills, and a live curve that shows the envelope moving it |
| Mix | Dry and synth balance | **Delay Level**: one delay tap, Min to Mid | Mix knob, Delay level beside it |
| Sustain | Synth note sustain. Compresses the input in Dry | **Ring Modulation**: frequency of a classic ring modulator | Sustain knob, Ring mod beside it. Fully left is off |
| Filter Env | Envelope amount on the cutoff | **Filter Bandwidth**: wide and gentle to narrow and peaky (resonance) | Env amount knob, Bandwidth beside it |
| Modulation | Detunes the two oscillators of each voice. Delay modulation in Dry | **Delay Feedback** | Modulation knob, Feedback beside it |

### Footswitches on the original

| Original switch | Press | Hold | Alt + press |
| --- | --- | --- | --- |
| Bypass | Bypass on or off | (not documented in sources I could reach) | **Synth Waveshape**: sawtooth or square |
| Tap | Tap tempo for delay and arp | Delay feedback to maximum until you let go | **Envelope Type**: triggered envelope or envelope follower |

Changeling has both as on-screen buttons and gives every alt function its own on-screen control: Saw/Square chips and Trigger/Follow chips.

### Global settings on the original

| Original | Changeling |
| --- | --- |
| Input level, instrument or line | Input chips: Guitar, Bass, Synth/Line. Bass changes the tracker (see below) |
| Trails | Trails chip: bypass lets the delay ring out |
| Expression jack: expression, TRS switches or preset switch | Handled by the Pedal page |
| MIDI channel | "Original pedal MIDI map" channel on the Pedal page, off by default |
| Presets: 4 on the pedal (with the Meris preset switch), 16 by MIDI program change | 16 slots in red, yellow, green and blue banks. Tap to load, hold to save. Saved with the Ableton set |
| Expression heel and toe positions stored for every knob | Expression target defaults to Filter cutoff. A Morph option on the Pedal page brings back the original heel-to-toe snapshot of every knob |

### MIDI on the original

The CC chart follows Meris's family layout. CC 17 Filter and CC 28 Tap are confirmed by owners on forums. The rest follow the family pattern and must be checked against the manual before release, because meris.us was blocked from this research session.

| CC | Original | CC | Original |
| --- | --- | --- | --- |
| 4 | Expression | 22 | Portamento |
| 14 | Bypass | 23 | Filter Type |
| 15 | Tempo (10 ms steps) | 24 | Delay Level |
| 16 | Pitch | 25 | Ring Modulation |
| 17 | Filter | 26 | Filter Bandwidth |
| 18 | Mix | 27 | Delay Feedback |
| 19 | Sustain | 28 | Tap (127 = tap) |
| 20 | Filter Envelope | 29 | Synth Mode |
| 21 | Modulation | 30, 31 | Waveshape, Envelope Type |

CC 20 to 23 collide with the house footswitch defaults, so this chart lives behind the opt-in "Original pedal MIDI map" channel, exactly as in Prism Patch. Program changes 0 to 15 load the 16 slots on that channel.

## New in Changeling

### MIDI keyboard

- **Source** chips: Audio, MIDI, or Both (the default). With Both, MIDI notes take free voices and audio tracking keeps the rest, so you can hold a keyboard drone and play bass over it.
- Velocity sets the envelope level and filter amount. Pitch bend is ±2 semitones. Mod wheel adds Modulation. Sustain pedal (CC 64) holds notes, the same as footswitch D.
- In Mono, MIDI uses last-note priority with legato glide. In Arp, held MIDI notes join the chord.
- Notes, pitch bend and program changes arrive on any channel. Footswitch and expression CCs on their learned channel are consumed by the Pedal code first, per the house rules.

### Pitch tracking for bass, guitar and synth

The original tracks well on guitar but lags and retriggers below the guitar register. Owners report trouble below A on the E string, octave jumps on low E, and the loudest harmonic being tracked instead of the fundamental. Changeling uses a tracker built for the low end, with a Character control that brings the original's chaos back on purpose.

- **Input chips set the range.** Bass: 28 Hz (drop A on a 5-string) to 1 kHz. Guitar: 70 Hz to 1.5 kHz. Synth/Line: full range.
- **Short-window lock.** The input is decimated to about 8 kHz and a normalised square difference (McLeod) is computed every 2 ms over a window of a third of each candidate period, and a note is called once three readings in a row agree: measured about 45 ms on bass (54 ms on low B) and about 20 ms on guitar, scored against a modelled instrument in `changeling_track_bench`. One cycle is the physical floor, and any tracker has to wait for it.
- **No octave hopping.** Before trusting a period, 2, 3 and 4 times it are checked too, so the fundamental wins when the second harmonic is louder, which is common on bass with the neck pickup and on a P Bass. While a note holds, the tracker stays with the peak nearest it.
- **No retriggers on decay.** Hysteresis on level and pitch keeps one note while it fades. A new note needs a fresh pluck or a real pitch change of more than 0.6 semitones.
- **Character: Tight to Wild** (Deep page). Tight is the new tracker. Wild brings back the original's glitches: octave flips on decay, grabbing the loudest harmonic, retriggers, and slow poly attack. Ring mod reacts to those glitches, so the chaos stays musical.
- **Tracking readout.** Note name, cents and a confidence bar in the strip, so you can see what it heard.

### Ring mod chaos

The ring modulator runs after the synth, as on the original, with a fixed carrier by default. That fixed carrier is what makes it clang inharmonically against tracked notes. A Follow option on the Deep page locks the carrier to the tracked pitch for bell-like intervals (see Questions).

### Tuner

Taken from the Enzo X. Click the tracking readout to open it. It mutes the output, as on the pedal.

## Foot control

Defaults follow the house rules: footswitches A to D send momentary CC 20, 21, 22, 23 and expression sends CC 11, all on channel 16, read straight from the footswitch's USB cable. The shared `PedalControl` module and `Flip Fella Pedals.xml` settings come from Prism Patch unchanged, so the M-Vave Chocolate Plus and M-Audio EX-P work with no mapping.

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | Bypass | Saw or square (the original's Alt + Bypass) |
| B | Tap tempo (delay and arp) | Delay feedback to maximum while held (the original's Tap hold) |
| C | Next synth mode: Poly, Mono, Arp, Dry | Ring mod chaos while held |
| D | Sustain latch on or off | Sustain while held, like a piano pedal |
| A+B | Next preset | |
| C+D | Hand the footswitches to the next Changeling | Keeper (hold 1 second) |

Why these:

- **A and B are the original's two switches**, in the original's order, and each hold does the switch's own second job. Your muscle memory from the pedal carries over.
- **C is synth mode** because it's the change you'd most want mid-song (chords in Poly, bass line in Mono, Arp for a section), and the original makes you bend down to the button. Holding C throws in ring mod chaos while held, for a glitch fill that always comes back clean.
- **D is sustain**, as you suggested, because it turns Changeling into a drone or pad under whatever you play next. That's the thing a synth pedal can't do without a third switch. Tap latches it, hold makes it momentary.
- **Keeper stays on C+D held 1 second.** No clash: a combo never fires the separate C or D holds, so Keeper can't turn ring mod on or latch sustain by accident.
- **Envelope Type** (Trigger or Follow) moves to the screen and Learn. It's a sound-design choice, not a live one.

**Expression: Filter cutoff**, heel closed and toe open. Of everything on the panel, cutoff sweeps best live. It works like a wah on single notes, opens and closes pads in Poly, and sounds great on arpeggios. On the Pedal page it can target any knob, or use Morph to sweep the whole patch between two snapshots as the original did.

Pebble taps its foot on every press and leans with the expression pedal.

## Portability

- Windows x64 VST3 and standalone from GitHub Actions on every push to `main`, published to the `builds` branch like the other plugins.
- Linux ARM64 VST3 and standalone for the Pi pedalboard, built natively on GitHub's ARM64 runner and published to the same branch.
- The engine is plain C++17 with no JUCE, so the render test (bass phrases from low B up, guitar chords, MIDI arps, footswitch and expression events) runs on both platforms in CI.
- The DSP is light: six voices, both trackers, ring mod and delay together measure about 2% of one desktop core, so a Pi 5 has plenty of headroom.

## Decisions (4 November 2026)

- Recreate the original Enzo, with MIDI notes, oscillator 2 and the tuner from the Enzo X. No Enzo X drive, modulation or reverb.
- Bass tracking reaches low B (a 5-string is on the way).
- Arp: the original's quarter notes by default, pattern and step on the Deep page.
- Ring mod: fixed carrier by default, Follow on the Deep page.
- Source defaults to Both.
- Linux ARM64: VST3 and standalone.
- Footswitch layout approved as above.

## Sources

- Meris Enzo product page and quick-start guide (alt functions, filter types)
- Sound On Sound, MusicTech, Waveform Magazine and Bonedo reviews
- TalkBass and VGuitarForums threads on Enzo and Enzo X bass tracking
- Morningstar and MOD Audio forum threads on Enzo MIDI (CC 17, CC 28, quarter-note arp)
- Enzo X manual v1.0.3 and v1.5.1 summaries, Guitar World, MusicRadar, Synth Anatomy, FutureMusic
