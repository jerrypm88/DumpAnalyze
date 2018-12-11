#include "stdafx.h"
#include "Log.h"
#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <io.h>
#include <time.h>
#include <sys/timeb.h>

//处理日志开关、日志文件路径时使用
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#ifdef LOG_ASYN
	//#include <boost/thread/thread.hpp>
#include <atomic>
#endif

namespace Utils
{
	class File
	{
	public:
		File() : m_file(-1)
		{
		}

		~File()
		{
			close();
		}

		off_t open(const wchar_t* fileName)
		{
			errno_t ret = ::_wsopen_s(&m_file, fileName, _O_CREAT | _O_WRONLY | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
			if (0 != ret)
			{
				std::wostringstream oss;
				oss << L"open " << fileName << L" error. [" << ret << L"]\r\n";
				OutputDebugStringW(oss.str().c_str());
			}

			return seek(0, SEEK_END);
		}

		int write(const void* buf, size_t count)
		{
			return m_file != -1 ? ::_write(m_file, buf, static_cast<unsigned int>(count)) : -1;
		}

		template<class CharType>
		int write(const std::basic_string<CharType>& str)
		{
			return write(str.data(), str.size() * sizeof(CharType));
		}

		off_t seek(off_t offset, int whence)
		{
			return m_file != -1 ? ::_lseek(m_file, offset, whence) : -1;
		}

		void close()
		{
			if (m_file != -1)
			{
				::_close(m_file);
				m_file = -1;
			}
		}

		static int unlink(const wchar_t* fileName)
		{
			return ::_wunlink(fileName);
		}
	private:
		int m_file;
	};

	class LogAppender
	{
	public:
		LogAppender(const wchar_t* fileName)
			: m_fileSize(0)
			, m_firstWrite(true)
			, m_fileName(fileName)
		{
		}

		virtual void write(const std::wstring& logRecord)
		{
			if (m_firstWrite)
			{
				openLogFile();
				m_firstWrite = false;
			}

			if (m_fileSize > MAX_LOG_SIZE)
			{
				clearLogFiles();
			}

			int bytesWritten = m_file.write(detail::toNarrow(logRecord, CP_UTF8));

			if (bytesWritten > 0)
			{
				m_fileSize += bytesWritten;
			}
			else
			{
				OutputDebugStringW(logRecord.c_str());
			}
		}

	private:
		void clearLogFiles()
		{
			m_file.close();

			File::unlink(m_fileName.c_str());
			openLogFile();
		}

		void openLogFile()
		{
			m_fileSize = m_file.open(m_fileName.c_str());

			if (0 == m_fileSize)
			{
				int bytesWritten = m_file.write(std::string("\xEF\xBB\xBF"));

				if (bytesWritten > 0)
				{
					m_fileSize += bytesWritten;
				}
			}
		}

	private:
		off_t           m_fileSize;
		bool            m_firstWrite;
		std::wstring	m_fileName;
		File			m_file;
	};

	class LoggerImpl : public Logger
	{
	public:
		LoggerImpl() :logOn_(false), appender_(NULL),
			quit_flag_(false)
		{
			initLog();
		}

		virtual ~LoggerImpl()
		{
			if (logOn_)
			{
#ifdef LOG_ASYN
				quit_flag_ = true;
				thrd_.join();
#endif
				if (appender_)
				{
					appender_->write(getLogHead() + L"---------------------log end------------------------------\r\n");
					delete appender_;
					appender_ = NULL;
				}
			}
		}

		bool isLogOn()
		{
			return logOn_;
		}

		void operator+=(const LogRecord& logRecord)
		{
			std::wstring wmsg = getLogHead() + logRecord.getStr() + L"\r\n";

			std::lock_guard<std::mutex> lock(mu_);
#ifdef LOG_ASYN
			logList_.push_back(wmsg);
#else
			appender_->write(wmsg);
#endif
		}

	private:
		void initLog()
		{
 			WCHAR szLogFile[MAX_PATH] = {};
 			GetModuleFileName(NULL, szLogFile, MAX_PATH);
 			PathRemoveFileSpec(szLogFile);
 
			std::wstring szFrom;
			CCommandLine::getInstance().getOption(L"from", szFrom);
			if (szFrom.empty()) szFrom = L"ldstray";

			//PathAppend(szLogFile, CA2W(g_strWorkingFolder.c_str()));
			PathAppend(szLogFile, std::wstring(szFrom + L".log.on").c_str());
#ifndef _DEBUG
			if (!PathFileExists(szLogFile))
			{
				logOn_ = false;
				return;
			}
#endif
			logOn_ = true;
			PathRemoveFileSpec(szLogFile);
			PathAppend(szLogFile, std::wstring(szFrom + L".log").c_str());

			appender_ = new LogAppender(szLogFile);
			appender_->write(getLogHead() + L"---------------------log start----------------------------\r\n");
#ifdef LOG_ASYN
			thrd_ = std::thread(&LoggerImpl::writeAsyn, this);
#endif
		}

		std::wstring getLogHead()
		{
			WCHAR szBuffer[100] = { 0 };
			timeb curtime;
			tm t;
			::ftime(&curtime);
			::localtime_s(&t, &curtime.time);
			wsprintf(szBuffer, L"%04d-%02d-%02d %02d:%02d:%02d.%03d [%5d][%5d] "
				, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday
				, t.tm_hour, t.tm_min, t.tm_sec, curtime.millitm
				, GetCurrentProcessId(), GetCurrentThreadId());
			return szBuffer;
		}

#ifdef LOG_ASYN
		void writeAsyn()
		{
			while (true)
			{
				if (quit_flag_) break;

				std::vector<std::wstring> logList;
				{
					std::lock_guard<std::mutex> lock(mu_);
					logList.swap(logList_);
				}

				if (logList.empty())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					continue;
				}

				for (size_t i = 0; i < logList.size(); i++)
				{
					appender_->write(logList[i]);
				}
				logList.clear();
			}
		}

#endif

	private:
		bool								logOn_;
		std::mutex						mu_;
		LogAppender*						appender_;
#ifdef LOG_ASYN
		std::vector<std::wstring>			logList_;
		std::thread						thrd_;
		std::atomic_bool                quit_flag_;
#endif
	};

	std::shared_ptr<Logger> Logger::instance_;
	std::mutex Logger::inst_mu_;

	std::shared_ptr<Logger> Logger::getInstance()
	{
		if (!instance_.get())
		{
			std::lock_guard<std::mutex> lock(inst_mu_);
			if (!instance_.get())
			{
				LoggerImpl *pTemp = new LoggerImpl();
				if (pTemp)
				{
					MemoryBarrier();
					instance_ = std::shared_ptr<Logger>(pTemp);
				}
			}
		}

		return instance_;
	}
}