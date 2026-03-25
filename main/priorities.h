#ifndef PRIORITIES_H
#define PRIORITIES_H

/* Supervisor (must be highest) */
#define PRIO_SUPERVISOR             24

/* WiFi layer */
#define PRIO_WIFI_SUPERVISOR        23
#define PRIO_WIFI_SERVICE           22

/* GT911 touch - responsive for UI */
#define PRIO_GT911_SUPERVISOR       15
#define PRIO_GT911_SERVICE          14

/* Display layer (for later) */
#define PRIO_DISPLAY_SUPERVISOR     10
#define PRIO_DISPLAY_SERVICE        9

#endif /* PRIORITIES_H */