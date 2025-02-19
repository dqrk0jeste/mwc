setup-debug:
	meson setup --reconfigure build --prefix=/usr/local -Db_sanitize=address,undefined

setup-install:
	meson setup --reconfigure build --prefix=/usr/local --buildtype=release

clean:
	rm -rf build 2>/dev/null
