#include "stdafxf.h"
#include "Demux.h"

void CDemuxStream::GetStreamName(std::string& strInfo)
{
	strInfo = "";
}

AVDiscard CDemuxStream::GetDiscard()
{
	return AVDISCARD_NONE;
}

void CDemuxStream::SetDiscard(AVDiscard discard)
{
	return;
}

void CDemuxStreamAudio::GetStreamType(std::string& strInfo)
{
	char sInfo[64];

	if (codec == AV_CODEC_ID_AC3) strcpy(sInfo, "AC3 ");
	else if (codec == AV_CODEC_ID_DTS)
	{
#ifdef FF_PROFILE_DTS_HD_MA
		if (profile == FF_PROFILE_DTS_HD_MA)
			strcpy(sInfo, "DTS-HD MA ");
		else if (profile == FF_PROFILE_DTS_HD_HRA)
			strcpy(sInfo, "DTS-HD HRA ");
		else
#endif
			strcpy(sInfo, "DTS ");
	}
	else if (codec == AV_CODEC_ID_MP2) strcpy(sInfo, "MP2 ");
	else if (codec == AV_CODEC_ID_TRUEHD) strcpy(sInfo, "Dolby TrueHD ");
	else strcpy(sInfo, "");

	if (iChannels == 1) strcat(sInfo, "Mono");
	else if (iChannels == 2) strcat(sInfo, "Stereo");
	else if (iChannels == 6) strcat(sInfo, "5.1");
	else if (iChannels == 8) strcat(sInfo, "7.1");
	else if (iChannels != 0)
	{
		char temp[32];
		sprintf(temp, " %d%s", iChannels, "-chs");
		strcat(sInfo, temp);
	}
	strInfo = sInfo;
}

