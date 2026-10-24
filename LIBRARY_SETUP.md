# Library setup

The audio files are separate CC0 data, not GPL software. Read the original notices retained by the scripts. No accounts, payments, driver installs or proprietary agreements are required.

1. Install Python 3 and a C++23 Windows toolchain if needed. Obtain 7-Zip only from its official publisher for extracting the FreePats .7z data archives.
2. From the repository root run `python collection-work/download_sources.py --audio`. It downloads about 9.2 GB using four streaming workers and verifies every file against the supplied pinned Git tree. Interrupted completed files are verified before reuse. Electronic TX81Z and uncertain siren content are excluded. Other ambiguous raw samples are inspected but excluded by the mapping generators.
3. Run `python collection-work/download_freepats.py`. Extract each resulting archive into `collection-work/library/FreePats/source`, preserving its top-level folder. These are data-only archives; do not execute any pack content. The supplied archive manifest records SHA-256 and official origins. Existing original Spanish guitar assets can also be used.
4. The official SFZ metadata in `collection-work/vsco-sfz/source` and `collection-work/vcsl-sfz/source` is provided for mapping. Do not execute their original scripts. Run our `python collection-work/prepare_vsco.py --finalize`, `python collection-work/prepare_vcsl.py`, `python collection-work/prepare_freepats.py`, then `python collection-work/aggregate_coverage.py`.
5. Build the application. Run `bin/library_test.exe collection-work/library collection-validation` for complete silent decode/render validation. No audio devices are opened by this test.
6. Put the resulting `library` folder beside `WorldHeritageInstrumentCreator.exe`. Open Library... in the application, choose a preset and review its provenance. Only explicitly audition after you want sound.

Use only presets the converters emitted successfully. Missing/hash-mismatched sources are errors, never silent placeholders. The collection coverage manifest separates distinct acoustic instrument types from articulations and duplicate presets. These conversions are not full SFZ emulation: source loops, keyswitches, release triggers and some envelope/velocity features remain documented gaps. Source WAV bytes are preserved.

The master raw inventory is broader than the app's mapped acoustic collection. For a compact redistribution, use COLLECTION-COVERAGE.json/source_files plus preset files and all relevant license/readme/mapping documentation. Do not include excluded raw sounds merely because they were downloaded. Preserve original licenses.
