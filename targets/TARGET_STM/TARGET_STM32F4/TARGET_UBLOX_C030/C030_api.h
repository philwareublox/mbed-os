#ifndef C030_H
#define C030_H

#ifdef __cplusplus
extern "C" {
#endif

void c030_init(void);

void c030_mdm_powerOn(int usb);

void c03_mdm_powerOff(void);

void c030_gps_powerOn(void);

void c030_gps_powerOff(void);

#ifdef __cplusplus
}
#endif

#endif // C030_H
