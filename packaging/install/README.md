# Installer

`build_installer.py` turns a release archive from
[packaging/release](../release/README.md) into the one-file installer that users
download:

```sh
python3 packaging/install/build_installer.py path/to/videoframevr-<version>-aarch64.tar.gz [--output DIR]
```

It prints the path of `Install-VideoFrameVR-<version>-<architecture>.desktop`.
Attach that file to the GitHub release; the in-app updater looks for that name.

The installer is a desktop entry with `installer.py` and the release archive
embedded in it, so installing needs no network. It shows its dialogs with zenity
and needs only Python 3 and zenity, which SteamOS includes. It installs to:

```
~/Applications/VideoFrameVR/
  videoframevr.sh    launcher that Steam and the menu entry run
  helper.pyz         updater, Add to Steam and Uninstall
  current -> releases/<version>-<archive hash>/
~/.local/share/applications/videoframevr.desktop
```

Steam rewrites its shortcuts file when it exits, so the installer can only add
a shortcut while Steam is stopped. That's why adding to Steam restarts it.
Problems go to `~/.local/state/videoframevr-installer.log`.
