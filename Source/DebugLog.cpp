#include "DebugLog.h"
#include "RuntimeProfile.h"
#include "VerboseLog.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <Windows.h>
 #include <cstdio>
 #include <iostream>
#endif

#include <atomic>
#include <cstdlib>
#include <exception>

namespace
{
	bool debugEnabled = false;
	bool consoleOpened = false;
	bool crashDiagnosticsInstalled = false;
	CriticalSection crashContextLock;
	CriticalSection debugFileLock;
	String crashContext;
	File debugLogFile;
	std::atomic<bool> fatalCrashLogged { false };

	File createDebugLogFile()
	{
		auto logsDirectory = File::getSpecialLocation(File::userApplicationDataDirectory)
			.getChildFile("LightHostModern")
			.getChildFile("Logs");

		const auto& profile = lightHostModern::RuntimeProfile::current();
		if (profile.test) logsDirectory = File((profile.directory / L"Logs").wstring().c_str());
		logsDirectory.createDirectory();

		const auto timestamp = Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
		return logsDirectory.getChildFile("LightHostModern-debug-" + timestamp + "-host-" + String((int) GetCurrentProcessId()) + ".txt");
	}

	void appendDebugLogLine(const String& line)
	{
		if (!debugEnabled)
			return;

		const ScopedLock lock(debugFileLock);

		if (debugLogFile == File())
			debugLogFile = createDebugLogFile();

		debugLogFile.appendText(line + newLine, false, false, "\n");
	}

	class LightHostModernDebugLogger final : public Logger
	{
	public:
        void logMessage(const String& message) override
        {
            lightHostModern::verbose::log("juce",message.toStdString());
			if (!debugEnabled)
				return;

			const auto line = "[LightHostModernLogger] " + message;
			appendDebugLogLine(line);

			if (consoleOpened)
				std::cout << line << std::endl;
		}
	};

	LightHostModernDebugLogger debugLogger;

	void writeFatalCrashLog(const String& reason, void* address = nullptr, uint32 code = 0)
	{
		if (!debugEnabled || fatalCrashLogged.exchange(true))
			return;

		const ScopedLock lock(crashContextLock);
		lightHostModernLog("Fatal crash diagnostic: " + reason);

		if (code != 0)
			lightHostModernLog("Fatal crash exception code: 0x" + String::toHexString((int) code));

		if (address != nullptr)
			lightHostModernLog("Fatal crash address: " + String::toHexString((pointer_sized_int) address));

		if (crashContext.isNotEmpty())
			lightHostModernLog("Fatal crash context: " + crashContext);

		std::cout.flush();
	}

#if JUCE_WINDOWS
	LONG WINAPI lightHostModernUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
	{
        lightHostModern::verbose::fatalException(exceptionInfo);
		uint32 code = 0;
		void* address = nullptr;

		if (exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr)
		{
			code = (uint32) exceptionInfo->ExceptionRecord->ExceptionCode;
			address = exceptionInfo->ExceptionRecord->ExceptionAddress;
		}

		writeFatalCrashLog("Unhandled native exception", address, code);
		return EXCEPTION_CONTINUE_SEARCH;
	}
#endif

	void lightHostModernTerminateHandler()
	{
		writeFatalCrashLog("std::terminate called");
		std::abort();
	}
}

void setLightHostModernDebugEnabled(bool enabled)
{
	debugEnabled = enabled;

	if (debugEnabled)
	{
		const ScopedLock lock(debugFileLock);
		if (debugLogFile == File())
		{
			debugLogFile = createDebugLogFile();
			debugLogFile.replaceWithText("LightHostModern debug log" + newLine, false, false, "\n");
			debugLogFile.appendText("Started: " + Time::getCurrentTime().toString(true, true, true, true) + newLine, false, false, "\n");
			debugLogFile.appendText("Process: " + File::getSpecialLocation(File::currentExecutableFile).getFullPathName() + newLine, false, false, "\n");
			debugLogFile.appendText(newLine, false, false, "\n");
		}

		Logger::setCurrentLogger(&debugLogger);
	}
    else if (lightHostModern::verbose::logger().active())
        Logger::setCurrentLogger(&debugLogger);
    else if (Logger::getCurrentLogger() == &debugLogger)
	{
        Logger::setCurrentLogger(nullptr);
	}
}

bool isLightHostModernDebugEnabled()
{
	return debugEnabled;
}

String getLightHostModernDebugLogPath()
{
	const ScopedLock lock(debugFileLock);
	return debugLogFile.getFullPathName();
}

void openLightHostModernDebugConsoleIfNeeded()
{
#if JUCE_WINDOWS
	if (!debugEnabled || consoleOpened)
		return;

	if (!AllocConsole())
		return;

	consoleOpened = true;
	SetConsoleTitleW(L"LightHostModern Debug Console");

	FILE* stream = nullptr;
	freopen_s(&stream, "CONOUT$", "w", stdout);
	freopen_s(&stream, "CONOUT$", "w", stderr);
	freopen_s(&stream, "CONIN$", "r", stdin);

	std::ios::sync_with_stdio(true);
	std::cout.clear();
	std::cerr.clear();
	std::clog.clear();

	lightHostModernLog("Debug console opened.");
	lightHostModernLog("Debug log file: " + getLightHostModernDebugLogPath());
#endif
}

void installLightHostModernCrashDiagnostics()
{
	if (!debugEnabled || crashDiagnosticsInstalled)
		return;

	crashDiagnosticsInstalled = true;
	std::set_terminate(lightHostModernTerminateHandler);

#if JUCE_WINDOWS
	SetUnhandledExceptionFilter(lightHostModernUnhandledExceptionFilter);
#endif

	lightHostModernLog("Crash diagnostics installed.");
}

void lightHostModernLog(const String& message)
{
    lightHostModern::verbose::log("host",message.toStdString());
	if (!debugEnabled)
		return;

	const auto line = "[LightHostModern] " + message;
	appendDebugLogLine(line);
	std::cout << line << std::endl;
}

void setLightHostModernCrashContext(const String& context)
{
	if (!debugEnabled)
		return;

	const ScopedLock lock(crashContextLock);
	crashContext = context;
}

void clearLightHostModernCrashContext()
{
	if (!debugEnabled)
		return;

	const ScopedLock lock(crashContextLock);
	crashContext.clear();
}
