#include <JuceHeader.h>
#include "IconMenu.hpp"
#include "DebugLog.h"
#include "RuntimeProfile.h"
#include "LightHostLocales.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Windows.h"
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

#pragma comment(lib, "ole32.lib")

namespace
{
	var trayLocale()
	{
		const auto settings = File(lightHost::RuntimeProfile::current().uiSettings().wstring().c_str());
		wchar_t language[32] {};
		GetPrivateProfileStringW(L"Localization", L"Language", L"en-us", language, 32, settings.getFullPathName().toWideCharPointer());
		const String fileName = String(language).equalsIgnoreCase("pt-br") ? "pt-br.json" : "en-us.json";
		for (int i = 0; i < LightHostLocales::namedResourceListSize; ++i)
			if (String(LightHostLocales::originalFilenames[i]).endsWith(fileName))
			{
				int size = 0;
				const auto* data = LightHostLocales::getNamedResource(LightHostLocales::namedResourceList[i], size);
				return JSON::parse(String::fromUTF8(data, size));
			}
		return {};
	}

	HWND findWinUIWindow()
	{
		return FindWindowW(nullptr, lightHost::RuntimeProfile::current().windowTitle().c_str());
	}

	bool focusWinUIWindow()
	{
		if (auto* hwnd = findWinUIWindow())
		{
			if (IsIconic(hwnd))
				ShowWindow(hwnd, SW_RESTORE);
			else
				ShowWindow(hwnd, SW_SHOW);

			SetForegroundWindow(hwnd);
			return true;
		}

		return false;
	}

	void closeWinUIWindow()
	{
		if (auto* hwnd = findWinUIWindow())
			PostMessageW(hwnd, WM_CLOSE, 0, 0);
	}
}

IconMenu::IconMenu(bool startInSafeMode, bool debugEnabled, bool restoreActivePluginsOnStartup)
	: INDEX_OPEN_WINUI(900000),
	  INDEX_QUIT(900001),
	  engine(std::make_unique<AudioEngine>(startInSafeMode, restoreActivePluginsOnStartup)),
	  debugMode(debugEnabled)
{
	ipcServer = std::make_unique<HostIpcServer>(*engine, [this] { setIcon(); });
	lightHostLog("IconMenu created. safeMode=" + String(startInSafeMode ? "true" : "false")
		+ " restoreActivePluginsOnStartup=" + String(restoreActivePluginsOnStartup ? "true" : "false"));
	setIcon();
	setIconTooltip(String(lightHost::RuntimeProfile::current().windowTitle().c_str()));
}

IconMenu::~IconMenu()
{
	stopTimer(menuTimerId);
	closeWinUIWindow();

	if (engine != nullptr)
		engine->flushPendingSaves();
}

void IconMenu::setIcon()
{
	if (!getAppProperties().getUserSettings()->containsKey("trayIconMode"))
		getAppProperties().getUserSettings()->setValue("trayIconMode", "color");

	String color = getAppProperties().getUserSettings()->getValue("trayIconMode",
		getAppProperties().getUserSettings()->getValue("icon", "color")).toLowerCase();
	Image icon;

	if (color.equalsIgnoreCase("white"))
		icon = ImageFileFormat::loadFrom(BinaryData::logowhite_png, BinaryData::logowhite_pngSize);
	else if (color.equalsIgnoreCase("black"))
		icon = ImageFileFormat::loadFrom(BinaryData::logoblack_png, BinaryData::logoblack_pngSize);
	else
		icon = ImageFileFormat::loadFrom(BinaryData::logo_png, BinaryData::logo_pngSize);

	setIconImage(icon, icon);
}

void IconMenu::timerCallback(int timerId)
{
	if (timerId != menuTimerId)
		return;

	stopTimer(menuTimerId);
	showNativeContextMenu();
}

void IconMenu::mouseDown(const MouseEvent& e)
{
    Process::makeForegroundProcess();
	if (e.mods.isLeftButtonDown())
	{
		openWinUI();
		return;
	}

	showNativeContextMenu();
}

void IconMenu::menuInvocationCallback(int id, IconMenu* im)
{
	if (im == nullptr || im->engine == nullptr)
		return;

    if (id == im->INDEX_OPEN_WINUI)
	{
        im->openWinUI();
		return;
	}
	if (id == im->INDEX_QUIT)
	{
		im->engine->savePluginStates();
		im->engine->flushPendingSaves();
		closeWinUIWindow();
		JUCEApplication::getInstance()->quit();
	}
}

