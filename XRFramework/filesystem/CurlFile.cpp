#include "stdafxf.h"
#include <vector>
#include <climits>


#include "Util.h"
#include "CurlFile.h"
#include "utils/URL.h"
#include "utils/UrlUtils.h"
#include "utils/StringUtils.h"
#include "utils/SpecialProtocol.h"
#include "XRThreads/SystemClock.h"
#include "CurlGlobal.h"
#include "log/log.h"

#define XMIN(a,b) ((a)<(b)?(a):(b))
#define FITS_INT(a) (((a) <= INT_MAX) && ((a) >= INT_MIN))

curl_proxytype proxyType2CUrlProxyType[] = {
	CURLPROXY_HTTP,
	CURLPROXY_SOCKS4,
	CURLPROXY_SOCKS4A,
	CURLPROXY_SOCKS5,
	CURLPROXY_SOCKS5_HOSTNAME,
};

// curl calls this routine to debug
extern "C" int debug_callback(CURL_HANDLE *handle, curl_infotype info, char *output, size_t size, void *data)
{
	if (info == CURLINFO_DATA_IN || info == CURLINFO_DATA_OUT)
		return 0;

	if (!g_LogPtr->IsLogExtraLogged(LOGCURL))
		return 0;

	std::string strLine;
	strLine.append(output, size);
	std::vector<std::string> vecLines;
	StringUtils::Tokenize(strLine, vecLines, "\r\n");
	std::vector<std::string>::const_iterator it = vecLines.begin();

	char *infotype;
	switch (info)
	{
	case CURLINFO_TEXT: infotype = (char *) "TEXT: "; break;
	case CURLINFO_HEADER_IN: infotype = (char *) "HEADER_IN: "; break;
	case CURLINFO_HEADER_OUT: infotype = (char *) "HEADER_OUT: "; break;
	case CURLINFO_SSL_DATA_IN: infotype = (char *) "SSL_DATA_IN: "; break;
	case CURLINFO_SSL_DATA_OUT: infotype = (char *) "SSL_DATA_OUT: "; break;
	case CURLINFO_END: infotype = (char *) "END: "; break;
	default: infotype = (char *) ""; break;
	}

	while (it != vecLines.end())
	{
		LOGDEBUG("%s%s", infotype, (*it).c_str());
		it++;
	}
	return 0;
}

/* curl calls this routine to get more data */
extern "C" size_t write_callback(char *buffer, size_t size, size_t nitems, void *userp)
{
	if (userp == NULL) return 0;

	CCurlFile::CReadState *state = (CCurlFile::CReadState *)userp;
	return state->WriteCallback(buffer, size, nitems);
}

extern "C" size_t read_callback(char *buffer, size_t size, size_t nitems, void *userp)
{
	if (userp == NULL) return 0;

	CCurlFile::CReadState *state = (CCurlFile::CReadState *)userp;
	return state->ReadCallback(buffer, size, nitems);
}

extern "C" size_t header_callback(void *ptr, size_t size, size_t nmemb, void *stream)
{
	CCurlFile::CReadState *state = (CCurlFile::CReadState *)stream;
	return state->HeaderCallback(ptr, size, nmemb);
}

/* fix for silly behavior of realloc */
static inline void* realloc_simple(void *ptr, size_t size)
{
	void *ptr2 = realloc(ptr, size);
	if (ptr && !ptr2 && size > 0)
	{
		free(ptr);
		return NULL;
	}
	else
		return ptr2;
}

size_t CCurlFile::CReadState::HeaderCallback(void *ptr, size_t size, size_t nmemb)
{
	std::string inString;
	// libcurl doc says that this info is not always \0 terminated
	const char* strBuf = (const char*)ptr;
	const size_t iSize = size * nmemb;
	if (strBuf[iSize - 1] == 0)
		inString.assign(strBuf, iSize - 1); // skip last char if it's zero
	else
		inString.append(strBuf, iSize);

	m_httpheader.Parse(inString);

	return iSize;
}

size_t CCurlFile::CReadState::ReadCallback(char *buffer, size_t size, size_t nitems)
{
	if (m_fileSize == 0)
		return 0;

	if (m_filePos >= m_fileSize)
	{
		m_isPaused = true;
		return CURL_READFUNC_PAUSE;
	}

	int64_t retSize = std::min(m_fileSize - m_filePos, int64_t(nitems * size));
	memcpy(buffer, m_readBuffer + m_filePos, retSize);
	m_filePos += retSize;

	return retSize;
}

size_t CCurlFile::CReadState::WriteCallback(char *buffer, size_t size, size_t nitems)
{
	unsigned int amount = size * nitems;
	//  LOGDEBUG("CCurlFile::WriteCallback (%p) with %i bytes, readsize = %i, writesize = %i", this, amount, m_buffer.getMaxReadSize(), m_buffer.getMaxWriteSize() - m_overflowSize);
	if (m_overflowSize)
	{
		// we have our overflow buffer - first get rid of as much as we can
		unsigned int maxWriteable = XMIN((unsigned int)m_buffer.getMaxWriteSize(), m_overflowSize);
		if (maxWriteable)
		{
			if (!m_buffer.WriteData(m_overflowBuffer, maxWriteable))
				LOGERR("Unable to write to buffer - what's up?");
			if (m_overflowSize > maxWriteable)
			{ // still have some more - copy it down
				memmove(m_overflowBuffer, m_overflowBuffer + maxWriteable, m_overflowSize - maxWriteable);
			}
			m_overflowSize -= maxWriteable;
		}
	}
	// ok, now copy the data into our ring buffer
	unsigned int maxWriteable = XMIN((unsigned int)m_buffer.getMaxWriteSize(), amount);
	if (maxWriteable)
	{
		if (!m_buffer.WriteData(buffer, maxWriteable))
		{
			LOGERR("Unable to write to buffer with %i bytes - what's up?", maxWriteable);
		}
		else
		{
			amount -= maxWriteable;
			buffer += maxWriteable;
		}
	}
	if (amount)
	{
		//    LOGDEBUG("CCurlFile::WriteCallback(%p) not enough free space for %i bytes", (void*)this,  amount);

		m_overflowBuffer = (char*)realloc_simple(m_overflowBuffer, amount + m_overflowSize);
		if (m_overflowBuffer == NULL)
		{
			LOGWARN("Failed to grow overflow buffer from %i bytes to %i bytes", m_overflowSize, amount + m_overflowSize);
			return 0;
		}
		memcpy(m_overflowBuffer + m_overflowSize, buffer, amount);
		m_overflowSize += amount;
	}
	return size * nitems;
}

CCurlFile::CReadState::CReadState()
{
	m_easyHandle = NULL;
	m_multiHandle = NULL;
	m_overflowBuffer = NULL;
	m_overflowSize = 0;
	m_filePos = 0;
	m_fileSize = 0;
	m_bufferSize = 0;
	m_cancelled = false;
	m_bFirstLoop = true;
	m_sendRange = true;
	m_readBuffer = 0;
	m_isPaused = false;
	m_curlHeaderList = NULL;
	m_curlAliasList = NULL;
}

CCurlFile::CReadState::~CReadState()
{
	Disconnect();
	if (m_easyHandle)
		CCurlGlobal::easy_release(&m_easyHandle, &m_multiHandle);
}

