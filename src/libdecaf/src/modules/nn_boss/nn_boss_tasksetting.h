#pragma once
#include "modules/nn_result.h"
#include "modules/coreinit/coreinit_ghs_typeinfo.h"
#include "nn_boss_titleid.h"

#include <common/be_ptr.h>
#include <common/structsize.h>
#include <cstdint>

namespace nn
{

namespace boss
{

class TaskSetting
{
public:
   static ghs::VirtualTableEntry *VirtualTable;
   static ghs::TypeDescriptor *TypeInfo;

public:
   TaskSetting();
   ~TaskSetting();

   void
   InitializeSetting();

   void
   SetRunPermissionInParentalControlRestriction(bool value);

   nn::Result
   RegisterPreprocess(uint32_t, nn::boss::TitleID *id, const char *);

   void
   RegisterPostprocess(uint32_t, nn::boss::TitleID *id, const char *, nn::Result *);

protected:
   char mTaskSettingData[0x1000];
   be_ptr<ghs::VirtualTableEntry> mVirtualTable;

protected:
   CHECK_MEMBER_OFFSET_START
      CHECK_OFFSET(TaskSetting, 0x00, mTaskSettingData);
      CHECK_OFFSET(TaskSetting, 0x1000, mVirtualTable);
   CHECK_MEMBER_OFFSET_END
};
CHECK_SIZE(TaskSetting, 0x1004);

}  // namespace boss

}  // namespace nn