void IconMenu::showNativeContextMenu()
{
	POINT iconLocation {};
	GetCursorPos(&iconLocation);

	HMENU nativeMenu = CreatePopupMenu();
	if (nativeMenu == nullptr)
		return;

	const auto locale = trayLocale();
	const auto label = [&locale](const char* key, const char* fallback) {
		const auto value = locale[key].toString();
		return value.isEmpty() ? String(fallback) : value;
	};
	AppendMenuW(nativeMenu, MF_STRING, INDEX_OPEN_WINUI, label("tray.openUi", "Open app UI").toWideCharPointer());
	AppendMenuW(nativeMenu, MF_STRING | (engine->isGlobalMuted() ? MF_CHECKED : 0), INDEX_GLOBAL_MUTE,
		label("audio.globalMute", "Mute output").toWideCharPointer());
	AppendMenuW(nativeMenu, MF_STRING | (engine->isGlobalBypassed() ? MF_CHECKED : 0), INDEX_GLOBAL_BYPASS,
		label("audio.globalBypass", "Bypass chain").toWideCharPointer());
	AppendMenuW(nativeMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(nativeMenu, MF_STRING, INDEX_QUIT, label("tray.quit", "Quit").toWideCharPointer());

	HWND owner = GetForegroundWindow();
	if (owner == nullptr)
		owner = GetDesktopWindow();

	SetForegroundWindow(owner);
	const UINT command = TrackPopupMenu(nativeMenu,
		TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
		iconLocation.x,
		iconLocation.y,
		0,
		owner,
		nullptr);

	DestroyMenu(nativeMenu);
	if (command == INDEX_GLOBAL_MUTE) { engine->setGlobalMuted(!engine->isGlobalMuted()); return; }
	if (command == INDEX_GLOBAL_BYPASS) { engine->setGlobalBypassed(!engine->isGlobalBypassed()); return; }

	if (command == (UINT) INDEX_OPEN_WINUI)
	{
		openWinUI();
		return;
	}

	if (command == (UINT) INDEX_QUIT)
	{
		if (engine != nullptr)
		{
			engine->savePluginStates();
			engine->flushPendingSaves();
		}
		closeWinUIWindow();
		JUCEApplication::getInstance()->quit();
	}
}

void IconMenu::openWinUI()
{
	lightHostLog("Open New UI clicked.");

	if (focusWinUIWindow())
	{
		lightHostLog("Focused existing WinUI window.");
		return;
	}

	String parameters = "--host-pipe=\"" + ipcServer->getPipeName() + "\"";
	parameters << String(lightHost::RuntimeProfile::current().arguments().c_str());
	if (debugMode)
	{
		parameters << " --debug";
		const auto logPath = getLightHostDebugLogPath();
		if (logPath.isNotEmpty())
			parameters << " --debug-log=\"" << logPath << "\"";
	}

	const auto executableName = "LightHostWinUI.exe";
	Array<File> searchRoots;
	const auto executableDirectory = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory();
	// Packaged UI belongs to this host. Development fallbacks must identify a
	// repository; an unrelated current directory must not select another build.
	searchRoots.add(executableDirectory);
	auto current = executableDirectory;
	for (int i = 0; i < 8; ++i)
	{
		if (current.getChildFile("WinUI/LightHost.WinUI.sln").existsAsFile()) searchRoots.addIfNotAlreadyThere(current);
		const auto parent = current.getParentDirectory();
		if (parent == current) break;
		current = parent;
	}

	for (auto root : searchRoots)
	{
		lightHostLog("Search root: " + root.getFullPathName());

		StringArray configurations;
		if (debugMode)
		{
			configurations.add("Debug");
			configurations.add("Release");
		}
		else
		{
			configurations.add("Release");
			configurations.add("Debug");
		}

		for (const auto& configuration : configurations)
		{
			Array<File> candidates;
			// Match LightHost.Output.props and the distribution layout first.
			candidates.add(root.getChildFile("WinUI")
				.getChildFile("x64")
				.getChildFile(configuration)
				.getChildFile("LightHost.WinUI")
				.getChildFile(executableName));
			candidates.add(root.getChildFile("LightHost.WinUI").getChildFile(executableName));
			candidates.add(root.getChildFile("WinUI").getChildFile("LightHost.WinUI").getChildFile(executableName));
			candidates.add(root.getChildFile("WinUI").getChildFile(executableName));

			for (const auto& candidate : candidates)
			{
				lightHostLog("Checking WinUI candidate: " + candidate.getFullPathName());

				if (candidate.existsAsFile())
				{
					lightHostLog("Found WinUI executable.");

					const auto workingDirectory = candidate.getParentDirectory();
					const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,
						L"open",
						candidate.getFullPathName().toWideCharPointer(),
						parameters.toWideCharPointer(),
						workingDirectory.getFullPathName().toWideCharPointer(),
						SW_SHOWNORMAL));

					lightHostLog("ShellExecute result: " + String((int) result));

					if (result <= 32)
					{
						const auto locale = trayLocale();
						auto message = locale["tray.uiLaunchFailed"].toString();
						if (message.isEmpty()) message = "Could not open the application interface. Error: {code}";
						AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
							"Light Host Modern",
							message.replace("{code}", String((int) result)));
					}

					return;
				}
			}
		}
	}

	lightHostLog("Loose WinUI build not found; trying packaged WinUI app.");
	if (openPackagedWinUI(parameters))
		return;

	lightHostLog("WinUI executable not found.");
	const auto locale = trayLocale();
	auto message = locale["tray.uiMissing"].toString();
	if (message.isEmpty()) message = "The application interface was not found. Repair the installation or extract the complete portable package.";

	AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
		"Light Host Modern",
		message);
}