bool CCurlFile::CReadState::Seek(int64_t pos)
{
	if (pos == m_filePos)
		return true;

	if (FITS_INT(pos - m_filePos) && m_buffer.SkipBytes((int)(pos - m_filePos)))
	{
		m_filePos = pos;
		return true;
	}

	if (pos > m_filePos && pos < m_filePos + m_bufferSize)
	{
		int len = m_buffer.getMaxReadSize();
		m_filePos += len;
		m_buffer.SkipBytes(len);
		if (!FillBuffer(m_bufferSize))
		{
			if (!m_buffer.SkipBytes(-len))
				LOGERR("Failed to restore position after failed fill")
			else
			m_filePos -= len;

			return false;
		}

		if (!FITS_INT(pos - m_filePos) || !m_buffer.SkipBytes((int)(pos - m_filePos)))
		{
			LOGERR("Failed to skip to position after having filled buffer");

			if (!m_buffer.SkipBytes(-len))
				LOGERR("Failed to restore position after failed seek")
			else
			m_filePos -= len;

			return false;
		}
		m_filePos = pos;
		return true;
	}
	return false;
}

void CCurlFile::CReadState::SetResume(void)
{
	/*
	* Explicitly set RANGE header when filepos=0 as some http servers require us to always send the range
	* request header. If we don't the server may provide different content causing seeking to fail.
	* This only affects HTTP-like items, for FTP it's a null operation.
	*/
	if (m_sendRange && m_filePos == 0)
		curl_easy_setopt(m_easyHandle, CURLOPT_RANGE, "0-");
	else
	{
		curl_easy_setopt(m_easyHandle, CURLOPT_RANGE, NULL);
		m_sendRange = false;
	}

	curl_easy_setopt(m_easyHandle, CURLOPT_RESUME_FROM_LARGE, m_filePos);
}

long CCurlFile::CReadState::Connect(unsigned int size)
{
	if (m_filePos != 0)
		LOGDEBUG("Resume from position %""I64d", m_filePos);

	SetResume();
	curl_multi_add_handle(m_multiHandle, m_easyHandle);

	m_bufferSize = size;
	m_buffer.Destroy();
	m_buffer.Create(size * 3);
	m_httpheader.Clear();

	// read some data in to try and obtain the length
	// maybe there's a better way to get this info??
	m_stillRunning = 1;
	if (!FillBuffer(1))
	{
		LOGERR("Didn't get any data from stream.");
		return -1;
	}

	double length;
	if (CURLE_OK == curl_easy_getinfo(m_easyHandle, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &length))
	{
		if (length < 0)
			length = 0.0;
		m_fileSize = m_filePos + (int64_t)length;
	}

	long response;
	if (CURLE_OK == curl_easy_getinfo(m_easyHandle, CURLINFO_RESPONSE_CODE, &response))
		return response;

	return -1;
}

void CCurlFile::CReadState::Disconnect()
{
	if (m_multiHandle && m_easyHandle)
		curl_multi_remove_handle(m_multiHandle, m_easyHandle);

	m_buffer.Clear();
	free(m_overflowBuffer);
	m_overflowBuffer = NULL;
	m_overflowSize = 0;
	m_filePos = 0;
	m_fileSize = 0;
	m_bufferSize = 0;
	m_readBuffer = 0;

	/* cleanup */
	if (m_curlHeaderList)
		curl_slist_free_all(m_curlHeaderList);
	m_curlHeaderList = NULL;

	if (m_curlAliasList)
		curl_slist_free_all(m_curlAliasList);
	m_curlAliasList = NULL;
}

CCurlFile::~CCurlFile()
{
	Close();
	delete m_state;
	delete m_oldState;
	//	m_curlInterface.Unload();
}

CCurlFile::CCurlFile()
{
	//m_curlInterface = DllLibCurlGlobal::Get();
	//m_curlInterface.Load(); // loads the curl dll and resolves exports etc.
	m_opened = false;
	m_forWrite = false;
	m_inError = false;
	m_multisession = true;
	m_seekable = true;
	m_useOldHttpVersion = false;
	m_connecttimeout = 0;
	m_lowspeedtime = 0;
	m_ftpauth = "";
	m_ftpport = "";
	m_ftppasvip = false;
	m_bufferSize = 32768;
	m_binary = true;
	m_postdata = "";
	m_postdataset = false;
	m_username = "";
	m_password = "";
	m_httpauth = "";
	m_cipherlist = "DEFAULT";
	m_proxytype = PROXY_HTTP;
	m_state = new CReadState();
	m_oldState = NULL;
	m_skipshout = false;
	m_httpresponse = -1;
	m_acceptCharset = "UTF-8,*;q=0.8"; /* prefer UTF-8 if available */
}

//Has to be called before Open()
void CCurlFile::SetBufferSize(unsigned int size)
{
	m_bufferSize = size;
}

void CCurlFile::Close()
{
	if (m_opened && m_forWrite && !m_inError)
		Write(NULL, 0);

	m_state->Disconnect();
	delete m_oldState;
	m_oldState = NULL;

	m_url.clear();
	m_referer.clear();
	m_cookie.clear();

	m_opened = false;
	m_forWrite = false;
	m_inError = false;
}

