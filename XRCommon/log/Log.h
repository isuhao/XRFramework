#pragma once
#include <string>

#include "ilog.h"
#include "LogDefines.h"
#include "LogImplementation.h"

#include "utils/Singleton.h"
#include "../XRThreads/CriticalSection.h"

static const char* const levelNames[] =
{ "DEBUG", "INFO", "NOTICE", "WARNING", "ERROR", "SEVERE", "FATAL", "NONE" };

// add 1 to level number to get index of name
static const char* const logLevelNames[] =
{ "LOG_LEVEL_NONE" /*-1*/, "LOG_LEVEL_NORMAL" /*0*/, "LOG_LEVEL_DEBUG" /*1*/, "LOG_LEVEL_DEBUG_FREEMEM" /*2*/ };

class CLog : public ::Singleton < CLog > {
public:
	CLog() : m_isInit(false), m_bConsoleOnly(true) {};
	virtual ~CLog();

	bool Init(std::string& path);
	void Close();

	void Log(int loglevel, const char* file, const int lineNumber, const char* functionName, const char* format, ...);
	void LogString(int loglevel, const char* file, const int lineNumber, const char* functionName, std::string& logString);


	void PrintDebugString(const std::string& line); // universal interface for printing debug strings
	int  GetLogLevel() { return m_logLevel; }
	int  GetExtraLogLevel() { return m_extraLogLevels; }

	void SetLogLevel(int level);
	void SetExtraLogLevels(int level);

	bool IsLogExtraLogged(int loglevel);
	bool IsLogLevelLogged(int loglevel);

	static CLog& Create();
	static void	Destroy();

private:
	bool WriteLogString(int logLevel, const char* file, const int lineNumber, const char* functionName, const std::string& logString);

	XR::CCriticalSection m_CritSection;

	LogImplementation_t m_Iplementation;

	int         m_repeatCount;
	int         m_repeatLogLevel;
	std::string m_repeatLine;
	int         m_logLevel;
	int         m_extraLogLevels;
	bool		m_isInit;
	std::string m_errorMessage;
	bool m_bConsoleOnly;
	HANDLE m_hConsoleHandle;
};

#define g_Log CLog::getSingleton()
#define g_LogPtr CLog::getSingletonPtr()

namespace XR
{
	class LogImpl : public XR::ILogger {
	public:
		virtual ~LogImpl() {}
		inline virtual void log(int logLevel, const char* file, const int lineNumber,
			const char* functionName, _In_z_ const char* message) 
		{
			g_LogPtr->Log(logLevel, file, lineNumber, functionName, "%s", message);
		}
	};
}