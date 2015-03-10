/*
*      Copyright (C) 2005-2013 Team XBMC
*      http://xbmc.org
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with XBMC; see the file COPYING.  If not, see
*  <http://www.gnu.org/licenses/>.
*
*/
#include "stdafxf.h"
#include "DllLibCurl.h"
#include "../XRThreads/SingleLock.h"
#include "../XRThreads/SystemClock.h"
#include "log/log.h"


#include <assert.h>

bool DllLibCurlGlobal::Load()
{
	XR::CSingleLock lock(m_critSection);
	if (m_curlReferences > 0) 
	{
		m_curlReferences++;
		return true;
	}

	if (curl_global_init(CURL_GLOBAL_ALL)) 
	{
		LOGERR("Error initializing libcurl");
		return false;
	}

	/* check idle will clean up the last one */
	m_curlReferences = 1;

	return true;
}

void DllLibCurlGlobal::Unload()
{
	XR::CSingleLock lock(m_critSection);

	if (--m_curlReferences == 0)
		curl_global_cleanup();  // close libcurl

}

void DllLibCurlGlobal::CheckIdle()
{
	/* avoid locking section here, to avoid stalling gfx thread on loads*/
	if (m_curlReferences == 0)
		return;

	XR::CSingleLock lock(m_critSection);
	/* 20 seconds idle time before closing handle */
	const unsigned int idletime = 30000;


	VEC_CURLSESSIONS::iterator it = m_sessions.begin();
	while (it != m_sessions.end()) 
	{
		//unsigned int tickcount = XR::SystemClockMillis() - it->m_idletimestamp;
		if (!it->m_busy && (XR::SystemClockMillis() - it->m_idletimestamp > idletime)) 
		{
			LOGINFO("Closing session to %s://%s (easy=%p, multi=%p)\n", it->m_protocol.c_str(), it->m_hostname.c_str(), (void*)it->m_easy, (void*)it->m_multi);

			// It's important to clean up multi *before* cleaning up easy, because the multi cleanup
			// code accesses stuff in the easy's structure.
			if (it->m_multi)
				curl_multi_cleanup(it->m_multi);
			if (it->m_easy)
				curl_easy_cleanup(it->m_easy);

			Unload();

			it = m_sessions.erase(it);
			continue;
		}
		it++;
	}
}

void DllLibCurlGlobal::easy_aquire(const char *protocol, const char *hostname, CURL_HANDLE** easy_handle, CURLM** multi_handle)
{
	assert(easy_handle != NULL);

	XR::CSingleLock lock(m_critSection);

	VEC_CURLSESSIONS::iterator it;
	for (it = m_sessions.begin(); it != m_sessions.end(); it++) 
	{
		if (!it->m_busy) 
		{
			/* allow reuse of requester is trying to connect to same host */
			/* curl will take care of any differences in username/password */
			if (it->m_protocol.compare(protocol) == 0 && it->m_hostname.compare(hostname) == 0) 
			{
				it->m_busy = true;
				if (easy_handle) 
				{
					if (!it->m_easy)
						it->m_easy = curl_easy_init();

					*easy_handle = it->m_easy;
				}

				if (multi_handle) 
				{
					if (!it->m_multi)
						it->m_multi = curl_multi_init();

					*multi_handle = it->m_multi;
				}
				return;
			}
		}
	}

	SSession session = {};
	session.m_busy = true;
	session.m_protocol = protocol;
	session.m_hostname = hostname;

	/* count up global interface counter */
	Load();

	if (easy_handle) 
	{
		session.m_easy = curl_easy_init();
		*easy_handle = session.m_easy;
	}

	if (multi_handle) 
	{
		session.m_multi = curl_multi_init();
		*multi_handle = session.m_multi;
	}

	m_sessions.push_back(session);


	LOGINFO("Created session to %s://%s (easy=%p, multi=%p)", protocol, hostname, (void*)session.m_easy, (void*)session.m_multi);

	return;
}

