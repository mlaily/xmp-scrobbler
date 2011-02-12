--- orig/xmp-scrobbler.cpp	Tue Jun 26 16:23:32 2007
+++ xmp-scrobbler.cpp		Sat Feb 12 21:22:23 2011
@@ -1,8 +1,11 @@
 // xmp-scrobbler
-
+
+#define WIN32_LEAN_AND_MEAN
 #include <windows.h>
+#include <shellapi.h>
 #include <commctrl.h>
 
+#include <cstdlib>
 #include <vector>
 #include <string>
 #include <iostream>
@@ -15,10 +18,11 @@
 
 #include "libscrobbler\scrobbler.h"
 
-#define XMPSCROBBLER_VERSION		"0.8"
+#define XMPSCROBBLER_VERSION		"Pre-0.9.7"
 #define MAX_LENGTH					240		// in seconds
-#define DELAY_BEFORE_FETCHINFO		10		// in seconds
-#define DELAY_BEFORE_RESET			5		// in seconds
+#define DELAY_BEFORE_RESET       	5        // in seconds
+#define DEFAULT_FETCH_DELAY      	10       // in seconds
+#define DEFAULT_FETCH_DELAY_STR  	"10"     // in seconds
 
 HINSTANCE ghInstance;
 DWORD instID = 0;
@@ -42,11 +46,13 @@ int infoCounter = -1;    // info counter - used to hav
 
 int cueCounter = -1;
 
