#pragma once
#include "modules/nn_result.h"

#include <common/structsize.h>

namespace nn
{

namespace nfp
{

struct AmiiboSettingsArgs
{
   UNKNOWN(0x5D);
};
CHECK_SIZE(AmiiboSettingsArgs, 0x5D);

nn::Result
GetAmiiboSettingsArgs(AmiiboSettingsArgs *args);

}  // namespace nfp

}  // namespace nn