void DllLibCurlGlobal::easy_release(CURL_HANDLE** easy_handle, CURLM** multi_handle)
{
	XR::CSingleLock lock(m_critSection);

	CURL_HANDLE* easy = NULL;
	CURLM*       multi = NULL;

	if (easy_handle) 
	{
		easy = *easy_handle;
		*easy_handle = NULL;
	}

	if (multi_handle) 
	{
		multi = *multi_handle;
		*multi_handle = NULL;
	}

	VEC_CURLSESSIONS::iterator it;
	for (it = m_sessions.begin(); it != m_sessions.end(); it++) 
	{
		if (it->m_easy == easy && (multi == NULL || it->m_multi == multi)) 
		{
			/* reset session so next caller doesn't reuse options, only connections */
			/* will reset verbose too so it won't print that it closed connections on cleanup*/
			curl_easy_reset(easy);
			it->m_busy = false;
			it->m_idletimestamp = XR::SystemClockMillis();
			return;
		}
	}
}

CURL_HANDLE* DllLibCurlGlobal::easy_duphandle(CURL_HANDLE* easy_handle)
{
	XR::CSingleLock lock(m_critSection);

	VEC_CURLSESSIONS::iterator it;
	for (it = m_sessions.begin(); it != m_sessions.end(); it++) 
	{
		if (it->m_easy == easy_handle) 
		{
			SSession session = *it;
			session.m_easy = curl_easy_duphandle(easy_handle);
			Load();
			m_sessions.push_back(session);
			return session.m_easy;
		}
	}
	return curl_easy_duphandle(easy_handle);
}

void DllLibCurlGlobal::easy_duplicate(CURL_HANDLE* easy, CURLM* multi, CURL_HANDLE** easy_out, CURLM** multi_out)
{
	XR::CSingleLock lock(m_critSection);

	if (easy_out && easy)
		*easy_out = curl_easy_duphandle(easy);

	if (multi_out && multi)
		*multi_out = curl_multi_init();

	VEC_CURLSESSIONS::iterator it;
	for (it = m_sessions.begin(); it != m_sessions.end(); it++) 
	{
		if (it->m_easy == easy) 
		{
			SSession session = *it;
			if (easy_out && easy)
				session.m_easy = *easy_out;
			else
				session.m_easy = NULL;

			if (multi_out && multi)
				session.m_multi = *multi_out;
			else
				session.m_multi = NULL;

			Load();
			m_sessions.push_back(session);
			return;
		}
	}
	return;
}

DllLibCurlGlobal& DllLibCurlGlobal::Get()
{
	XR::CSingleLock lock(m_critSection);
	static DllLibCurlGlobal m_this;
	return m_this;
}

void DllLibCurlGlobal::UnloadAll()
{
	XR::CSingleLock lock(m_critSection);

	CURL_HANDLE* easy = NULL;
	CURLM*       multi = NULL;
	VEC_CURLSESSIONS::iterator it;

	for (it = m_sessions.begin(); it != m_sessions.end();) 
	{
		if (it->m_easy) 
		{
			curl_easy_reset(it->m_easy);
			it->m_busy = false;
			// It's important to clean up multi *before* cleaning up easy, because the multi cleanup
			// code accesses stuff in the easy's structure.
			if (it->m_multi)
				curl_multi_cleanup(it->m_multi);
			if (it->m_easy)
				curl_easy_cleanup(it->m_easy);
			LOGINFO("Closing session to %s://%s (easy=%p, multi=%p)", it->m_protocol.c_str(), it->m_hostname.c_str(), (void*)it->m_easy, (void*)it->m_multi);
			it = m_sessions.erase(it);

			Unload();
		}
	}

	for (int i = m_curlReferences; i > 0; i--)
		Unload();
}

XR::CCriticalSection DllLibCurlGlobal::m_critSection;
DllLibCurlGlobal::VEC_CURLSESSIONS DllLibCurlGlobal::m_sessions;
long DllLibCurlGlobal::m_curlReferences = 0;
