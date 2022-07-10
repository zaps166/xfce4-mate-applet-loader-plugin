// SPDX-License-Identifier: MIT

#include <libxfce4panel/libxfce4panel.h>

extern void mateappletloader_construct(XfcePanelPlugin *plugin);

XFCE_PANEL_PLUGIN_REGISTER(mateappletloader_construct)
