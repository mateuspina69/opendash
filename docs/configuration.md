# opendash.ini

OpenDash reads its settings from `opendash.ini` in the folder of `opendash.exe` (an `opendash.ini` in the working
directory wins over it). The file is plain `key=value` lines; lines without `=` are ignored, and a path may be
wrapped in quotes. The file does not need to exist: OpenDash creates it on the first start, when it asks for the
game's folder, and writes back the settings changed in the game's options.

## Game data

| Key | Values | Default |
| --- | --- | --- |
| `data` | `pc`: the installed PC version; `disc`: the USA PS1 disc image (work in progress, see [ps1_disc.md](ps1_disc.md)) | `pc` |
| `dat_dir` | the PC version's `dat/` folder | `C:/Program Files (x86)/Capcom/Dash2/dat` (Linux: the same path in `~/.wine/drive_c`) |
| `exe` | the PC version's `dash2.exe` (OpenDash reads its per-stage tables) | `dash2.exe` next to `dat_dir` |
| `disc` | the PS1 disc's `.cue` (its `.bin` tracks next to it); needed by `data=disc`, and it also provides the English text and voices with `data=pc` | none |
| `lang` | `jp` (the PC version's text), `en` (from the PS1 disc), or the code of a language pack in `lang_dir` | `jp`; `en` with `data=disc` |
| `lang_dir` | the folder of the language packs (one subfolder per language code) | `lang/` next to `opendash.exe` |
| `restored_voices` | `1`: a language pack's restored voices when it has them; `0`: its original voices | `1` |
| `story_save` | the save whose story state free play (F2, the stage browser) starts from: a slot number (`DASH2-NN.DAT` next to `dash2.exe`, only read), a path, or `none` | `DASH2-00.DAT` when it exists |

## Video

| Key | Values | Default |
| --- | --- | --- |
| `display` | `windowed`, `borderless`, `exclusive` | `windowed` |
| `window` | the window's size, `WxH` (pixels) | 1280x720 |
| `fullscreen_mode` | the exclusive full screen's mode, `WxH@Hz` | the desktop's |
| `aa` | `auto`, `none`, `dlss`, `fsr`, `xess`, `nis` (each upscaler only when its DLL is next to the exe and the GPU supports it; outside Windows FSR 3.1 is built from the SDK), `msaa` (4x), `ssaa` (2x2 supersampling; both on any GPU, `aa_quality` has no say) | `auto` |
| `aa_quality` | `native`, `quality`, `balanced`, `performance` | `native` |
| `vsync` | `1`: frames wait for the display's refresh; `0`: as fast as the GPU goes (tearing; the game's speed is unchanged) | `1` |
| `fog` | `remaster`, `classic`, `off` | `remaster` |
| `fog_distance` | the remaster fog's distance scale, 0.25 to 4 | `1` |
| `water` | `remaster`, `classic` (the original screen wave in the flooded rooms) | `remaster` |

## Controls

`keys`, `pad` and `mouse` hold the bindings of the key configuration screen. The game writes them when the
bindings are changed in its options; they are not meant to be edited by hand. Without them the default bindings
are used.

## Netplay

See [netplay.md](netplay.md).

| Key | Values | Default |
| --- | --- | --- |
| `net` | `off`, `host`, `join` | `off` |
| `net_port` | the host's UDP port | `27460` |
| `net_join` | the host's `ip:port`, with `net=join` | none |
| `net_name` | this player's name | none |
| `net_key` | the session key; the host refuses players with a different key | none |

## Overrides

Environment variables win over the file: `OPENDASH_DATA`, `OPENDASH_DAT_DIR`, `OPENDASH_EXE`, `OPENDASH_DISC`,
`OPENDASH_LANG` and `OPENDASH_LANG_DIR`. Command-line options win over both: `--lang`, `--aa`, `--aa-quality`, `--vsync`,
`--host`, `--join`, `--net-name` and `--net-key`.

## Examples

The PC version installed somewhere else:

```ini
dat_dir=D:/Games/Dash2/dat
exe=D:/Games/Dash2/dash2.exe
```

The PS1 disc, without the PC version:

```ini
data=disc
disc=D:/Games/Mega Man Legends 2 (USA).cue
```