void CCurlFile::SetCommonOptions(CReadState* state)
{
	CURL_HANDLE* h = state->m_easyHandle;

	curl_easy_reset(h);

	curl_easy_setopt(h, CURLOPT_DEBUGFUNCTION, debug_callback);

#ifdef _DEBUG 
	curl_easy_setopt(h, CURLOPT_VERBOSE, TRUE);
#else
	curl_easy_setopt(h, CURLOPT_VERBOSE, FALSE);
#endif
	curl_easy_setopt(h, CURLOPT_WRITEDATA, state);
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_callback);

	curl_easy_setopt(h, CURLOPT_READDATA, state);
	curl_easy_setopt(h, CURLOPT_READFUNCTION, read_callback);

	// set username and password for current handle
	if (m_username.length() > 0 && m_password.length() > 0)
	{
		std::string userpwd = m_username + ":" + m_password;
		curl_easy_setopt(h, CURLOPT_USERPWD, userpwd.c_str());
	}

	// make sure headers are seperated from the data stream
	curl_easy_setopt(h, CURLOPT_WRITEHEADER, state);
	curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, header_callback);
	curl_easy_setopt(h, CURLOPT_HEADER, FALSE);

	curl_easy_setopt(h, CURLOPT_FTP_USE_EPSV, 0); // turn off epsv

	// Allow us to follow two redirects
	curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, TRUE);
	curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5);

	// Enable cookie engine for current handle to re-use them in future requests
	std::string strCookieFile;
	//std::string strTempPath = CSpecialProtocol::TranslatePath(special://);
	std::string strTempPath = CSpecialProtocol::TranslatePath("special://temp/");
	strCookieFile = UrlUtils::AddFileToFolder(strTempPath, "cookies.dat");

	curl_easy_setopt(h, CURLOPT_COOKIEFILE, strCookieFile.c_str());
	curl_easy_setopt(h, CURLOPT_COOKIEJAR, strCookieFile.c_str());

	// Set custom cookie if requested
	if (!m_cookie.empty())
		curl_easy_setopt(h, CURLOPT_COOKIE, m_cookie.c_str());

	curl_easy_setopt(h, CURLOPT_COOKIELIST, "FLUSH");

	// When using multiple threads you should set the CURLOPT_NOSIGNAL option to
	// TRUE for all handles. Everything will work fine except that timeouts are not
	// honored during the DNS lookup - which you can work around by building libcurl
	// with c-ares support. c-ares is a library that provides asynchronous name
	// resolves. Unfortunately, c-ares does not yet support IPv6.
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, TRUE);

	// not interested in failed requests
	curl_easy_setopt(h, CURLOPT_FAILONERROR, 1);

	// enable support for icecast / shoutcast streams
	if (NULL == state->m_curlAliasList)
		// m_curlAliasList is used only by this one place, but SetCommonOptions can
		// be called multiple times, only append to list if it's empty.
		state->m_curlAliasList = curl_slist_append(state->m_curlAliasList, "ICY 200 OK");
	curl_easy_setopt(h, CURLOPT_HTTP200ALIASES, state->m_curlAliasList);

	// never verify peer, we don't have any certificates to do this
	curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0);

	curl_easy_setopt(m_state->m_easyHandle, CURLOPT_URL, m_url.c_str());
	curl_easy_setopt(m_state->m_easyHandle, CURLOPT_TRANSFERTEXT, FALSE);

	// setup POST data if it is set (and it may be empty)
	if (m_postdataset || !m_postdata.empty())
	{
		curl_easy_setopt(h, CURLOPT_POST, 1);
		curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, m_postdata.length());
		curl_easy_setopt(h, CURLOPT_POSTFIELDS, m_postdata.c_str());
	}

	// setup Referer header if needed
	if (!m_referer.empty())
		curl_easy_setopt(h, CURLOPT_REFERER, m_referer.c_str());
	else
	{
		curl_easy_setopt(h, CURLOPT_REFERER, NULL);
		curl_easy_setopt(h, CURLOPT_AUTOREFERER, TRUE);
	}

	// setup any requested authentication
	if (m_ftpauth.length() > 0)
	{
		curl_easy_setopt(h, CURLOPT_FTP_SSL, CURLFTPSSL_TRY);
		if (m_ftpauth == "any")
			curl_easy_setopt(h, CURLOPT_FTPSSLAUTH, CURLFTPAUTH_DEFAULT);
		else if (m_ftpauth == "ssl")
			curl_easy_setopt(h, CURLOPT_FTPSSLAUTH, CURLFTPAUTH_SSL);
		else if (m_ftpauth == "tls")
			curl_easy_setopt(h, CURLOPT_FTPSSLAUTH, CURLFTPAUTH_TLS);
	}

	// setup requested http authentication method
	if (m_httpauth.length() > 0)
	{
		if (m_httpauth == "any")
			curl_easy_setopt(h, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
		else if (m_httpauth == "anysafe")
			curl_easy_setopt(h, CURLOPT_HTTPAUTH, CURLAUTH_ANYSAFE);
		else if (m_httpauth == "digest")
			curl_easy_setopt(h, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
		else if (m_httpauth == "ntlm")
			curl_easy_setopt(h, CURLOPT_HTTPAUTH, CURLAUTH_NTLM);
	}

	// allow passive mode for ftp
	if (m_ftpport.length() > 0)
		curl_easy_setopt(h, CURLOPT_FTPPORT, m_ftpport.c_str());
	else
		curl_easy_setopt(h, CURLOPT_FTPPORT, NULL);

	// allow curl to not use the ip address in the returned pasv response
	if (m_ftppasvip)
		curl_easy_setopt(h, CURLOPT_FTP_SKIP_PASV_IP, 0);
	else
		curl_easy_setopt(h, CURLOPT_FTP_SKIP_PASV_IP, 1);

	// setup Content-Encoding if requested
	if (m_contentencoding.length() > 0)
		curl_easy_setopt(h, CURLOPT_ENCODING, m_contentencoding.c_str());

	if (!m_useOldHttpVersion && !m_acceptCharset.empty())
		SetRequestHeader("Accept-Charset", m_acceptCharset);

	if (m_userAgent.length() > 0)
		curl_easy_setopt(h, CURLOPT_USERAGENT, m_userAgent.c_str());
	else /* set some default agent as shoutcast doesn't return proper stuff otherwise */
		curl_easy_setopt(h, CURLOPT_USERAGENT, "XR");

	if (m_useOldHttpVersion)
		curl_easy_setopt(h, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

	if (false)
		curl_easy_setopt(h, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

	if (m_proxy.length() > 0)
	{
		curl_easy_setopt(h, CURLOPT_PROXY, m_proxy.c_str());
		curl_easy_setopt(h, CURLOPT_PROXYTYPE, proxyType2CUrlProxyType[m_proxytype]);
		if (m_proxyuserpass.length() > 0)
			curl_easy_setopt(h, CURLOPT_PROXYUSERPWD, m_proxyuserpass.c_str());

	}
	if (m_customrequest.length() > 0)
		curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, m_customrequest.c_str());

	if (m_connecttimeout == 0)
		m_connecttimeout = 10;

	// set our timeouts, we abort connection after m_timeout, and reads after no data for m_timeout seconds
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, m_connecttimeout);

	// We abort in case we transfer less than 1byte/second
	curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1);

	if (m_lowspeedtime == 0)
		m_lowspeedtime = 20;

	// Set the lowspeed time very low as it seems Curl takes much longer to detect a lowspeed condition
	curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, m_lowspeedtime);

	if (m_skipshout)
		// For shoutcast file, content-length should not be set, and in libcurl there is a bug, if the
		// cast file was 302 redirected then getinfo of CURLINFO_CONTENT_LENGTH_DOWNLOAD will return
		// the 302 response's body length, which cause the next read request failed, so we ignore
		// content-length for shoutcast file to workaround this.
		curl_easy_setopt(h, CURLOPT_IGNORE_CONTENT_LENGTH, 1);

	// Setup allowed TLS/SSL ciphers. New versions of cURL may deprecate things that are still in use.
	curl_easy_setopt(h, CURLOPT_SSL_CIPHER_LIST, m_cipherlist.c_str());
}

void CCurlFile::SetRequestHeaders(CReadState* state)
{
	if (state->m_curlHeaderList)
	{
		curl_slist_free_all(state->m_curlHeaderList);
		state->m_curlHeaderList = NULL;
	}

	MAPHTTPHEADERS::iterator it;
	for (it = m_requestheaders.begin(); it != m_requestheaders.end(); it++)
	{
		std::string buffer = it->first + ": " + it->second;
		state->m_curlHeaderList = curl_slist_append(state->m_curlHeaderList, buffer.c_str());
	}

	// add user defined headers
	if (state->m_easyHandle)
		curl_easy_setopt(state->m_easyHandle, CURLOPT_HTTPHEADER, state->m_curlHeaderList);
}

void CCurlFile::SetCorrectHeaders(CReadState* state)
{
	CHttpHeader& h = state->m_httpheader;
	/* workaround for shoutcast server wich doesn't set content type on standard mp3 */
	if (h.GetMimeType().empty())
	{
		if (!h.GetValue("icy-notice1").empty()
			|| !h.GetValue("icy-name").empty()
			|| !h.GetValue("icy-br").empty())
			h.AddParam("Content-Type", "audio/mpeg");
	}

	/* hack for google video */
	if (StringUtils::EqualsNoCase(h.GetMimeType(), "text/html")
		&& !h.GetValue("Content-Disposition").empty())
	{
		std::string strValue = h.GetValue("Content-Disposition");
		if (strValue.find("filename=") != std::string::npos &&
			strValue.find(".flv") != std::string::npos)
			h.AddParam("Content-Type", "video/flv");
	}
}

