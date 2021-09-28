#include "operation/kernel.h"


namespace redshow
{
    struct Trace
    {
        Kernel kernel;

        Trace() = default;

        virtual ~Trace() = 0;
    };
}