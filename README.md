# Burnout 3: Takedown — PAL Enhanced

**European PAL adaptation of the unofficial Burnout 3: Takedown PC port.**

A community-driven project dedicated to bringing the European PlayStation 2 version of Burnout 3: Takedown to modern Windows PCs using native recompilation.

Based on the original [burnout3-pc](https://github.com/danielgomesvieira2000/burnout3-pc) project by Daniel Gomes Vieira.

## Development Status

**Work in progress — experimental playable builds available locally.**

Recent development checkpoints report native VU compilation,
save functionality and movie skipping.

These capabilities require further validation before a
public release. Game compatibility is not yet guaranteed.

The PAL version is currently being adapted to the existing PC port architecture. Significant compatibility and startup work remains.

This repository is intended for development, research, documentation and collaboration.

## Objectives

- Support the European PAL release (SLES-52584).
- Preserve the original European language selection.
- Support Spanish and other PAL localizations.
- Restore region-specific functionality, including the original 50/60 Hz selection sequence.
- Maintain compatibility with applicable upstream PC improvements.
- Preserve the original game logic and experience.
- Explore additional PC enhancements and customization.

## Current Progress

Initial development has focused on:

- European executable analysis.
- PAL-specific recompilation configuration.
- Initial Spanish localization integration.
- Investigation of the startup sequence.
- Identification of missing PAL boot-flow handling.
- Development of reproducible validation procedures.

These items represent ongoing research and integration work, not completed game compatibility.

## Technical Approach

The project uses the original European MIPS executable as the reference for PAL-specific game behaviour.

Game logic and state validation are kept separate from graphics validation, since the original PC port contains rendering improvements that may differ visually from the PlayStation 2 release.

Where practical, local scripts and reproducible tooling are preferred over manual processes.

## Planned Enhancements

Long-term ideas under investigation include:

- Custom soundtrack support and music replacement tools.
- Improved configuration and localization.
- Additional display and rendering options.
- Higher frame rates, subject to engine compatibility.
- Further gameplay and quality-of-life improvements.

These features are not currently implemented or guaranteed.

## Documentation

Technical research, development status and validation methodology are maintained in `docs/pal/`.

## Credits

**Original PC port:** [Daniel Gomes Vieira](https://github.com/danielgomesvieira2000)

**PAL adaptation:** [JMPliskin](https://github.com/JMPliskin)

Special thanks to the contributors of the upstream project and the PS2 recompilation toolchain.

Please refer to the original repository for the full list of contributors and third-party acknowledgements.

## Legal Notice

This is an unofficial, non-commercial fan project and is not affiliated with Electronic Arts or Criterion Games.

This repository is not intended to distribute original game assets, disc images, extracted game executables or generated game-derived code.

Users must provide their own legally obtained copy of Burnout 3: Takedown.

Burnout is a trademark of Electronic Arts Inc.

## License

The original project's source code is distributed under the MIT License. Existing copyright notices and third-party licenses are retained.

See [LICENSE](LICENSE) for details.