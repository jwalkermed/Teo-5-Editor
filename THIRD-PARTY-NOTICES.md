# Third-party notices

TEO-5 Editor is licensed under the GNU Affero General Public License v3.0 or later (see `LICENSE`).
It is built with, and its binaries include, the following third-party software.

| Component | Used for | Licence |
|---|---|---|
| [JUCE 8](https://juce.com) (fetched by CMake, not stored in this repository) | the whole application | used under the **AGPLv3** option of its AGPLv3 / commercial dual licence |
| Steinberg VST3 SDK (bundled with JUCE) | the VST3 plug-in | used under the **GPLv3** option of its GPLv3 / Steinberg licence, which may be combined with AGPLv3 code |
| Apple AudioUnitSDK (bundled with JUCE) | the Audio Unit | Apache 2.0 |
| Libraries bundled with JUCE, e.g. zlib, libpng, libjpeg, FLAC, Ogg Vorbis, HarfBuzz, SheenBidi | file, image, audio and text handling | zlib, BSD, IJG, Old MIT, Apache 2.0 |
| [Michroma](https://github.com/googlefonts/Michroma-font) font (`Resources/Fonts`) | panel lettering | SIL Open Font License 1.1 (`Resources/Fonts/Michroma-OFL.txt`) |

The full list of JUCE's dependencies and their licences is in `LICENSE.md` in the JUCE source
(`build/_deps/juce-src/LICENSE.md` after configuring).

Oberheim and TEO-5 are trademarks of their owner. This project is not affiliated with or endorsed by Oberheim.
