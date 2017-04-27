#ifndef UBLOX_LOW_LEVEL_API_H
#define UBLOX_LOW_LEVEL_API_H

#ifdef __cplusplus
extern "C" {
#endif

void ublox_mdm_init(void);

void ublox_mdm_power_on(int usb);

void ublox_mdm_power_off(void);

#ifdef __cplusplus
}
#endif

#endif // UBLOX_LOW_LEVEL_H
