/*
 *   <author>     <data>      <desc>
 *   LiFenfen   2024/09/09  , add for select BDF by device-tree , bug id 7902090
 */

#ifndef _OPLUS_WIFI_H
#define _OPLUS_WIFI_H

int oplus_wifi_init(struct platform_device *plat_dev);
void oplus_wifi_deinit(void);
const char* get_oplus_wifi_bdf(void);
#endif /* _OPLUS_WIFI_H */