bool IconMenu::openPackagedWinUI(const String& parameters)
{
	const auto aumid = resolvePackagedWinUIAumid();

	if (aumid.isEmpty())
	{
		lightHostLog("Packaged WinUI app not registered; falling back to loose executable.");
		return false;
	}

	lightHostLog("Found packaged WinUI AUMID: " + aumid);

	const auto coInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const bool shouldUninitialise = SUCCEEDED(coInitResult);

	if (FAILED(coInitResult) && coInitResult != RPC_E_CHANGED_MODE)
	{
		lightHostLog("CoInitializeEx failed. HRESULT=0x" + String::toHexString(static_cast<int>(coInitResult)));
		return false;
	}

	IApplicationActivationManager* activationManager = nullptr;
	const auto createResult = CoCreateInstance(CLSID_ApplicationActivationManager,
		nullptr,
		CLSCTX_LOCAL_SERVER,
		IID_PPV_ARGS(&activationManager));

	if (FAILED(createResult) || activationManager == nullptr)
	{
		lightHostLog("IApplicationActivationManager creation failed. HRESULT=0x" + String::toHexString(static_cast<int>(createResult)));

		if (shouldUninitialise)
			CoUninitialize();

		return false;
	}

	DWORD processId = 0;
	const auto activationResult = activationManager->ActivateApplication(aumid.toWideCharPointer(),
		parameters.toWideCharPointer(),
		AO_NONE,
		&processId);

	activationManager->Release();

	if (shouldUninitialise)
		CoUninitialize();

	if (FAILED(activationResult))
	{
		lightHostLog("Packaged WinUI activation failed. HRESULT=0x" + String::toHexString(static_cast<int>(activationResult)));
		return false;
	}

	lightHostLog("Packaged WinUI activated. pid=" + String(static_cast<int>(processId)));
	return true;
}

String IconMenu::resolvePackagedWinUIAumid()
{
	ChildProcess process;
	const String command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"$p = Get-AppxPackage -Name LightHost.WinUI | Select-Object -First 1; if ($null -ne $p) { [Console]::Out.Write($p.PackageFamilyName + '!App') }\"";

	if (!process.start(command, ChildProcess::wantStdOut | ChildProcess::wantStdErr))
	{
		lightHostLog("Failed to start PowerShell to resolve packaged WinUI AUMID.");
		return {};
	}

	if (!process.waitForProcessToFinish(5000))
	{
		process.kill();
		lightHostLog("Timed out while resolving packaged WinUI AUMID.");
		return {};
	}

	const auto output = process.readAllProcessOutput().trim();
	const auto exitCode = process.getExitCode();

	if (exitCode != 0)
	{
		lightHostLog("PowerShell failed while resolving packaged WinUI AUMID. exitCode=" + String(exitCode) + " output=" + output);
		return {};
	}

	return output;
}
