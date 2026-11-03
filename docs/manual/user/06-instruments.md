# 6. Tracks and instruments (VST3)

**You will:** give each KeyStep track a VST3 instrument and hear the sequencer through your computer.

```mermaid
flowchart LR
  K["KeyStep notes by USB, channels 1-4"] --> I1[T1 VST3]
  K --> I2[T2 VST3]
  K --> I3[T3 VST3]
  K --> I4[T4 VST3]
  I1 --> FX[Inserts per track]
  I2 --> FX
  I3 --> FX
  I4 --> FX
  FX --> M[Master chain] --> L[Limiter] --> O[Audio out]
```

## One time: scan your plugins

**Scan plugins** checks your VST3 folders one file at a time in a separate process, so a plugin that crashes cannot
take the app down. The first scan takes a few minutes. **Plugins...** lists the results (type in the box to filter by name or maker); tick **Hide** on the ones you
never want offered (they are hidden, not deleted; **Show hidden** brings them back into view).

![The VST3 plugins window with the Hide column](img/shot-plugins.png)

## Per track

Above the grid there are four cards, one per track, each in the track's own color (T1 green, T2 orange, T3 yellow,
T4 red) so you can tell tracks apart at a glance. The card of the selected track is full color with a thick white border; the
others are dimmed. Click anywhere on a card (its controls too) to select its track; the pointer turns into a hand over the cards you can select. Each card has:

- a dot with the track number and the instrument list,
- a vertical volume fader on the left (hover it for the level in dB),
- **Seq** and **Arp** keys (**Seq** and **Drum** on track 1), lit for the mode the track is in; see the grid chapter,
- a key with three little faders, which opens the plugin's own window,
- **M**, which mutes (lights red), and **S**, which solos (lights blue): while any track is soloed only the soloed tracks sound (you can solo
  several).

Notes arrive on the track's MIDI channel (T1 = 1 ... T4 = 4, the KeyStep defaults; drums on 10). When the project sets its own
channel for a track (**Project Map**, the T1-T4 channel boxes), the instrument listens on that one. **Audio...** picks the output
device; **Test note** plays a C3 on every loaded instrument without the KeyStep.

**FX...** opens four insert slots per track and a master chain of four; each has **Edit** and **Byp** (bypass). A master
limiter sits at the end. The label under the rows shows the output peak, limiter reduction and the BPM read from the
KeyStep's clock.

![Track cards with an instrument loaded; the plugin key opens its window](img/shot-instruments.png)

**Watch out:** a plugin missing on this machine shows **Missing: name** and is kept untouched in the project.
Plugin latency is not compensated yet.
