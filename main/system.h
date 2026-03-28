#ifndef SYSTEM_H
#define SYSTEM_H

#include "supervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

void wifi_supervisor(void *arg);

extern const service_def_t services[];

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_H */