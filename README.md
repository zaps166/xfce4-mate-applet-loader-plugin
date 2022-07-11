# Xfce4 MATE applet loader

## Description

This Xfce panel plugin can load MATE panel applets.

### Known issues
- if auto hide is enabled, panel might not hide after closing of MATE's context menu
- if auto hide is enabled, panel will auto hide if callendar window is visible
- removing applet after crash from dialog window will not remove settings
- window list applet doesn't display the handler and it's too wide

## Installation (Linux)

```sh
git clone https://github.com/zaps166/xfce4-mate-applet-loader-plugin.git
cd xfce4-mate-applet-loader
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make
sudo make install/strip
```

---

It's available in [AUR](https://aur.archlinux.org/packages/xfce4-mate-applet-loader-plugin-git)
