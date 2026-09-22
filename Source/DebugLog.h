#ifndef DebugLog_h
#define DebugLog_h

#include <JuceHeader.h>

void setLightHostModernDebugEnabled(bool enabled);
bool isLightHostModernDebugEnabled();
String getLightHostModernDebugLogPath();
void openLightHostModernDebugConsoleIfNeeded();
void lightHostModernLog(const String& message);
void installLightHostModernCrashDiagnostics();
void setLightHostModernCrashContext(const String& context);
void clearLightHostModernCrashContext();

#endif /* DebugLog_h */
