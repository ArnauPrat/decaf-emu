#pragma once
#include "nn_olv.h"
#include <common/be_val.h>
#include <common/structsize.h>

namespace nn
{

namespace olv
{

class DownloadedTopicData
{
public:
   DownloadedTopicData();

   uint32_t
   GetCommunityId();

   uint32_t
   GetUserCount();

protected:
   be_val<uint32_t> mUnk1;
   be_val<uint32_t> mCommunityId;
   UNKNOWN(0xFF8);

private:
   CHECK_MEMBER_OFFSET_START
   CHECK_OFFSET(DownloadedTopicData, 0x00, mUnk1);
   CHECK_OFFSET(DownloadedTopicData, 0x04, mCommunityId);
   CHECK_MEMBER_OFFSET_END
};
CHECK_SIZE(DownloadedTopicData, 0x1000);

}  // namespace olv

}  // namespace nn
