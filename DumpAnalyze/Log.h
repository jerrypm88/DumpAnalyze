#pragma once
#include <cstring>
#include <vector>
#include <iostream>
#include <sstream>
//#include <boost/shared_ptr.hpp>
//#include <boost/thread/mutex.hpp>

#include <memory>
#include <mutex>
#include <thread>

#define LOG_ASYN
#define LOG_ASYN						//异步写日志
#define MAX_LOG_SIZE 1073741824			//日志文件最大1G 1024 * 1024 * 1024

namespace Utils
{
	namespace detail
	{
		inline std::wstring toWide(const char* str)
		{
			size_t len = ::strlen(str);
			std::wstring wstr(len, 0);

			if (!wstr.empty())
			{
				int wlen = ::MultiByteToWideChar(CP_ACP, 0, str, static_cast<int>(len), &wstr[0], static_cast<int>(wstr.size()));
				wstr.resize(wlen);
			}

			return wstr;
		}

		//CP_UTF8
		inline std::string toNarrow(const std::wstring& wstr, long page)
		{
			std::string str(wstr.size() * sizeof(wchar_t), 0);

			if (!str.empty())
			{
				int len = ::WideCharToMultiByte(page, 0, wstr.c_str(), static_cast<int>(wstr.size()), &str[0], static_cast<int>(str.size()), 0, 0);
				str.resize(len);
			}

			return str;
		}

		inline void operator<<(std::wostringstream& stream, const char* data)
		{
			data = data ? data : "(null)";
			std::operator<<(stream, toWide(data));
		}

		inline void operator<<(std::wostringstream& stream, const std::string& data)
		{
			Utils::detail::operator<<(stream, data.c_str());
		}

		inline void operator<<(std::wostringstream& stream, const wchar_t* data)
		{
			data = data ? data : L"(null)";
			std::operator<<(stream, data);
		}

		inline void operator<<(std::wostringstream& stream, const std::wstring& data)
		{
			Utils::detail::operator<<(stream, data.c_str());
		}
	}

	class LogRecord
	{
	public:
		LogRecord()
		{
		}

		LogRecord& operator<<(char data)
		{
			char str[] = { data, 0 };
			return *this << str;
		}

		LogRecord& operator<<(wchar_t data)
		{
			wchar_t str[] = { data, 0 };
			return *this << str;
		}

		LogRecord& operator<<(std::wostream& (*data)(std::wostream&))
		{
			m_message << data;
			return *this;
		}

		template<typename T>
		LogRecord& operator<<(const T& data)
		{
			using namespace Utils::detail;

			m_message << data;
			return *this;
		}

		std::wstring getStr() const
		{
			return m_message.str();
		}

	private:
		std::wostringstream		m_message;
	};

	class Logger
	{
	public:
		virtual ~Logger(){}

		virtual bool isLogOn() = 0;

		virtual void operator+=(const LogRecord& LogRecord) = 0;

		static std::shared_ptr<Logger> getInstance();

	private:
		static std::shared_ptr<Logger>	instance_;
		static std::mutex					inst_mu_;
	};
}

#define LOG if (Utils::Logger::getInstance()->isLogOn()) (*Utils::Logger::getInstance()) += Utils::LogRecord()

//计时日志
class TimerObject
{
public:
	explicit TimerObject(const std::string& strMsg) :strMsg_(strMsg)
	{
		LOG << strMsg_ << " Enter...";
		//获取每秒多少CPU Performance Tick 
		QueryPerformanceFrequency(&pff_); 
		QueryPerformanceCounter(&startTime_);
	}

	~TimerObject()
	{
		try
		{
			LARGE_INTEGER endTime_;
			QueryPerformanceCounter(&endTime_);
			LOG << strMsg_ << " Exit... total time: " << (endTime_.QuadPart-startTime_.QuadPart)*1000000/pff_.QuadPart << " ns";
		}
		catch (...)
		{
		}
	}

private:
	std::string strMsg_;
	LARGE_INTEGER pff_;
	LARGE_INTEGER startTime_;

};

#ifdef _DEBUG
	#define FUNC_TRACE(strMsg) TimerObject timerObj(strMsg);
#else
	#define FUNC_TRACE(strMsg)
#endif