void CCurlFile::ParseAndCorrectUrl(CURL &url2)
{
	std::string strProtocol = url2.GetTranslatedProtocol();
	url2.SetProtocol(strProtocol);

	if (strProtocol == "ftp"
		|| strProtocol == "ftps")
	{
		// we was using url options for urls, keep the old code work and warning
		if (!url2.GetOptions().empty())
		{
			LOGWARN("Ftp url option is deprecated, please switch to use protocol option (change '?' to '|'), url: [%s]", url2.GetRedacted().c_str());
			url2.SetProtocolOptions(url2.GetOptions().substr(1));
			/* ftp has no options */
			url2.SetOptions("");
		}

		/* this is uggly, depending on from where   */
		/* we get the link it may or may not be     */
		/* url encoded. if handed from ftpdirectory */
		/* it won't be so let's handle that case    */

		std::string filename(url2.GetFileName());
		std::vector<std::string> array;

		// if server sent us the filename in non-utf8, we need send back with same encoding.
		if (url2.GetProtocolOption("utf8") == "0")
			//g_charsetConverter.utf8ToStringCharset(filename);

			/* TODO: create a tokenizer that doesn't skip empty's */
			StringUtils::Tokenize(filename, array, "/");
		filename.clear();
		for (std::vector<std::string>::iterator it = array.begin(); it != array.end(); it++)
		{
			if (it != array.begin())
				filename += "/";

			filename += CURL::Encode(*it);
		}

		/* make sure we keep slashes */
		if (StringUtils::EndsWith(url2.GetFileName(), "/"))
			filename += "/";

		url2.SetFileName(filename);

		m_ftpauth = "";
		if (url2.HasProtocolOption("auth"))
		{
			m_ftpauth = url2.GetProtocolOption("auth");
			if (m_ftpauth.empty())
				m_ftpauth = "any";
		}
		m_ftpport = "";
		if (url2.HasProtocolOption("active"))
		{
			m_ftpport = url2.GetProtocolOption("active");
			if (m_ftpport.empty())
				m_ftpport = "-";
		}
		m_ftppasvip = url2.HasProtocolOption("pasvip") && url2.GetProtocolOption("pasvip") != "0";
	}
	else if (strProtocol == "http"
		|| strProtocol == "https")
	{
		// 		if (CSettings::Get().GetBool("network.usehttpproxy")
		// 			&& !CSettings::Get().GetString("network.httpproxyserver").empty()
		// 			&& CSettings::Get().GetInt("network.httpproxyport") > 0
		// 			&& m_proxy.empty())
		// 		{
		// 			m_proxy = CSettings::Get().GetString("network.httpproxyserver");
		// 			m_proxy += StringUtils::Format(":%d", CSettings::Get().GetInt("network.httpproxyport"));
		// 			if (CSettings::Get().GetString("network.httpproxyusername").length() > 0 && m_proxyuserpass.empty())
		// 			{
		// 				m_proxyuserpass = CSettings::Get().GetString("network.httpproxyusername");
		// 				m_proxyuserpass += ":" + CSettings::Get().GetString("network.httpproxypassword");
		// 			}
		// 			m_proxytype = (ProxyType)CSettings::Get().GetInt("network.httpproxytype");
		// 			LOGDEBUG("Using proxy %s, type %d", m_proxy.c_str(), proxyType2CUrlProxyType[m_proxytype]);
		// 		}

		// get username and password
		m_username = url2.GetUserName();
		m_password = url2.GetPassWord();

		// handle any protocol options
		std::map<std::string, std::string> options;
		url2.GetProtocolOptions(options);
		if (options.size() > 0)
		{
			// clear protocol options
			url2.SetProtocolOptions("");
			// set headers
			for (std::map<std::string, std::string>::const_iterator it = options.begin(); it != options.end(); ++it)
			{
				const std::string &name = it->first;
				const std::string &value = it->second;

				if (name == "auth")
				{
					m_httpauth = value;
					if (m_httpauth.empty())
						m_httpauth = "any";
				}
				else if (name == "Referer")
					SetReferer(value);
				else if (name == "User-Agent")
					SetUserAgent(value);
				else if (name == "Cookie")
					SetCookie(value);
				else if (name == "Encoding")
					SetContentEncoding(value);
				else if (name == "noshout" && value == "true")
					m_skipshout = true;
				else if (name == "seekable" && value == "0")
					m_seekable = false;
				else if (name == "Accept-Charset")
					SetAcceptCharset(value);
				else if (name == "HttpProxy")
					SetStreamProxy(value, PROXY_HTTP);
				else if (name == "SSLCipherList")
					m_cipherlist = value;
				else
					SetRequestHeader(name, value);
			}
		}
	}

	if (m_username.length() > 0 && m_password.length() > 0)
		m_url = url2.GetWithoutUserDetails();
	else
		m_url = url2.Get();
}

void CCurlFile::SetStreamProxy(const std::string &proxy, ProxyType type)
{
	CURL url(proxy);
	m_proxy = url.GetWithoutUserDetails();
	m_proxyuserpass = url.GetUserName();
	if (!url.GetPassWord().empty())
		m_proxyuserpass += ":" + url.GetPassWord();
	m_proxytype = type;
	LOGDEBUG("Overriding proxy from URL parameter: %s, type %d", m_proxy.c_str(), proxyType2CUrlProxyType[m_proxytype]);
}

bool CCurlFile::Post(const std::string& strURL, const std::string& strPostData, std::string& strHTML)
{
	m_postdata = strPostData;
	m_postdataset = true;
	return Service(strURL, strHTML);
}

bool CCurlFile::Get(const std::string& strURL, std::string& strHTML)
{
	m_postdata = "";
	m_postdataset = false;
	return Service(strURL, strHTML);
}

bool CCurlFile::Service(const std::string& strURL, std::string& strHTML)
{
	if (Open(strURL))
	{
		if (ReadData(strHTML))
		{
			Close();
			return true;
		}
	}
	Close();
	return false;
}

bool CCurlFile::ReadData(std::string& strHTML)
{
	int size_read = 0;
	int data_size = 0;
	strHTML = "";
	char buffer[16384];
	while ((size_read = Read(buffer, sizeof(buffer) - 1)) > 0)
	{
		buffer[size_read] = 0;
		strHTML.append(buffer, size_read);
		data_size += size_read;
	}
	if (m_state->m_cancelled)
		return false;
	return true;
}

bool CCurlFile::Download(const std::string& strURL, const std::string& strFileName, LPDWORD pdwSize)
{
	LOGINFO("Download started - %s->%s", strURL.c_str(), strFileName.c_str());

	// 	std::string strData;
	// 	if (!Get(strURL, strData))
	// 		return false;
	// 
	// 	XFILE::CFile file;
	// 	if (!file.OpenForWrite(strFileName, true))
	// 	{
	// 		LOGERR("CCurlFile::Download - Unable to open file %s: %u",
	// 			strFileName.c_str(), GetLastError());
	// 		return false;
	// 	}
	// 	if (strData.size())
	// 		file.Write(strData.c_str(), strData.size());
	// 	file.Close();
	// 
	// 	if (pdwSize != NULL)
	// 	{
	// 		*pdwSize = strData.size();
	// 	}

	return true;
}

