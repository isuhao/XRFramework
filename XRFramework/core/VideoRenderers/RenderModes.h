#pragma once

//Thats all for now, i'm not expecting to implement more before leaving beta stage

enum ERenderFormat {
	RENDER_FMT_NONE = 0,
	RENDER_FMT_YUV420P,
	RENDER_FMT_YUV420P10,
	RENDER_FMT_YUV420P16,
	RENDER_FMT_NV12,
	RENDER_FMT_UYVY422,
	RENDER_FMT_YUYV422,
	/*RENDER_FMT_DXVA,
	RENDER_FMT_OMXEGL,
	RENDER_FMT_CVBREF,
	RENDER_FMT_BYPASS,
	RENDER_FMT_EGLIMG,
	RENDER_FMT_MEDIACODEC,
	RENDER_FMT_IMXMAP,
	RENDER_FMT_MMAL,
*/
};

// Render Methods
enum ERenderMethod
{
	RENDER_INVALID = 0x00,
	RENDER_PS = 0x01,
	RENDER_SW = 0x02,
	RENDER_AUTO = 0x03,
};

enum EScalingMethod
{
	VS_SCALINGMETHOD_NEAREST = 0,
	VS_SCALINGMETHOD_LINEAR,

	VS_SCALINGMETHOD_CUBIC,
	VS_SCALINGMETHOD_LANCZOS2,
	VS_SCALINGMETHOD_LANCZOS3_FAST,
	VS_SCALINGMETHOD_LANCZOS3,
	VS_SCALINGMETHOD_SINC8,
	VS_SCALINGMETHOD_NEDI,

	VS_SCALINGMETHOD_BICUBIC_SOFTWARE,
	VS_SCALINGMETHOD_LANCZOS_SOFTWARE,
	VS_SCALINGMETHOD_SINC_SOFTWARE,
	VS_SCALINGMETHOD_VDPAU_HARDWARE,
	VS_SCALINGMETHOD_DXVA_HARDWARE,

	VS_SCALINGMETHOD_AUTO,

	VS_SCALINGMETHOD_SPLINE36_FAST,
	VS_SCALINGMETHOD_SPLINE36,

	VS_SCALINGMETHOD_MAX // do not use and keep as last enum value.
};