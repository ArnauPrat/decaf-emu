#pragma once
#include <common/cbool.h>

namespace coreinit
{

/**
* \defgroup coreinit_interrupts Interrupts
* \ingroup coreinit
* @{
*/

BOOL
OSEnableInterrupts();

BOOL
OSDisableInterrupts();

BOOL
OSRestoreInterrupts(BOOL enable);

BOOL
OSIsInterruptEnabled();

/** @} */

} // namespace coreinit
