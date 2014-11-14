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
#undef max

#include "stdafx.h"
#include <stdint.h>
#include <windows.h>
#include "SystemClock.h"

namespace XR
{
	unsigned int SystemClockMillis()
	{
		uint64_t now_time;
		static uint64_t start_time = 0;
		static bool start_time_set = false;

		now_time = (uint64_t)timeGetTime();

		if (!start_time_set)
		{
			start_time = now_time;
			start_time_set = true;
		}
		return (unsigned int)(now_time - start_time);
	}
	const unsigned int EndTime::InfiniteValue = std::numeric_limits<unsigned int>::max();
}