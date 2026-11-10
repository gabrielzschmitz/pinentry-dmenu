# pinentry-dmenu

<img align="right" width="192px" src="./pinentry-dmenu.svg" alt="dmenu Logo">

<a href="./LICENSE"><img src="https://img.shields.io/badge/license-GPLv2-blue" alt="License"></a>
<a href="https://www.buymeacoffee.com/gabrielzschmitz" target="_blank"><img src="https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png" alt="Buy Me A Coffee" style="height: 20px !important;width: 87px;" ></a>
<a href="https://github.com/gabrielzschmitz/pinentry-dmenu"><img src="https://img.shields.io/github/stars/gabrielzschmitz/pinentry-dmenu?style=social" alt="Give me a Star"></a>

This package combines my customized [dmenu](https://github.com/gabrielzschmitz)
with [ritze/pinentry-dmenu](https://github.com/ritze/pinentry-dmenu) to
implement Pinentry support, incorporating additional patches and configuration
changes. The repository builds and installs only `pinentry-dmenu`.

---

## Features

- Minimal and lightweight pinentry menu
- Fast startup and low resource usage
- Centered window with configurable padding
- Adjustable line height and layout
- Mouse support, including motion events
- Match numbering for quick selection
- Xresources support for dynamic configuration
- Nix flake, Nix package, and development shell included

## Build & Install

<details open>
<summary><b>With Make</b></summary>

```sh
cd src/
make
sudo make install
```

</details>

<details>
<summary><b>With Nix flakes</b></summary>

Build the package and enter the development shell:

```sh
nix build
nix develop
```

**Install on NixOS**

Add the flake input:

```nix
inputs.pinentry-dmenu.url = "github:gabrielzschmitz/pinentry-dmenu";
```

Add the package to your NixOS configuration:

```nix
environment.systemPackages = [
  inputs.pinentry-dmenu.packages.${pkgs.system}.default
];
```

Rebuild:

```sh
sudo nixos-rebuild switch --flake .#nixos
```

</details>

<details>
<summary><b>With Nix (`default.nix`)</b></summary>

Build the package:

```sh
nix-build -A pinentry-dmenu
```

**Install on NixOS**

```nix
environment.systemPackages = [
  (pkgs.callPackage /path/to/pinentry-dmenu/default.nix {})
];
```

Replace `/path/to/pinentry-dmenu` with the path to your local clone, then
rebuild:

```sh
sudo nixos-rebuild switch
```

</details>

## Dmenu patches

This build includes the following patches:

- [center](https://tools.suckless.org/dmenu/patches/center/)
- [dmenu padding](https://tools.suckless.org/dmenu/patches/dmenupadding/)
- [line height](https://tools.suckless.org/dmenu/patches/line-height/)
- [lines below prompt and full width](https://tools.suckless.org/dmenu/patches/lines-below-prompt/)
- [mouse support and motion](https://tools.suckless.org/dmenu/patches/mouse-support/)
- [numbers](https://tools.suckless.org/dmenu/patches/numbers)
- [Xresources](https://tools.suckless.org/dmenu/patches/xresources)

All patch files are available in [`src/patches/`](./src/patches/).

## Configuration

Set `pinentry-program` in `~/.gnupg/gpg-agent.conf` to the installed
`pinentry-dmenu` path, then restart `gpg-agent`.

Configuration lives in `~/.gnupg/pinentry-dmenu.conf`. The window stays
centered at `min_width` from `src/config.def.h` (750px by default). Config file
cannot disable centering. Set `horizontal_padding`, `vertical_padding`,
`lineheight`, `min_lineheight`, or `lines` to adjust layout. Eight description
rows are reserved by default; descriptions bottom-align and grow window when
they need more rows. `Ctrl+w` deletes previous word; `worddelimiters` in
`src/config.def.h` controls word boundaries.

Keyboard grabbing is disabled by default so focus can move to other apps. Set
`grab_keyboard = true;` to enable it; GPG's `OPTION no-grab` still disables the
grab for that request.

**Security:** without a keyboard grab, another X11 app can steal focus and
receive passphrase keystrokes. Verify pinentry still has focus before typing.
Treat every client connected to the same X server as trusted.

### Xresources (`xrdb`)

Pinentry uses its own `pinentry-dmenu.*` Xresources. Original `dmenu.*`
resources stay untouched for dmenu. Dmenu highlight colors map to prompt/error
colors. Config-file values take precedence. Centering stays enabled.
Border foreground color means focused; border background color means unfocused.

Add your preferred settings to `~/.Xresources`, for example:

```xresources
! Pinentry dmenu options
pinentry-dmenu.font:          SFMono Nerd Font:antialias=true:autohint=true:pixelsize=20
! pinentry-dmenu.prompt:        PROMPT
pinentry-dmenu.lines:         0

! Pinentry colors
pinentry-dmenu.background:    #14171c
pinentry-dmenu.foreground:    #abb2bf
pinentry-dmenu.backgroundSelect: #61afef
pinentry-dmenu.foregroundSelect: #14171c
pinentry-dmenu.backgroundPrompt: #61afef
pinentry-dmenu.foregroundPrompt: #14171c
pinentry-dmenu.backgroundDesc: #181b21
pinentry-dmenu.foregroundDesc: #abb2bf
pinentry-dmenu.backgroundError: #be5046
pinentry-dmenu.foregroundError: #181b21
pinentry-dmenu.borderpx:       3
pinentry-dmenu.focusedBorder: #abb2bf
pinentry-dmenu.unfocusedBorder: #282c34
```

Load the settings with `xrdb`:

```sh
xrdb -merge ~/.Xresources
```

## GnuPG integration

The frontend displays Assuan titles, errors, repeat errors, custom action
labels as full-width dmenu-style rows below prompt, one-button messages, and
live passphrase-quality feedback. Action rows and the selected-action count
appear only when GPG supplies matching button options. Keyboard actions remain
available without configured button labels. Mouse hover selects actions;
left-click activates them, middle-click pastes selection, and right-click
cancels. `SETKEYINFO`, `GETINFO`, and `CLEARPASSPHRASE` remain handled by
bundled pinentry core. Window title uses UTF-8 `_NET_WM_NAME`; window
advertises the provided PNG icon through `_NET_WM_ICON`. Both WM_CLASS instance
and class are `pinentry-dmenu` for compositor rules such as picom opacity.

Run `cd src && ./test 4` or `./test 5` for multiline descriptions; `./test 6`
for custom confirmation buttons; `./test 7` for a one-button message; `./test 8`
for an error; `./test 9` for timeout; `./test 10` for repeated entry;
`./test 11` for empty cache-clear arguments; or `./test 12` for escaped
button-label underscores.

## License

This project is licensed under the GPLv2 License. See the [LICENSE](./LICENSE)
file for details.