// Detect whether we are "online" or not! Very simple and dirty!
bool CCurlFile::IsInternet()
{
	std::string strURL = "http://www.google.com";
	bool found = Exists(strURL);
	Close();

	return found;
}

void CCurlFile::Cancel()
{
	m_state->m_cancelled = true;
	while (m_opened)
		Sleep(1);
}

void CCurlFile::Reset()
{
	m_state->m_cancelled = false;
}

bool CCurlFile::Open(const CURL& url)
{
	m_opened = true;
	m_seekable = true;

	CURL url2(url);
	ParseAndCorrectUrl(url2);

	std::string redactPath = CURL::GetRedacted(m_url);
	LOGDEBUG("CurlFile::Open(%p) %s", (void*)this, redactPath.c_str());

	assert(!(!m_state->m_easyHandle ^ !m_state->m_multiHandle));
	if (m_state->m_easyHandle == NULL)
		CCurlGlobal::easy_aquire(url2.GetProtocol().c_str(), url2.GetHostName().c_str(), &m_state->m_easyHandle, &m_state->m_multiHandle);

	// setup common curl options
	SetCommonOptions(m_state);
	SetRequestHeaders(m_state);
	m_state->m_sendRange = m_seekable;

	m_httpresponse = m_state->Connect(m_bufferSize);
	if (m_httpresponse < 0 || m_httpresponse >= 400)
		return false;

	SetCorrectHeaders(m_state);

	// since we can't know the stream size up front if we're gzipped/deflated
	// flag the stream with an unknown file size rather than the compressed
	// file size.
	if (m_contentencoding.size() > 0)
		m_state->m_fileSize = 0;

	// 	// check if this stream is a shoutcast stream. sometimes checking the protocol line is not enough so examine other headers as well.
	// 	// shoutcast streams should be handled by FileShoutcast.
	// 	if ((m_state->m_httpheader.GetProtoLine().substr(0, 3) == "ICY" || !m_state->m_httpheader.GetValue("icy-notice1").empty()
	// 		|| !m_state->m_httpheader.GetValue("icy-name").empty()
	// 		|| !m_state->m_httpheader.GetValue("icy-br").empty()) && !m_skipshout)
	// 	{
	// 		LOGDEBUG("CCurlFile::Open - File <%s> is a shoutcast stream. Re-opening", redactPath.c_str());
	// 		throw new CRedirectException(new CShoutcastFile);
	// 	}

	m_multisession = false;
	if (url2.GetProtocol() == "http" || url2.GetProtocol() == "https")
	{
		m_multisession = true;
		if (m_state->m_httpheader.GetValue("Server").find("Portable SDK for UPnP devices") != std::string::npos)
		{
			LOGWARN("CCurlFile::Open - Disabling multi session due to broken libupnp server");
			m_multisession = false;
		}
	}

	if (StringUtils::EqualsNoCase(m_state->m_httpheader.GetValue("Transfer-Encoding"), "chunked"))
		m_state->m_fileSize = 0;

	if (m_state->m_fileSize <= 0)
		m_seekable = false;
	if (m_seekable)
	{
		if (url2.GetProtocol() == "http"
			|| url2.GetProtocol() == "https")
		{
			// if server says explicitly it can't seek, respect that
			if (StringUtils::EqualsNoCase(m_state->m_httpheader.GetValue("Accept-Ranges"), "none"))
				m_seekable = false;
		}
	}

	char* efurl;
	if (CURLE_OK == curl_easy_getinfo(m_state->m_easyHandle, CURLINFO_EFFECTIVE_URL, &efurl) && efurl)
	{
		if (m_url != efurl)
		{
			std::string redactEfpath = CURL::GetRedacted(efurl);
			LOGDEBUG("CCurlFile::Open - effective URL: <%s>", redactEfpath.c_str());
		}
		m_url = efurl;
	}
	return true;
}

bool CCurlFile::OpenForWrite(const CURL& url, bool bOverWrite)
{
	if (m_opened)
		return false;

	if (Exists(url) && !bOverWrite)
		return false;

	CURL url2(url);
	ParseAndCorrectUrl(url2);

	LOGDEBUG("CCurlFile::OpenForWrite(%p) %s", (void*)this, CURL::GetRedacted(m_url).c_str());

	ASSERT(m_state->m_easyHandle == NULL);
	CCurlGlobal::easy_aquire(url2.GetProtocol().c_str(), url2.GetHostName().c_str(), &m_state->m_easyHandle, &m_state->m_multiHandle);

	// setup common curl options
	SetCommonOptions(m_state);
	SetRequestHeaders(m_state);

	char* efurl;
	if (CURLE_OK == curl_easy_getinfo(m_state->m_easyHandle, CURLINFO_EFFECTIVE_URL, &efurl) && efurl)
		m_url = efurl;

	m_opened = true;
	m_forWrite = true;
	m_inError = false;
	m_writeOffset = 0;

	ASSERT(m_state->m_multiHandle);

	SetCommonOptions(m_state);
	curl_easy_setopt(m_state->m_easyHandle, CURLOPT_UPLOAD, 1);

	curl_multi_add_handle(m_state->m_multiHandle, m_state->m_easyHandle);

	m_state->SetReadBuffer(NULL, 0);

	return true;
}

int CCurlFile::Write(const void* lpBuf, int64_t uiBufSize)
{
	if (!(m_opened && m_forWrite) || m_inError)
		return -1;

	ASSERT(m_state->m_multiHandle);

	m_state->SetReadBuffer(lpBuf, uiBufSize);
	m_state->m_isPaused = false;
	curl_easy_pause(m_state->m_easyHandle, CURLPAUSE_CONT);

	CURLMcode result = CURLM_OK;

	m_stillRunning = 1;
	while (m_stillRunning && !m_state->m_isPaused)
	{
		while ((result = curl_multi_perform(m_state->m_multiHandle, &m_stillRunning)) == CURLM_CALL_MULTI_PERFORM);

		if (!m_stillRunning)
			break;

		if (result != CURLM_OK)
		{
			long code;
			if (curl_easy_getinfo(m_state->m_easyHandle, CURLINFO_RESPONSE_CODE, &code) == CURLE_OK)
				LOGERR("%s - Unable to write curl resource (%s) - %ld", __FUNCTION__, CURL::GetRedacted(m_url).c_str(), code);

			m_inError = true;
			return -1;
		}
	}

	m_writeOffset += m_state->m_filePos;
	return m_state->m_filePos;
}

