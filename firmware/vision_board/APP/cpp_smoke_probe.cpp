#include "cpp_smoke_app.h"

namespace
{
class SmokeCalculator
{
public:
    int32_t add_with_bias(int32_t a, int32_t b) const
    {
        return a + b + kBias;
    }

private:
    static const int32_t kBias = 7;
};
}

extern "C" int32_t cpp_smoke_add_from_cpp(int32_t a, int32_t b)
{
    SmokeCalculator calc;
    return calc.add_with_bias(a, b);
}
