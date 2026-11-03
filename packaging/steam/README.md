# Steam artwork

Original VideoFrameVR artwork matching the desktop icon in [`../videoframevr.svg`](../videoframevr.svg).

| File | Size | Use |
| --- | --- | --- |
| `cover.png` | 600 × 900 | Portrait library tile |
| `wide.png` | 920 × 430 | Wide recent-game tile |
| `hero.png` | 1920 × 620 | Library page background, without a title |
| `logo.png` | 1000 × 240 | Transparent title/logo over the background |
| `icon-32.png` | 32 × 32 | Small shortcut icon |
| `icon-256.png` | 256 × 256 | Steam shortcut icon |
| `icon-512.png` | 512 × 512 | High-resolution app icon |

Normal installs and AppImages include the PNGs in `share/videoframevr/steam/`.
Raster app icons are also installed in the standard `hicolor` icon directories,
alongside the scalable SVG. No artwork-generation tools run during a build.

## Add to Steam

1. In Steam, choose **Games → Add a Non-Steam Game**. Select the installed
   VideoFrameVR application, or browse to the native `build/videoframevr` executable.
   A permanent AppImage path works too.
2. Set the shortcut name to `VideoFrameVR`. Leave forced compatibility/Proton off.
   Enable **Include in VR Library** if the client offers it.
3. In the shortcut's Properties, click the icon and select `icon-256.png`.
4. In the library grid, right-click its portrait tile and choose
   **Manage → Set custom artwork**, then select `cover.png`.
5. Launch and exit once so its wide recent-game tile appears. Set that tile's
   custom artwork to `wide.png` in the same way.
6. Open its library page. Right-click the banner to set `hero.png` as the custom
   background and `logo.png` as the custom logo. Steam also offers logo positioning
   there; the background leaves room on the left for the title.

If the shortcut icon stays blank after selecting the PNG, fully exit Steam using
**Steam → Exit**, then reopen it. Linux client reports describe icons only
refreshing after a restart, even when the selected path was saved successfully.
Closing the Steam window is not enough. See [Valve's issue tracker discussion](https://github.com/ValveSoftware/steam-for-linux/issues/11799#issuecomment-3048020464).

The Linux executable does not contain a Windows-style embedded icon. The desktop
entry supplies its application-menu icon; Steam can use the exported PNG when
adding the executable directly. The artwork is local to the Steam installation,
not a Steam store listing.

![Wide Steam artwork](wide.png)