bool CCurlFile::CReadState::ReadString(char *szLine, int iLineLength)
{
	unsigned int want = (unsigned int)iLineLength;

	if ((m_fileSize == 0 || m_filePos < m_fileSize) && !FillBuffer(want))
		return false;

	// ensure only available data is considered
	want = XMIN((unsigned int)m_buffer.getMaxReadSize(), want);

	/* check if we finished prematurely */
	if (!m_stillRunning && (m_fileSize == 0 || m_filePos != m_fileSize) && !want)
	{
		if (m_fileSize != 0)
			LOGWARN("%s - Transfer ended before entire file was retrieved pos %""I64d"", size %""I64d", __FUNCTION__, m_filePos, m_fileSize);

		return false;
	}

	char* pLine = szLine;
	do
	{
		if (!m_buffer.ReadData(pLine, 1))
			break;

		pLine++;
	} while (((pLine - 1)[0] != '\n') && ((unsigned int)(pLine - szLine) < want));
	pLine[0] = 0;
	m_filePos += (pLine - szLine);
	return (bool)((pLine - szLine) > 0);
}

bool CCurlFile::Exists(const CURL& url)
{
	// if file is already running, get info from it
	if (m_opened)
	{
		LOGWARN("CCurlFile::Exists - Exist called on open file %s", url.GetRedacted().c_str());
		return true;
	}

	CURL url2(url);
	ParseAndCorrectUrl(url2);

	ASSERT(m_state->m_easyHandle == NULL);
	CCurlGlobal::easy_aquire(url2.GetProtocol().c_str(), url2.GetHostName().c_str(), &m_state->m_easyHandle, NULL);

	SetCommonOptions(m_state);
	SetRequestHeaders(m_state);
	curl_easy_setopt(m_state->m_easyHandle, CURLOPT_TIMEOUT, 5);
	curl_easy_setopt(m_state->m_easyHandle, CURLOPT_NOBODY, 1);
	curl_easy_setopt(m_state->m_easyHandle, CURLOPT_WRITEDATA, NULL); /* will cause write failure*/

	if (url2.GetProtocol() == "ftp")
	{
		curl_easy_setopt(m_state->m_easyHandle, CURLOPT_FILETIME, 1);
		// nocwd is less standard, will return empty list for non-existed remote dir on some ftp server, avoid it.
		if (StringUtils::EndsWith(url2.GetFileName(), "/"))
			curl_easy_setopt(m_state->m_easyHandle, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_SINGLECWD);
		else
			curl_easy_setopt(m_state->m_easyHandle, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_NOCWD);
	}

	CURLcode result = curl_easy_perform(m_state->m_easyHandle);
	CCurlGlobal::easy_release(&m_state->m_easyHandle, NULL);

	if (result == CURLE_WRITE_ERROR || result == CURLE_OK)
		return true;

	if (result == CURLE_HTTP_RETURNED_ERROR)
	{
		long code;
		if (curl_easy_getinfo(m_state->m_easyHandle, CURLINFO_RESPONSE_CODE, &code) == CURLE_OK && code != 404)
			LOGERR("CCurlFile::Exists - Failed: HTTP returned error %ld for %s", code, url.GetRedacted().c_str());
	}
	else if (result != CURLE_REMOTE_FILE_NOT_FOUND && result != CURLE_FTP_COULDNT_RETR_FILE)
	{
		LOGERR("CCurlFile::Exists - Failed: %s(%d) for %s", curl_easy_strerror(result), result, url.GetRedacted().c_str());
	}

	errno = ENOENT;
	return false;
}

int64_t CCurlFile::Seek(int64_t iFilePosition, int iWhence)
{
	int64_t nextPos = m_state->m_filePos;

	if (!m_seekable)
		return -1;

	switch (iWhence)
	{
	case SEEK_SET:
		nextPos = iFilePosition;
		break;
	case SEEK_CUR:
		nextPos += iFilePosition;
		break;
	case SEEK_END:
		if (m_state->m_fileSize)
			nextPos = m_state->m_fileSize + iFilePosition;
		else
			return -1;
		break;
	default:
		return -1;
	}

	// We can't seek beyond EOF
	if (m_state->m_fileSize && nextPos > m_state->m_fileSize) return -1;

	if (m_state->Seek(nextPos))
		return nextPos;

	if (m_multisession)
	{
		if (!m_oldState)
		{
			CURL url(m_url);
			m_oldState = m_state;
			m_state = new CReadState();
			m_state->m_fileSize = m_oldState->m_fileSize;
			CCurlGlobal::easy_aquire(url.GetProtocol().c_str(),
				url.GetHostName().c_str(),
				&m_state->m_easyHandle,
				&m_state->m_multiHandle);
		}
		else
		{
			CReadState *tmp;
			tmp = m_state;
			m_state = m_oldState;
			m_oldState = tmp;

			if (m_state->Seek(nextPos))
				return nextPos;

			m_state->Disconnect();
		}
	}
	else
		m_state->Disconnect();

	// re-setup common curl options
	SetCommonOptions(m_state);

	/* caller might have changed some headers (needed for daap)*/
	SetRequestHeaders(m_state);

	m_state->m_filePos = nextPos;
	m_state->m_sendRange = true;

	long response = m_state->Connect(m_bufferSize);
	if (response < 0 && (m_state->m_fileSize == 0 || m_state->m_fileSize != m_state->m_filePos))
	{
		if (m_multisession)
		{
			if (m_oldState)
			{
				delete m_state;
				m_state = m_oldState;
				m_oldState = NULL;
			}
			// Retry without mutlisession
			m_multisession = false;
			return Seek(iFilePosition, iWhence);
		}
		else
		{
			m_seekable = false;
			return -1;
		}
	}

	SetCorrectHeaders(m_state);

	return m_state->m_filePos;
}

int64_t CCurlFile::GetLength()
{
	if (!m_opened) return 0;
	return m_state->m_fileSize;
}

int64_t CCurlFile::GetPosition()
{
	if (!m_opened) return 0;
	return m_state->m_filePos;
}

