#ifndef UBLOX_LOW_LEVEL_H
#define UBLOX_LOW_LEVEL_H

#ifdef __cplusplus
extern "C" {
#endif

void ublox_mdm_init(void);

void ublox_mdm_power_on(int usb);

void ublox_mdm_power_off(void);

void ublox_gps_power_on(void);

void ublox_gps_power_off(void);

#ifdef __cplusplus
}
#endif

#endif // UBLOX_LOW_LEVEL_H
