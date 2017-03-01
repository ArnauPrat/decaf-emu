#pragma once
#include "modules/nn_result.h"

#include <common/be_val.h>
#include <cstdint>

namespace nn
{

namespace fp
{

nn::Result
GetFriendList(void *list,
              be_val<uint32_t> *length,
              uint32_t index,
              uint32_t listSize);

}  // namespace fp

}  // namespace nn