int CCurlFile::Stat(const CURL& url, struct __stat64* buffer)
{
	// if file is already running, get info from it
	if (m_opened)
	{
		LOGWARN("CCurlFile::Stat - Stat called on open file %s", url.GetRedacted().c_str());
		if (buffer)
		{
			memset(buffer, 0, sizeof(struct __stat64));
			buffer->st_size = GetLength();
			buffer->st_mode = _S_IFREG;
		}
		return 0;
	}

	CURL url2(url);
	ParseAndCorrectUrl(url2);

	ASSERT(m_state->m_easyHandle == NULL);
	CCurlGlobal::easy_aquire(url2.GetProtocol().c_str(), url2.GetHostName().c_str(), &m_state->m_easyHandle, NULL);

	SetCommonOptions(m_state);
	SetRequestHeaders(m_state);
	curl_easy_setopt(m_state->m_easyHandle, CURLOPT_TIMEOUT, 10);
	curl_easy_setopt(m_state->m_easyHandle, CURLOPT_NOBODY, 1);
	curl_easy_setopt(m_state->m_easyHandle, CURLOPT_WRITEDATA, NULL); /* will cause write failure*/
	curl_easy_setopt(m_state->m_easyHandle, CURLOPT_FILETIME, 1);

	if (url2.GetProtocol() == "ftp")
	{
		// nocwd is less standard, will return empty list for non-existed remote dir on some ftp server, avoid it.
		if (StringUtils::EndsWith(url2.GetFileName(), "/"))
			curl_easy_setopt(m_state->m_easyHandle, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_SINGLECWD);
		else
			curl_easy_setopt(m_state->m_easyHandle, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_NOCWD);
	}

	CURLcode result = curl_easy_perform(m_state->m_easyHandle);

	if (result == CURLE_HTTP_RETURNED_ERROR)
	{
		long code;
		if (curl_easy_getinfo(m_state->m_easyHandle, CURLINFO_RESPONSE_CODE, &code) == CURLE_OK && code == 404)
			return -1;
	}

	if (result == CURLE_GOT_NOTHING
		|| result == CURLE_HTTP_RETURNED_ERROR
		|| result == CURLE_RECV_ERROR /* some silly shoutcast servers */)
	{
		/* some http servers and shoutcast servers don't give us any data on a head request */
		/* request normal and just fail out, it's their loss */
		/* somehow curl doesn't reset CURLOPT_NOBODY properly so reset everything */
		SetCommonOptions(m_state);
		SetRequestHeaders(m_state);
		curl_easy_setopt(m_state->m_easyHandle, CURLOPT_TIMEOUT, 10);
		curl_easy_setopt(m_state->m_easyHandle, CURLOPT_RANGE, "0-0");
		curl_easy_setopt(m_state->m_easyHandle, CURLOPT_WRITEDATA, NULL); /* will cause write failure*/
		curl_easy_setopt(m_state->m_easyHandle, CURLOPT_FILETIME, 1);
		curl_easy_perform(m_state->m_easyHandle);
	}

	if (result == CURLE_HTTP_RANGE_ERROR)
	{
		/* crap can't use the range option, disable it and try again */
		curl_easy_setopt(m_state->m_easyHandle, CURLOPT_RANGE, NULL);
		result = curl_easy_perform(m_state->m_easyHandle);
	}

	if (result != CURLE_WRITE_ERROR && result != CURLE_OK)
	{
		CCurlGlobal::easy_release(&m_state->m_easyHandle, NULL);
		errno = ENOENT;
		LOGERR("CCurlFile::Stat - Failed: %s(%d) for %s", curl_easy_strerror(result), result, url.GetRedacted().c_str());
		return -1;
	}

	double length;
	result = curl_easy_getinfo(m_state->m_easyHandle, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &length);
	if (result != CURLE_OK || length < 0.0)
	{
		if (url.GetProtocol() == "ftp")
		{
			CCurlGlobal::easy_release(&m_state->m_easyHandle, NULL);
			LOGINFO("CCurlFile::Stat - Content length failed: %s(%d) for %s", curl_easy_strerror(result), result, url.GetRedacted().c_str());
			errno = ENOENT;
			return -1;
		}
		else
			length = 0.0;
	}

	SetCorrectHeaders(m_state);

	if (buffer)
	{
		char *content;
		result = curl_easy_getinfo(m_state->m_easyHandle, CURLINFO_CONTENT_TYPE, &content);
		if (result != CURLE_OK)
		{
			LOGINFO("CCurlFile::Stat - Content type failed: %s(%d) for %s", curl_easy_strerror(result), result, url.GetRedacted().c_str());
			CCurlGlobal::easy_release(&m_state->m_easyHandle, NULL);
			errno = ENOENT;
			return -1;
		}
		else
		{
			memset(buffer, 0, sizeof(struct __stat64));
			buffer->st_size = (int64_t)length;
			if (content && strstr(content, "text/html")) //consider html files directories
				buffer->st_mode = _S_IFDIR;
			else
				buffer->st_mode = _S_IFREG;
		}
		long filetime;
		result = curl_easy_getinfo(m_state->m_easyHandle, CURLINFO_FILETIME, &filetime);
		if (result != CURLE_OK)
		{
			LOGINFO("CCurlFile::Stat - Filetime failed: %s(%d) for %s", curl_easy_strerror(result), result, url.GetRedacted().c_str());
		}
		else
		{
			if (filetime != -1)
				buffer->st_mtime = filetime;
		}
	}
	CCurlGlobal::easy_release(&m_state->m_easyHandle, NULL);
	return 0;
}

unsigned int CCurlFile::CReadState::Read(void* lpBuf, int64_t uiBufSize)
{
	/* only request 1 byte, for truncated reads (only if not eof) */
	if ((m_fileSize == 0 || m_filePos < m_fileSize) && !FillBuffer(1))
		return 0;

	/* ensure only available data is considered */
	unsigned int want = (unsigned int)XMIN(m_buffer.getMaxReadSize(), uiBufSize);

	/* xfer data to caller */
	if (m_buffer.ReadData((char *)lpBuf, want))
	{
		m_filePos += want;
		return want;
	}

	/* check if we finished prematurely */
	if (!m_stillRunning && (m_fileSize == 0 || m_filePos != m_fileSize))
	{
		LOGWARN("%s - Transfer ended before entire file was retrieved pos %""I64d"", size %""I64d", __FUNCTION__, m_filePos, m_fileSize);
		return 0;
	}

	return 0;
}

