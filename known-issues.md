- (fixed*) waybar rounded corners leave black background behind instead of being transparent. maybe caused by exclusive area?
/* you need to use rgba color with alpha value less then 1, 0.999 works
- (fixed) hotpluging monitors does not work
- (fixed) zen browser does not respect the initial size the compositor sends him, idk what to do, check on map and send again?
- (fixed) mpv gets black bars on top and bottom on hyprland, but not on mwc, missing protocol?
- (fixed) when a client requests a cursor it may render two cursors for some reason
- (fixed) if mwc is run after some other compositor monitors work, but if it is the first one to be run one monitor just stays in tty, but is able to be hovered over, placed toplevels on etc. does not happen with default config tho, so the problem is somewhere in the server_handle_new_output() (mode selection probably)
- (fixed) dunst notification becomes stuck when it should be replaced by another. surely an issue on our part.
- (fixed) multiple layer surfaces taking keyboard focus probably wont work as intended, as the second one will not get the focus after the first one unmaps; this is going to be fixed in a near future, as it is not that difficult to do
- (fixed) crash caused by unused keybinds for workspaces, there is really not an efficient way to fix it
