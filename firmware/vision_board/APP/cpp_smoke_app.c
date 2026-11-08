#include "cpp_smoke_app.h"

#include <rtthread.h>

static int cpp_smoke_cmd(int argc, char **argv)
{
    int32_t value;

    (void)argc;
    (void)argv;

    value = cpp_smoke_add_from_cpp(10, 20);
    rt_kprintf("[cpp] smoke result=%d, expected=37\n", (int)value);

    if (value == 37)
    {
        rt_kprintf("[cpp] C to C++ call ok\n");
        return 0;
    }

    rt_kprintf("[cpp] C to C++ call failed\n");
    return -1;
}

MSH_CMD_EXPORT(cpp_smoke_cmd, verify C++ compile and C linkage);