-bool isWinNT = false;   // TRUE if running on WinNT with Unicode
-bool bProcessed = false;
+//bool isWinNT = false;   // TRUE if running on WinNT with Unicode
 
 typedef struct {
-	BOOL on;
+	BOOL on, delayFetch, everConfigured;
+	
+	char delayStr[4];
+	unsigned short int fetchDelay;
 
 	char username[255];
 	char password[255];
@@ -67,6 +73,28 @@ typedef struct {
 
 } XMPScrobblerConfig;
 
+typedef struct {
+	BOOL on;
+
+	char username[255];
+	char password[255];
+
+	BOOL proxy_enabled;
+	BOOL proxy_auth_enabled;
+
+	char proxy_server[255];
+	char proxy_port[10];
+
+	char proxy_user[255];
+	char proxy_password[255];
+	
+	BOOL logfile_limit;
+	int logfile_limit_size;
+	BOOL logfile_truncate;
+	BOOL logfile_debugmode;
+
+} XMPScrobblerOldConfig;
+
 // All played music can be represented as two-level array.
 // First level is FILE. Second one - TRACK. Most FILEs have only one TRACK inside,
 // but some of them has CUE sheet attached and describing which TRACKs are inside.
@@ -93,7 +121,7 @@ typedef struct {
 	int current_track;  // Last track played
 	int length;      // Total length (do we need it?)
 	
-	bool dirty;
+	bool dirty, resumed;
 
 	char album[TAG_FIELD_SIZE]; // Album name
 	char mb[TAG_FIELD_SIZE];    // MusicBrainz ID
@@ -136,7 +164,6 @@ void XMP_UpdateCurrentTrackIndex();
 
 void XMP_SetDirty();
 bool XMP_IsDirty();
-bool XMP_WasResetDirty();
 
 void XMP_ClearXMPFile();
 void XMP_Welcome();
@@ -200,9 +227,6 @@ void WINAPI DSP_Free(void *inst)
 	{
 		XMP_ScrobTerm();
 	}
-
-	XMP_Log("[DEBUG] DSP_Free()\n");
-
 	XMP_Log("[INFO] Shutting down...\n");
 	XMP_Log("----\n");
 }
@@ -225,10 +249,35 @@ BOOL CALLBACK DSPDialogProc(HWND h, UINT m, WPARAM w, 
 				case IDOK:
 				{
 					xmpcfg.on = MESS(IDC_ENABLE, BM_GETCHECK, 0, 0);
+					
+				// Delay info fetch stuff:
+				if (xmpcfg.delayFetch = MESS(IDC_INFO, BM_GETCHECK, 0, 0)) {
+					MESS(IDC_DELAY, WM_GETTEXT, 4, xmpcfg.delayStr);
+					if (xmpcfg.delayStr[0] == '\0') {
+						xmpcfg.delayFetch = false;
+						xmpcfg.fetchDelay = 0;
+					} else {
+						xmpcfg.fetchDelay = atoi(xmpcfg.delayStr);
+					}
+				} else {
+					xmpcfg.fetchDelay = 0;
+					memset(xmpcfg.delayStr, 0, 4);
+				}
 
-					MESS(IDC_USERNAME, WM_GETTEXT, 255, xmpcfg.username);
-					MESS(IDC_PASSWORD, WM_GETTEXT, 255, xmpcfg.password);
-
+					char newCredentials[255] = {0};
+					MESS(IDC_PASSWORD, WM_GETTEXT, 255, newCredentials);
+					if (strcmp(xmpcfg.password, newCredentials)) {
+						memcpy(xmpcfg.password, newCredentials, 255);
+						if (scrob)
+							scrob->setPassword(xmpcfg.password);
+					}
+					MESS(IDC_USERNAME, WM_GETTEXT, 255, newCredentials);
+					if (strcmp(xmpcfg.username, newCredentials)) {
+						memcpy(xmpcfg.username, newCredentials, 255);
+						if (scrob)
+							scrob->setUsername(xmpcfg.username);
+					}
+					
 					MESS(IDC_PROXY_SERVER, WM_GETTEXT, 255, xmpcfg.proxy_server);
 					MESS(IDC_PROXY_PORT, WM_GETTEXT, 10, xmpcfg.proxy_port);
 
@@ -244,6 +293,7 @@ BOOL CALLBACK DSPDialogProc(HWND h, UINT m, WPARAM w, 
 
 					xmpcfg.logfile_limit_size = MESS(IDC_LOGFILE_LIMITS, CB_GETCURSEL, 0, 0);
 
+					xmpcfg.everConfigured = true;
 					EndDialog( h, 1 );
 
 					break;
@@ -255,12 +305,18 @@ BOOL CALLBACK DSPDialogProc(HWND h, UINT m, WPARAM w, 
 					break;
 				}
 
-				case IDC_ENABLE:
+				/*case IDC_ENABLE:
 				{
 					//xmpcfg.on = MESS(IDC_ENABLE, BM_GETCHECK, 0, 0);
 					//MESS(IDC_PROXY_AUTH, WM_ENABLE, 0, 0);
 
 					break;
+				} */
+				
+				case IDC_INFO: {
+					EnableWindow(GetDlgItem(h, IDC_DELAY), MESS(IDC_INFO, BM_GETCHECK, 0, 0));
+
+					break;
 				}
 
 				case IDC_PROXY_ENABLE:
@@ -318,6 +374,16 @@ BOOL CALLBACK DSPDialogProc(HWND h, UINT m, WPARAM w, 
 			MESS(IDC_ENABLE, BM_SETCHECK, xmpcfg.on ? 1 : 0, 0);
 			MESS(IDC_PROXY_ENABLE, BM_SETCHECK, xmpcfg.proxy_enabled ? 1 : 0, 0);
 			MESS(IDC_PROXY_AUTH, BM_SETCHECK, xmpcfg.proxy_auth_enabled ? 1 : 0, 0);
+			
+			if (xmpcfg.everConfigured) {
+				MESS(IDC_INFO, BM_SETCHECK, xmpcfg.delayFetch ? 1 : 0, 0);
+				EnableWindow(GetDlgItem(h, IDC_DELAY), xmpcfg.delayFetch);
+            if (xmpcfg.delayFetch)
+				MESS(IDC_DELAY, WM_SETTEXT, 0, xmpcfg.delayStr);
+			} else {
+				MESS(IDC_INFO, BM_SETCHECK, 1, 0);
+				MESS(IDC_DELAY, WM_SETTEXT, 0, DEFAULT_FETCH_DELAY_STR);
+			}
 
 			MESS(IDC_USERNAME, WM_SETTEXT, 0, xmpcfg.username);
 			MESS(IDC_PASSWORD, WM_SETTEXT, 0, xmpcfg.password);
@@ -341,13 +407,13 @@ BOOL CALLBACK DSPDialogProc(HWND h, UINT m, WPARAM w, 
 
 			EnableWindow(GetDlgItem(h, IDC_LOGFILE_LIMITS), xmpcfg.logfile_limit);
 
-			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "32 KB");
-			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "64 KB");
-			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "128 KB");
-			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "256 KB");
-			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "512 KB");
-			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "1 MB");
-			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "2 MB");
+			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "32 KiB");
+			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "64 KiB");
+			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "128 KiB");
+			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "256 KiB");
+			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "512 KiB");
+			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "1 MiB");
+			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "2 MiB");
 			
 			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_SETCURSEL, xmpcfg.logfile_limit_size, 0);
 
