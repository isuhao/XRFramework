#pragma once
#include <memory>
#include "wx/wx.h" 
#include "WeebTv.h"
#include "BufferedStream.h"
#include "../XRCommon/settings/AppSettings.h"
#include "../XRCommon/settings/helpers/Monitors.h"
#include "../XRFramework/render/RenderSystemDX.h"
#include "../XRFramework/core/VideoRenderers/WinRenderer.h"

class MyFrame;

class MyApp : public wxApp
{
public:
	MyApp();
	virtual ~MyApp();
	virtual bool OnInit();
	virtual int OnExit() override;
	virtual bool IsCurrentThread() const;

	std::unique_ptr<CAppSettings> m_settings;
	std::unique_ptr<CMonitors> m_monitors;

	MyFrame* m_mainFrame;
	std::shared_ptr<cRenderSystemDX> m_renderSystem;
	std::unique_ptr<WinRenderer> m_VideoRenderer;
	std::unique_ptr<CWeebTv> m_weeb;
private:
	std::unique_ptr<CBufferedStream> m_bufStream;

	DWORD m_threadId;
};

wxDECLARE_EVENT(EVT_RENDER, wxIdleEvent);

DECLARE_APP(MyApp)
#define GetAppSettings() (*static_cast<MyApp&>(wxGetApp()).m_settings.get())
#define GetAppMonitors() (*static_cast<MyApp&>(wxGetApp()).m_monitors.get())
#define GetAppDX() (static_cast<MyApp&>(wxGetApp()).m_renderSystem)