/* use to attempt to fill the read buffer up to requested number of bytes */
bool CCurlFile::CReadState::FillBuffer(unsigned int want)
{
	int retry = 0;
	fd_set fdread;
	fd_set fdwrite;
	fd_set fdexcep;

	// only attempt to fill buffer if transactions still running and buffer
	// doesnt exceed required size already
	while ((unsigned int)m_buffer.getMaxReadSize() < want && m_buffer.getMaxWriteSize() > 0)
	{
		if (m_cancelled)
			return false;

		/* if there is data in overflow buffer, try to use that first */
		if (m_overflowSize)
		{
			unsigned amount = XMIN((unsigned int)m_buffer.getMaxWriteSize(), m_overflowSize);
			m_buffer.WriteData(m_overflowBuffer, amount);

			if (amount < m_overflowSize)
				memcpy(m_overflowBuffer, m_overflowBuffer + amount, m_overflowSize - amount);

			m_overflowSize -= amount;
			m_overflowBuffer = (char*)realloc_simple(m_overflowBuffer, m_overflowSize);
			continue;
		}

		CURLMcode result = curl_multi_perform(m_multiHandle, &m_stillRunning);
		if (!m_stillRunning)
		{
			if (result == CURLM_OK)
			{
				/* if we still have stuff in buffer, we are fine */
				if (m_buffer.getMaxReadSize())
					return true;

				/* verify that we are actually okey */
				int msgs;
				CURLcode CURLresult = CURLE_OK;
				CURLMsg* msg;
				while ((msg = curl_multi_info_read(m_multiHandle, &msgs)))
				{
					if (msg->msg == CURLMSG_DONE)
					{
						if (msg->data.result == CURLE_OK)
							return true;

						if (msg->data.result == CURLE_HTTP_RETURNED_ERROR)
						{
							long httpCode;
							curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &httpCode);
							LOGERR("CCurlFile::FillBuffer - Failed: HTTP returned error %ld", httpCode);
						}
						else
						{
							LOGERR("CCurlFile::FillBuffer - Failed: %s(%d)", curl_easy_strerror(msg->data.result), msg->data.result);
						}

						// We need to check the result here as we don't want to retry on every error
						if ((msg->data.result == CURLE_OPERATION_TIMEDOUT ||
							msg->data.result == CURLE_PARTIAL_FILE ||
							msg->data.result == CURLE_COULDNT_CONNECT ||
							msg->data.result == CURLE_RECV_ERROR) &&
							!m_bFirstLoop)
							CURLresult = msg->data.result;
						else if ((msg->data.result == CURLE_HTTP_RANGE_ERROR ||
							msg->data.result == CURLE_HTTP_RETURNED_ERROR) &&
							m_bFirstLoop                                   &&
							m_filePos == 0 &&
							m_sendRange)
						{
							// If server returns a range or http error, retry with range disabled
							CURLresult = msg->data.result;
							m_sendRange = false;
						}
						else
							return false;
					}
				}

				// Don't retry when we didn't "see" any error
				if (CURLresult == CURLE_OK)
					return false;

				// Close handle
				if (m_multiHandle && m_easyHandle)
					curl_multi_remove_handle(m_multiHandle, m_easyHandle);

				// Reset all the stuff like we would in Disconnect()
				m_buffer.Clear();
				free(m_overflowBuffer);
				m_overflowBuffer = NULL;
				m_overflowSize = 0;

				// If we got here something is wrong
				if (++retry > 2)
				{
					LOGERR("CCurlFile::FillBuffer - Reconnect failed!");
					// Reset the rest of the variables like we would in Disconnect()
					m_filePos = 0;
					m_fileSize = 0;
					m_bufferSize = 0;

					return false;
				}

				LOGINFO("CCurlFile::FillBuffer - Reconnect, (re)try %i", retry);

				// Connect + seek to current position (again)
				SetResume();
				curl_multi_add_handle(m_multiHandle, m_easyHandle);

				// Return to the beginning of the loop:
				continue;
			}
			return false;
		}

		// We've finished out first loop
		if (m_bFirstLoop && m_buffer.getMaxReadSize() > 0)
			m_bFirstLoop = false;

		switch (result)
		{
		case CURLM_OK:
		{
			int maxfd = 1;
			FD_ZERO(&fdread);
			FD_ZERO(&fdwrite);
			FD_ZERO(&fdexcep);

			// get file descriptors from the transfers
			CURLMcode code = curl_multi_fdset(m_multiHandle, &fdread, &fdwrite, &fdexcep, &maxfd);

			long timeout = 0;
			if (CURLM_OK != curl_multi_timeout(m_multiHandle, &timeout) || timeout == -1 || timeout<200)
				timeout = 200;

			XR::EndTime endTime(timeout);
			int rc;

			do
			{
				unsigned int time_left = endTime.MillisLeft();
				//struct timeval t = { (int)time_left / 1000, ((int)time_left % 1000) * 1000 };
				struct timeval t;
				t.tv_sec = 1;
				t.tv_usec = 0;

				// Wait until data is available or a timeout occurs.
				rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &t);

			} while (rc == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);

			if (rc == SOCKET_ERROR)
			{
				char buf[256];
				strerror_s(buf, 256, WSAGetLastError());
				LOGERR("CCurlFile::FillBuffer - Failed with socket error:%s", buf);

				return false;
			}
		}
		break;
		case CURLM_CALL_MULTI_PERFORM:
		{
			// we don't keep calling here as that can easily overwrite our buffer which we want to avoid
			// docs says we should call it soon after, but aslong as we are reading data somewhere
			// this aught to be soon enough. should stay in socket otherwise
			continue;
		}
		break;
		default:
		{
			LOGERR("CCurlFile::FillBuffer - Multi perform failed with code %d, aborting", result);
			return false;
		}
		break;
		}
	}
	return true;
}

void CCurlFile::CReadState::SetReadBuffer(const void* lpBuf, int64_t uiBufSize)
{
	m_readBuffer = (char*)lpBuf;
	m_fileSize = uiBufSize;
	m_filePos = 0;
}

void CCurlFile::ClearRequestHeaders()
{
	m_requestheaders.clear();
}

void CCurlFile::SetRequestHeader(std::string header, std::string value)
{
	m_requestheaders[header] = value;
}

void CCurlFile::SetRequestHeader(std::string header, long value)
{
	m_requestheaders[header] = StringUtils::Format("%ld", value);
}

bool CCurlFile::GetMimeType(const CURL &url, std::string &content, const std::string &useragent)
{
	CCurlFile file;
	if (!useragent.empty())
		file.SetUserAgent(useragent);

	struct __stat64 buffer;
	std::string redactUrl = url.GetRedacted();
	if (file.Stat(url, &buffer) == 0)
	{
		if (buffer.st_mode == _S_IFDIR)
			content = "x-directory/normal";
		else
			content = file.GetMimeType();
		LOGDEBUG("GetMimeType - %s -> %s", redactUrl.c_str(), content.c_str());
		return true;
	}
	LOGDEBUG("GetMimeType - %s -> failed", redactUrl.c_str());
	content.clear();
	return false;
}

std::string CCurlFile::GetServerReportedCharset(void)
{
	if (!m_state)
		return "";

	return m_state->m_httpheader.GetCharset();
}

/* STATIC FUNCTIONS */
bool CCurlFile::GetHttpHeader(const CURL &url, CHttpHeader &headers)
{
	try
	{
		CCurlFile file;
		if (file.Stat(url, NULL) == 0)
		{
			headers = file.GetHttpHeader();
			return true;
		}
		return false;
	}
	catch (...)
	{
		LOGERR("%s - Exception thrown while trying to retrieve header url: %s", __FUNCTION__, url.GetRedacted().c_str());
		return false;
	}
}

bool CCurlFile::GetCookies(const CURL &url, std::string &cookies)
{
	std::string		cookiesStr;
	struct curl_slist*     curlCookies;
	CURL_HANDLE*			easyHandle;
	CURLM*					multiHandle;

	// get the cookies list
	CCurlGlobal::easy_aquire(url.GetProtocol().c_str(),
		url.GetHostName().c_str(),
		&easyHandle, &multiHandle);
	if (CURLE_OK == curl_easy_getinfo(easyHandle, CURLINFO_COOKIELIST, &curlCookies))
	{
		// iterate over each cookie and format it into an RFC 2109 formatted Set-Cookie string
		struct curl_slist* curlCookieIter = curlCookies;
		while (curlCookieIter)
		{
			// tokenize the CURL cookie string
			std::vector<std::string> valuesVec;
			StringUtils::Tokenize(curlCookieIter->data, valuesVec, "\t");

			// ensure the length is valid
			if (valuesVec.size() < 7)
			{
				LOGERR("CCurlFile::GetCookies - invalid cookie: '%s'", curlCookieIter->data);
				curlCookieIter = curlCookieIter->next;
				continue;
			}

			// create a http-header formatted cookie string
			std::string cookieStr = valuesVec[5] + "=" + valuesVec[6] +
				"; path=" + valuesVec[2] +
				"; domain=" + valuesVec[0];

			// append this cookie to the string containing all cookies
			if (!cookiesStr.empty())
				cookiesStr += "\n";
			cookiesStr += cookieStr;

			// move on to the next cookie
			curlCookieIter = curlCookieIter->next;
		}

		// free the curl cookies
		curl_slist_free_all(curlCookies);

		// release our handles
		CCurlGlobal::easy_release(&easyHandle, &multiHandle);

		// if we have a non-empty cookie string, return it
		if (!cookiesStr.empty())
		{
			cookies = cookiesStr;
			return true;
		}
	}

	// no cookies to return
	return false;
}

int CCurlFile::IoControl(EIoControl request, void* param)
{
	if (request == IOCTRL_SEEK_POSSIBLE)
		return m_seekable ? 1 : 0;

	return -1;
}