@@ -372,7 +438,39 @@ DWORD WINAPI DSP_GetConfig(void *inst, void *config)
 // set DSP config
 BOOL WINAPI DSP_SetConfig(void *inst, void *config, DWORD size)
 {
-	memcpy( &xmpcfg, config, sizeof(XMPScrobblerConfig) );
+	if (size == sizeof(XMPScrobblerConfig))
+		// Current config format, just copy it.
+		memcpy(&xmpcfg, config, size);
+	else if (size == sizeof(XMPScrobblerOldConfig)) {
+		XMP_Log("[INFO] Upgrading old config data format...\n");
+		// Old config format, generate the new one from it.
+		xmpcfg.on = ((XMPScrobblerOldConfig *) config)->on;
+		xmpcfg.delayFetch = true;
+		xmpcfg.everConfigured = true;
+		strcpy(xmpcfg.delayStr, DEFAULT_FETCH_DELAY_STR);
+		xmpcfg.fetchDelay = DEFAULT_FETCH_DELAY;
+		/* It's better to avoid crazy things... Memory padding may apply.
+		memcpy(&xmpcfg.username, &((XMPScrobblerOldConfig *) config)->username, sizeof(XMPScrobblerOldConfig) - sizeof(BOOL)); */
+		strcpy(xmpcfg.username, ((XMPScrobblerOldConfig *) config)->username);
+		strcpy(xmpcfg.password, ((XMPScrobblerOldConfig *) config)->password);
+		xmpcfg.proxy_enabled = ((XMPScrobblerOldConfig *) config)->proxy_enabled;
+		xmpcfg.proxy_auth_enabled = ((XMPScrobblerOldConfig *) config)->proxy_auth_enabled;
+		strcpy(xmpcfg.proxy_server, ((XMPScrobblerOldConfig *) config)->proxy_server);
+		strcpy(xmpcfg.proxy_port, ((XMPScrobblerOldConfig *) config)->proxy_port);
+		strcpy(xmpcfg.proxy_user, ((XMPScrobblerOldConfig *) config)->proxy_user);
+		strcpy(xmpcfg.proxy_password, ((XMPScrobblerOldConfig *) config)->proxy_password);
+		xmpcfg.logfile_limit = ((XMPScrobblerOldConfig *) config)->logfile_limit;
+		xmpcfg.logfile_limit_size = ((XMPScrobblerOldConfig *) config)->logfile_limit_size;
+		xmpcfg.logfile_truncate = ((XMPScrobblerOldConfig *) config)->logfile_truncate;
+		xmpcfg.logfile_debugmode = ((XMPScrobblerOldConfig *) config)->logfile_debugmode;
+   } else {
+      // Either very old config format or something is corrupted, fallback config.
+      XMP_Log("[WARNING] The plugin config data is corrupted, please reconfigure it.\n");
+      xmpcfg.on = false;
+      xmpcfg.logfile_limit = false;
+      xmpcfg.logfile_truncate = false;
+      xmpcfg.logfile_debugmode = false;
+   }
 
 	if(xmpcfg.logfile_truncate)
 		XMP_ClearLog();
@@ -386,7 +484,7 @@ void WINAPI DSP_Reset(void *inst)
 {
 	if( !xmpcfg.on ) return;
 
-	XMP_Log( "[DEBUG] DSP_Reset\n" );
+	XMP_Log( "[DEBUG] Reset request.\n" );
 
 	if(xmpFile.track_count <= 0)
 	{
@@ -395,21 +493,16 @@ void WINAPI DSP_Reset(void *inst)
 
 		XMP_KillInfoTimer();
 		XMP_SetDirty();
-	} else
-	{
-		if(XMP_IsCUE())
-		{
-			XMP_KillCUETimer();
-			XMP_KillSubmitTimer();
-
-			XMP_SetResetTimer();
-		} else
-		{
-			XMP_SetDirty();
-		}
+	} 
+	
+	if (XMP_IsCUE()) {
+		XMP_KillSubmitTimer();
+		XMP_KillCUETimer();
+		XMP_SetResetTimer();
+	} else {
+		XMP_SetDirty();
 	}
 
-	return;
 }
 
 DWORD WINAPI DSP_Process(void *inst, float *buffer, DWORD count)
@@ -426,8 +519,6 @@ DWORD WINAPI DSP_Process(void *inst, float *buffer, DW
 
 		if( sec <= 0 )
 		{
-			XMP_Log( "[DEBUG] DSP_Process -- sec <= 0\n" );
-
 			XMP_KillSubmitTimer();
 			XMP_SubmitProc();
 
@@ -471,10 +562,11 @@ DWORD WINAPI DSP_Process(void *inst, float *buffer, DW
 			XMP_KillResetTimer();
 			XMP_UpdateCurrentTrackIndex();
 
-			if(XMP_WasResetDirty())
+			// Allow 10 seconds for slower machines...
+			if (XMP_GetPlaybackTime() < xmpFile.tracks[xmpFile.current_track].start + 10)
+				XMP_SetSubmitTimer();
+			else
 				XMP_SetDirty();
-			
-			XMP_SetSubmitTimer();
 		}
 	}
 
@@ -500,12 +592,12 @@ void WINAPI DSP_SetFormat(void *inst, const XMPFORMAT 
 	if( !xmpcfg.on ) return;
 
 	if(form != NULL) {
-		XMP_Log( "[DEBUG] DSP_SetFormat( %d, %d, %d )\n", inst, form->rate, form->chan );
+		XMP_Log("[DEBUG] Audio info: %d channels, %d Hz.\n", form->rate, form->chan);
 
 		xmpRate = form->rate;
 		xmpChans = form->chan;
 	} else {
-		XMP_Log( "[DEBUG] DSP_SetFormat( %d, NULL )\n", inst );
+		XMP_Log("[DEBUG] Audio output stopped.\n");
 
 		xmpRate = 0;
 		xmpChans = 0;
@@ -513,15 +605,15 @@ void WINAPI DSP_SetFormat(void *inst, const XMPFORMAT 
 
 	if( xmpRate == 0 && xmpChans == 0 ) // track was stopped
 	{
-		XMP_Log( "[DEBUG] DSP_StopTrack\n" );
-
 		if(xmpCounter != -1)
-			XMP_Log( "[INFO] Track stopped\n" );
+			XMP_Log("[INFO] Playback stopped.\n");
 
 		XMP_KillSubmitTimer();
 		XMP_KillInfoTimer();
 		XMP_KillResetTimer();
 		XMP_KillCUETimer();
+		// If it was dirty it's not anymore:
+		xmpFile.dirty = false;
 	} else
 	{
 		if(infoCounter == -1)
@@ -538,7 +630,7 @@ void WINAPI DSP_NewTrack(void *inst, const char *file)
 
 	if( file != NULL )
 	{
-		XMP_Log( "[DEBUG] DSP_NewTrack (OPEN)\n" );
+		XMP_Log("[DEBUG] A new track has been opened.\n");
 
 		XMP_ScrobInit( xmpcfg.username, xmpcfg.password, pathCache );
 
@@ -549,17 +641,9 @@ void WINAPI DSP_NewTrack(void *inst, const char *file)
 		XMP_SetInfoTimer();
 	}
 	else
-	{
-		XMP_Log( "[DEBUG] DSP_NewTrack (CLOSE)\n" );
-	}
+		XMP_Log("[DEBUG] The track has been closed.\n");
 }
 
-// the exported function that XMPlay calls to get the plugin interface
-/*extern "C" __declspec( dllexport ) XMPDSP *WINAPI XMPDSP_GetInterface(DWORD face)
-{
-	return &dsp;
-}*/
-
 extern "C" __declspec( dllexport ) XMPDSP *WINAPI XMPDSP_GetInterface2(DWORD face, InterfaceProc faceproc)
 {
 	if (face!=XMPDSP_FACE) return NULL;
@@ -569,11 +653,9 @@ extern "C" __declspec( dllexport ) XMPDSP *WINAPI XMPD
 
 extern "C" __declspec( dllexport ) BOOL APIENTRY DllMain(HINSTANCE hDLL, DWORD reason, LPVOID reserved)
 {
-	switch (reason) {
-		case DLL_PROCESS_ATTACH:
-			ghInstance=hDLL;
-			DisableThreadLibraryCalls(ghInstance);
-			break;
+	if (reason == DLL_PROCESS_ATTACH) {
+		ghInstance=hDLL;
+		DisableThreadLibraryCalls(ghInstance);
 	}
 	return 1;
 }
@@ -582,19 +664,18 @@ extern "C" __declspec( dllexport ) BOOL APIENTRY DllMa
 // ---------------------------------------------------
 // ---------------------------------------------------
 
-void _vsscanf(char *input, char *format, ...)
+/*void _vsscanf(char *input, char *format, ...)
 {
   va_list argList;
 
   va_start(argList, format);
   vsscanf(input, format, argList);
-}
+}*/
 
 void XMP_Log( const char *s, ... )
-{
-	string tmp(s);
-	
-	if(!xmpcfg.logfile_debugmode && tmp.find("[DEBUG]") != string::npos)
+{	
+	//if(!xmpcfg.logfile_debugmode && tmp.find("[DEBUG]") != string::npos)
+	if (!xmpcfg.logfile_debugmode && (s[1] == 'D'))
 		return;
 	
 	// if the log file is too big (depends on the maximum size set in options),
@@ -603,14 +684,11 @@ void XMP_Log( const char *s, ... )
 	if(xmpcfg.logfile_limit && !XMP_ValidateLogSize())
 		XMP_ClearLog();
 
-	ofstream f( pathLog.c_str(), ios::app );
-	char buf[8000];
-	char buft[20];
+	ofstream logFile(pathLog.c_str(), ios::app);
+	char buf[8000] = {0};
+	char buft[20] = {0};
 	SYSTEMTIME time;
 
-	memset( buf, 0, 8000 );
-	memset( buft, 0, 20 );
-
 	va_list argList;
 	va_start( argList, s );
 	
@@ -621,18 +699,16 @@ void XMP_Log( const char *s, ... )
 
 	vsprintf( buf, s, argList );
 
-	f << buft << buf;
+	logFile << buft << buf;
 
- 	f.close();
+ 	logFile.close();
 }
 
 void XMP_ClearLog()
 {
-	ofstream f(pathLog.c_str(), ios::trunc | ios::out);
-
-	f << "";
-
- 	f.close();
+	ofstream logFile(pathLog.c_str(), ios::trunc | ios::out);
+	logFile << "";
+ 	logFile.close();
 }
 
 // checks if log size doesn't exceed maximum size set in options by the user
@@ -664,7 +740,7 @@ bool XMP_ValidateLogSize()
 
 void XMP_SubmitProc()
 {
-	XMP_Log( "[DEBUG] XMP_SubmitProc start\n" );
+	XMP_Log("[DEBUG] Scheduling a new submission.\n");
 	
 	if(XMP_IsDirty())
 		return;
@@ -691,7 +767,7 @@ void XMP_ScrobInit( const char *u, const char *p, stri
 {
 	if( !scrob )
 	{
-		XMP_Log("[DEBUG] XMP_ScrobInit() started\n");
+		XMP_Log("[DEBUG] Initializing the submission library...\n");
 
 		scrob = new Scrobbler( u, p, "xmp", "0.1" );
 
@@ -713,8 +789,6 @@ void XMP_ScrobInit( const char *u, const char *p, stri
 		}
 
     	scrob->init();
-    	
-    	XMP_Log("[DEBUG] XMP_ScrobInit() finished\n");
 	}
 }
 
@@ -746,17 +820,15 @@ int XMP_InTrack(int sec)
 
 void XMP_FetchInfo()
 {
-	XMP_Log( "[DEBUG] XMP_FetchInfo -- start\n" );
+	XMP_Log("[DEBUG] Fetching the track data.\n");
 
-	DWORD size = 0;
-
 	char *info1 = xmpfmisc->GetInfoText(XMPINFO_TEXT_MESSAGE);
 
 //	XMP_Log( "[AUX] info1: \n\n%s\n\n", info1 );
 
 	if(!info1)
 	{
-		XMP_Log( "[DEBUG] XMP_FetchInfo -- GetInfoText failed!\n" );
+		XMP_Log("[ERROR] The call to GetInfoText(XMPINFO_TEXT_MESSAGE) failed!\n");
 		return;
 	}
 
@@ -769,24 +841,18 @@ void XMP_FetchInfo()
 	{
 		// this file contains CUE sheet, we need to parse it
 
-		XMP_Log( "[DEBUG] XMP_FetchInfo() - CUE sheet parsing started\n" );
+		XMP_Log("[DEBUG] CUE sheet detected, parsing started.\n");
 
 		int cue_start = -1;
-		int cue_end;
-		int i, x, y;
+		int x, y;
+		unsigned int i = 0;
 		string temp;
 		
 		vector<int> beg; // this vector holds begin point of each line
 
-//		XMP_Log("block: %s\n", block.c_str());		
-
-		i = 0;
-		
-		while((cue_start = block.find("\n", cue_start + 1)) >= 0)
-			if(i++ > 0){
+		while((cue_start = block.find("\n", ++cue_start)) >= 0)
+			if(i++ > 0)
 				beg.push_back(cue_start + 1);
-			//	XMP_Log("c: %d\n",cue_start);
-			}
 
 		beg.pop_back();
 
@@ -836,7 +902,7 @@ void XMP_FetchInfo()
 			
 		XMP_ExtractTags_Other((char *) info1, tmp.artist, tmp.title, xmpFile.album);
 			
-		XMP_Log( "[INFO] CUE sheet found, current file contains %d track(s)\n", xmpFile.track_count );
+		XMP_Log("[INFO] CUE sheet found, the current file contains %d track(s)\n", xmpFile.track_count);
 	} else
 	{
 		// we are dealing with a regular file (without CUE sheet),
@@ -849,8 +915,13 @@ void XMP_FetchInfo()
 		cue_track.length = xmpFile.length;
 //		cue_track.end = xmpFile.length;
 
-		cue_track.tags_ok = XMP_ExtractTags_ID3v2((char *) info1,
+		// Look for the info in library first:
+		cue_track.tags_ok = XMP_ExtractTags_Library((char *) info1,
 			cue_track.artist, cue_track.title, xmpFile.album);
+			
+		if (!cue_track.tags_ok)
+			cue_track.tags_ok = XMP_ExtractTags_ID3v2((char *) info1,
+				cue_track.artist, cue_track.title, xmpFile.album);
 
 		if(!cue_track.tags_ok)
 			cue_track.tags_ok = XMP_ExtractTags_ID3v1((char *) info1,
@@ -892,14 +963,12 @@ void XMP_FetchInfo()
 
 	xmpfmisc->Free(info1);
 
-	XMP_Log( "[DEBUG] XMP_FetchInfo -- end\n" );
+	XMP_Log("[DEBUG] Track data fetch ended.\n");
 //	print_xmp_file();
 }
 
 void XMP_SetSubmitTimer()
 {
-//	XMP_Log( "[DEBUG] XMP_SetSubmitTimer()\n" );
-
 	XMP_UpdateCurrentTrackIndex();
 
 	if(xmpRate == 0 || xmpChans == 0 || xmpFile.current_track == -1) return;
@@ -908,7 +977,7 @@ void XMP_SetSubmitTimer()
 
 	if( len < 30 )
 	{
-		XMP_Log( "[INFO] Track is too short to be submitted (must be at least 30 seconds long)\n" );
+		XMP_Log("[INFO] The current track is too short to be submitted (must be at least 30 seconds long).\n");
 
 		if(XMP_IsCUE())
 			XMP_SetCUETimer();
@@ -922,80 +991,66 @@ void XMP_SetSubmitTimer()
 		return;
 
 	int diff = XMP_GetPlaybackTime() - xmpFile.tracks[xmpFile.current_track].start;
-	DWORD secLen = (len / 2 < 240 ? len / 2 : 240) - diff;
+	int secLen = (len / 2 < 240 ? len / 2 : 240) - diff;
 	
-	XMP_Log("[DEBUG] len = %d, diff = %d\n", (len / 2 < 240 ? len / 2 : 240), diff);
+	if (secLen <= 0) {
+		xmpFile.resumed = true;
+		XMP_SetDirty();
+		return;
+	}
+	
+	XMP_Log("[DEBUG] Track length: %d seconds, %d already passed.\n", len, diff);
 
 	xmpCounter = secLen * xmpRate * xmpChans;
 
-	bProcessed = false;
 	xmpFile.tracks[xmpFile.current_track].submitted = false;
 
 	XMP_Log( "[INFO] Current track: %s - %s (%s)\n", xmpFile.tracks[xmpFile.current_track].artist,
 		xmpFile.tracks[xmpFile.current_track].title, xmpFile.album );
 
-	XMP_Log( "[INFO] Submitting in %d seconds...\n", secLen );
+	XMP_Log("[DEBUG] Caching for submission in %d seconds.\n", secLen);
 }
 
 void XMP_KillSubmitTimer()
 {
-//	XMP_Log( "[DEBUG] XMP_KillSubmitTimer\n" );
-
 	xmpCounter = -1;
 }
 
 void XMP_SetInfoTimer()
 {
-//	XMP_Log( "[DEBUG] XMP_SetInfoTimer()\n" );
-
 	if( xmpRate == 0 || xmpChans == 0 ) return;
 
-	XMP_KillInfoTimer();
-
-	infoCounter = DELAY_BEFORE_FETCHINFO * xmpRate * xmpChans;
+	infoCounter = xmpcfg.fetchDelay * xmpRate * xmpChans;
 }
 
 void XMP_KillInfoTimer()
 {
-//	XMP_Log( "[DEBUG] XMP_KillInfoTimer\n" );
-
 	infoCounter = -1;
 }
 
 void XMP_SetResetTimer()
-{
-//	XMP_Log( "[DEBUG] XMP_SetResetTimer()\n" );
-	
+{	
 	if( xmpRate == 0 || xmpChans == 0 ) return;
 
-	XMP_KillResetTimer();
-
 	resetCounter = DELAY_BEFORE_RESET * xmpRate * xmpChans;
 }
 
 void XMP_KillResetTimer()
 {
-//	XMP_Log( "[DEBUG] XMP_KillResetTimer\n" );
-
 	resetCounter = -1;
 }
 
 void XMP_SetCUETimer()
 {
 	XMP_KillCUETimer();
-	
 	XMP_UpdateCurrentTrackIndex();
 
-	// we don't need to set this timer for the last track in the file
-	
 	if(xmpFile.current_track + 1 < xmpFile.track_count)
 	{
 //		int diff = XMP_GetPlaybackTime() - xmpFile.tracks[xmpFile.current_track].start;
-		int secLen = xmpFile.tracks[xmpFile.current_track].length / 2 + 5;
-
+		int secLen = xmpFile.tracks[xmpFile.current_track + 1].start - XMP_GetPlaybackTime() + 5;
 		cueCounter = secLen * xmpRate * xmpChans;
-
-		XMP_Log("[DEBUG] XMP_SetCUETimer(%d)\n", secLen);
+		XMP_Log("[DEBUG] Looking for the next track in the CUE sheet in %d seconds.\n", secLen);
 	}
 }
 
@@ -1024,15 +1079,28 @@ void XMP_SetDirty()
 {
 	if(XMP_IsDirty()) return;
 
-	xmpFile.dirty = true;
-
 	XMP_KillSubmitTimer();
 	XMP_KillCUETimer();
 
-	if(xmpFile.current_track == -1 ||
-		(xmpFile.current_track >= 0 && !xmpFile.tracks[xmpFile.current_track].submitted &&
-		xmpFile.tracks[xmpFile.current_track].tags_ok))
-		XMP_Log("[WARNING] Seeking during playback is not allowed - current file is not going to be submitted!\n");
+	// NOTE: The current track will not be submitted, even if the seeking was to the start of it.
+	
+	// Allow submitting the rest of the tracks in a CUE list:
+	if (XMP_IsCUE()) {
+		XMP_SetCUETimer();
+	} else {
+		xmpFile.dirty = true;
+	}
+
+	if (xmpFile.resumed) {
+		xmpFile.resumed = false;
+		XMP_Log("[INFO] The current playback was already submitted in the previous XMPlay execution.\n");
+		return;
+   }
+	
+	if (xmpFile.current_track == -1 || (xmpFile.current_track >= 0 && !xmpFile.tracks[xmpFile.current_track].submitted &&
+			xmpFile.tracks[xmpFile.current_track].tags_ok))
+	XMP_Log("[WARNING] Seeking during playback is not allowed, the current track is not going to be submitted.\n");
+	
 }
 
 bool XMP_IsDirty()
@@ -1040,11 +1108,6 @@ bool XMP_IsDirty()
 	return xmpFile.dirty;
 }
 
-bool XMP_WasResetDirty()
-{
-	return XMP_GetPlaybackTime() - xmpFile.tracks[xmpFile.current_track].start > 5;
-}
-
 void XMP_ClearXMPFile()
 {
 	if(xmpFile.tracks != NULL)
@@ -1056,6 +1119,7 @@ void XMP_ClearXMPFile()
 	memset(&xmpFile, 0, sizeof(xmpFile));
 
 	xmpFile.dirty = false;
+	xmpFile.resumed = false;
 	xmpFile.current_track = -1;
 }
 
@@ -1063,24 +1127,19 @@ void XMP_Welcome()
 {
 	DWORD dwVersion = GetVersion();
 
-	DWORD dwMajor = (DWORD)(LOBYTE(LOWORD(dwVersion)));
-	DWORD dwMinor = (DWORD)(HIBYTE(LOWORD(dwVersion)));
-	DWORD dwBuild = 0; // Windows Me/98/95
-
-//	if(dwVersion < 0x80000000)              
-		dwBuild = (DWORD)(HIWORD(dwVersion));
-
 	XMP_Log("----\n");
-	XMP_Log("[INFO] Hello, this is xmp-scrobbler %s\n", XMPSCROBBLER_VERSION);
-	XMP_Log("[DEBUG] GetVersion() = %d (%d.%d, build %d)\n", GetVersion(), dwMajor, dwMinor, dwBuild);
-	XMP_Log("[DEBUG] curl_version() = %s\n", curl_version());
+	XMP_Log("[INFO] Hello, this is xmp-scrobbler %s.\n", XMPSCROBBLER_VERSION);
+	XMP_Log("[DEBUG] System data and plugin configuration:\n\n · Operating System: %d.%d, build %d.\n · Internal libraries: %s\n"
+		" · Tag fetching %s %hu seconds.\n · %s%s\n\n", LOBYTE(LOWORD(dwVersion)), HIBYTE(LOWORD(dwVersion)), HIWORD(dwVersion),
+		curl_version(), xmpcfg.delayFetch? "delayed" : "not delayed,", xmpcfg.fetchDelay, xmpcfg.proxy_enabled? "Operating through "
+		"proxy" : "Not operating through proxy", (xmpcfg.proxy_enabled && xmpcfg.proxy_auth_enabled)? " with authentication." : ".");
 }
 
 void XMP_Setup()
 {
-	char path[255];
+	char path[MAX_PATH];
 
-	GetModuleFileName( NULL, path, 255 );
+	GetModuleFileName(NULL, path, MAX_PATH);
 
 	string spath( path );
 
@@ -1090,11 +1149,11 @@ void XMP_Setup()
 	pathCache = spath + "xmp-scrobbler.cache";
 }
 
-void print_xmp_file()
+/*void print_xmp_file()
 {
 //	XMP_Log( "XMPFile\n\ntrack_count: %d\n\nlength: %d\n\n", xmpFile.track_count, xmpFile.last_track, xmpFile.length );
 	for(int i=0; i<xmpFile.track_count; i++)
 	{
 		XMP_Log( "[XMPTrack]\n\nstart: %d\nlength: %d\nartist: %s\ntitle: %s\nalbum: %s\n", xmpFile.tracks[i].start, xmpFile.tracks[i].length, xmpFile.tracks[i].artist, xmpFile.tracks[i].title, xmpFile.album );
 	}
-}
+}*/
