#pragma once
#include "esp_err.h"
typedef void *usb_phy_handle_t;
enum {USB_PHY_CTRL_OTG=1,USB_PHY_TARGET_UTMI=2,USB_OTG_MODE_DEVICE=3,USB_PHY_SPEED_HIGH=4};
typedef struct {int controller,target,otg_mode,otg_speed;const void *otg_io_conf,*ext_io_conf;} usb_phy_config_t;
esp_err_t usb_new_phy(const usb_phy_config_t *,usb_phy_handle_t *);
esp_err_t usb_del_phy(usb_phy_handle_t);
