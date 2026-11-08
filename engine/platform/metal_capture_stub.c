#include "metal_capture.h"

#include "core/log.h"

bool metal_capture_begin(const char *name)
{
    (void)name;
    mg_log_warn("metal capture: Apple platforms only");
    return false;
}

void metal_capture_end(void)
{
}
