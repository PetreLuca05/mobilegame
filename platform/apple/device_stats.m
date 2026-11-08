#import <Foundation/Foundation.h>
#include <mach/mach.h>

#include "platform/device_stats.h"

int device_thermal_state(void) { return (int)[[NSProcessInfo processInfo] thermalState]; }

float device_resident_mb(void)
{
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) {
        return 0.0f;
    }
    return (float)info.phys_footprint / (1024.0f * 1024.0f);
}
