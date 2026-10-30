// port_install_dlg.h -- the first start's window on Windows: its resource and controls, shared by
// hyperion.rc, which lays it out, and port_install.cpp, which runs it. Only #defines, so rc.exe
// reads it as C does.
#ifndef PORT_INSTALL_DLG_H
#define PORT_INSTALL_DLG_H

#define IDD_PORT_INSTALL 200
// the base, of the two
#define IDC_INSTALL_BATTLENET 201
#define IDC_INSTALL_CLASSIC 202
// each install's part: its name, folder, Browse and the line that says whether its files are there
#define IDC_INSTALL_BATTLENET_FOLDER 203
#define IDC_INSTALL_BATTLENET_BROWSE 204
#define IDC_INSTALL_BATTLENET_STATUS 205
#define IDC_INSTALL_CLASSIC_FOLDER 206
#define IDC_INSTALL_CLASSIC_BROWSE 207
#define IDC_INSTALL_CLASSIC_STATUS 208
#define IDC_INSTALL_BATTLENET_TITLE 209
#define IDC_INSTALL_CLASSIC_TITLE 210

#endif  // PORT_INSTALL_DLG_H
