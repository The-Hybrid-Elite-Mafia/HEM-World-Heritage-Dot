# 0.4.1 repair candidate

This revision addresses six user-reported problems. It does not install itself,
start audio on launch, change device preferences, or alter original recordings.

1. **Names:** library cards and the loaded title show the instrument before its
   taxonomy. Right-click a card for its full original name and preset path.
   Source filenames, metadata and saved instrument names are retained.
2. **Output:** the existing per-voice 0.25 multiplier (12.04 dB of mixing headroom)
   remains. The app now exposes a saved output trim, -24 to +12 dB, initially +6 dB
   when no output-level.conf exists. At maximum velocity this leaves approximately
   6 dB before a single unity-peak, zero-zone-gain recording reaches full scale.
   Velocity and zone gains still apply. Quiet source recordings remain relatively
   quiet; there is no normalization or automatic makeup gain. A stereo-linked
   limiter has instant attack, 50 ms recovery and a 0.98 ceiling. It changes
   overloaded passages; lower the trim if it engages frequently. The meters now
   display actual post-output peaks on a -60 to 0 dBFS scale. “Limiter used” means
   it has engaged during this process, not necessarily in the current block.
3. **Blend:** the old wet branch attenuated the entire excitation after its short
   strike window, silencing sustained samples. Continuous recorded excitation now
   feeds the resonators; the window applies only to transient emphasis. The wet
   branch includes the physical junction's feed-through plus waveguide/modal
   response. It is not an isolated resonant tail, an oscillator, or a loudness
   normalizer. Resonance can still change level and dynamics. Dry/bypass numerical
   tests and real-recording comparisons are included.
4. **Navigation:** alphabetical instrument-first order, case-insensitive search,
   family filters, Previous/Next, Page Up/Down, Home/End, and five-row wheel steps.
   High-resolution wheel deltas accumulate. Folder indexing runs in a worker;
   filtering operates only on the in-memory index. Unclassified/user packs appear
   under Other / user rather than having a guessed taxonomy.
5. **Mapping:** six labeled numeric fields, displayed MIDI pitch names, velocity
   units, alternate-take index, explicit note-on/release trigger, Save and Cancel.
   Invalid input leaves the mapping intact. A root may legitimately lie outside
   the playback range. Release mode requires a genuine mapped release recording;
   this does not create release samples.
6. **Status:** a dedicated panel distinguishes current engine connection,
   user-supplied v0.4.0 ASIO playback feedback, and offline validation. It makes
   no hardware certification or new speaker-test claim.

## Validation and compatibility

The manual-only Windows workflow verifies actual Microsoft C++23 mode and AVX2,
builds the application and runs silent engine, parameter, portability, preference,
metadata, production UI-handler, output-stage and level tests. It renders the
production canvas offscreen. See the exact commit's run logs and artifacts for
results; this document is not a claim that an unchecked commit has passed.

The complete private 402-preset collection is tested locally using library_test
and level_audit; it is not bundled in the public CI fixture. The CC0 Ganjo fixture
and its original notices are retained. Preset formats remain unchanged; legacy
DSP and sample gains remain intact. The output trim is app-specific and separate
from saved audio-driver preferences and user instruments.

Live UI interaction, speaker playback and connected MIDI hardware have not been
tested for this repair candidate. The prior installed version and user data must
remain intact until the candidate is reviewed.
