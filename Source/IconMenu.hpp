#ifndef IconMenu_hpp
#define IconMenu_hpp

#include "AudioEngine.h"
#include "HostIpcServer.h"
#include "UiProcessLifetime.h"

class IconMenu : public SystemTrayIconComponent, private MultiTimer
{
public:
    void showInterface();
    IconMenu(bool startInSafeMode = false, bool debugEnabled = false, bool restoreActivePluginsOnStartup = false);
    ~IconMenu() override;

    void mouseDown(const MouseEvent&) override;
    static void menuInvocationCallback(int id, IconMenu*);

	const int INDEX_OPEN_WINUI, INDEX_QUIT;
	static constexpr int INDEX_GLOBAL_MUTE = 900002, INDEX_GLOBAL_BYPASS = 900003, INDEX_CHAIN_PROFILE = 910000;
    void refreshTrayIcon();

private:
	enum TimerIds
	{
		menuTimerId = 1
	};

	void timerCallback(int timerId) override;
	void showNativeContextMenu();
	void openWinUI();
	void monitorWinUI(HANDLE process);
	bool openPackagedWinUI(const String& parameters);
	String resolvePackagedWinUIAumid();
	void setIcon();

	std::unique_ptr<AudioEngine> engine;
	std::unique_ptr<class HostWindow> hostWindow;
	std::unique_ptr<HostIpcServer> ipcServer;
    PopupMenu menu;
	bool debugMode = false;
	std::unique_ptr<lightHostModern::UiProcessLifetime> uiLifetime;
	int x = 0, y = 0;

};

#endif /* IconMenu_hpp */
