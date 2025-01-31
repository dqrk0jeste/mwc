<div align="center">
<h1>mwc</h1>
<img src="https://github.com/dqrk0jeste/mwc/blob/main/assets/mwc.gif" width="500"/>
<br>
</div>

<br>

## features
- tiling and floating toplevels
- master layout with support for multiple masters, ideal for wide monitors
- keyboard focused workflow
- great multitasking with multimonitor and workspaces support
- smooth and customizable animations
- easy configuration with custom keybinds, monitor layouts etc
- portals and an ipc for integrating with other apps

> mwc is made mainly for myself, implementing just enough for my workflow. that means a lot of things is just not there. if you are looking for something more mature take a look at hyprland, sway or river. 

## showcase
<div align="center">

<img src="assets/showcase-1.png" alt="logo" width="500">
<img src="assets/showcase-2.png" alt="logo" width="500">

</div>

## dependencies
- make *
- wayland-protocols *
- wayland
- libinput
- libdrm
- pixman
- libxkbcommmon
- wlroots 18.0 
- scenefx (latest git)

> \* compile-time dependencies

## building
```bash
git clone https://github.com/dqrk0jeste/mwc
cd mwc
make
```

## installation

> does not install this version of mwc
### arch
`mwc` is available in the arch user repository under the name `mwc-compositor-git`. you can install it with your favourite aur helper
```bash
yay -S mwc-git
```

### other linux distributions
```bash
make install
```
note: you will probably need sudo privileges to do so

> if you wish to uninstall `mwc` you can do so with `make unistall`.

## post install
if you need to interact with sandboxed applications and/or screenshare you will need xdg-desktop-portals. by default `mwc` needs
- xdg-desktop-portal (base)
- xdg-desktop-portal-wlr (for screensharing)
- xdg-desktop-portal-gtk (for everything else)

## usage
```bash
mwc
```

> you probably want to run it from a tty

## configuration
configuration is done in a configuration file found at `$XDG_CONFIG_HOME/mwc/mwc.conf` or `$HOME/.config/mwc/mwc.conf`. if no config is found a default config will be used (you need `mwc` installed, see above).

for detailed documentation see `examples/example.conf`. you can also find the default config in the repo.

## todo
- [ ] fix issues
- [x] animations
- [x] rounded corners
- [x] transparency
- [x] blur
- [x] drag and drop implementation
- [x] monitor hotplugging
- [x] complete foreign toplevel implementation (toplevel enter/leave output missing)
- [x] add portals
- [x] mouse clicks for keybinds (for moving and resizing toplevels)
- [ ] more ipc capabilities
