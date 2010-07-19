--- xmp-scrobbler.cpp		Wed Jun 23 23:49:31 2010
+++ orig/xmp-scrobbler.cpp	Wed Jun 23 23:51:55 2010
@@ -15,10 +15,11 @@
 
 #include "libscrobbler\scrobbler.h"
 
-#define XMPSCROBBLER_VERSION		"0.8"
+#define XMPSCROBBLER_VERSION		"0.9.6"
 #define MAX_LENGTH					240		// in seconds
-#define DELAY_BEFORE_FETCHINFO		10		// in seconds
-#define DELAY_BEFORE_RESET			5		// in seconds
+#define DELAY_BEFORE_RESET       	5        // in seconds
+#define DEFAULT_FETCH_DELAY      	10       // in seconds
+#define DEFAULT_FETCH_DELAY_STR  	"10"     // in seconds
 
 HINSTANCE ghInstance;
 DWORD instID = 0;
@@ -46,7 +47,10 @@ bool isWinNT = false;   // TRUE if running on WinNT wi
 bool bProcessed = false;
 
 typedef struct {
-	BOOL on;
+	BOOL on, delayFetch, everConfigured;
+	
+	char delayStr[4];
+	unsigned short int fetchDelay;
 
 	char username[255];
 	char password[255];
@@ -67,6 +71,28 @@ typedef struct {
 
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
@@ -93,7 +119,7 @@ typedef struct {
 	int current_track;  // Last track played
 	int length;      // Total length (do we need it?)
 	
-	bool dirty;
+	bool dirty, resumed;
 
 	char album[TAG_FIELD_SIZE]; // Album name
 	char mb[TAG_FIELD_SIZE];    // MusicBrainz ID
@@ -136,7 +162,6 @@ void XMP_UpdateCurrentTrackIndex();
 
 void XMP_SetDirty();
 bool XMP_IsDirty();
-bool XMP_WasResetDirty();
 
 void XMP_ClearXMPFile();
 void XMP_Welcome();
@@ -225,6 +250,20 @@ BOOL CALLBACK DSPDialogProc(HWND h, UINT m, WPARAM w, 
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
 
 					MESS(IDC_USERNAME, WM_GETTEXT, 255, xmpcfg.username);
 					MESS(IDC_PASSWORD, WM_GETTEXT, 255, xmpcfg.password);
@@ -244,6 +283,7 @@ BOOL CALLBACK DSPDialogProc(HWND h, UINT m, WPARAM w, 
 
 					xmpcfg.logfile_limit_size = MESS(IDC_LOGFILE_LIMITS, CB_GETCURSEL, 0, 0);
 
+					xmpcfg.everConfigured = true;
 					EndDialog( h, 1 );
 
 					break;
@@ -255,12 +295,18 @@ BOOL CALLBACK DSPDialogProc(HWND h, UINT m, WPARAM w, 
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
@@ -318,6 +364,16 @@ BOOL CALLBACK DSPDialogProc(HWND h, UINT m, WPARAM w, 
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
@@ -372,7 +428,39 @@ DWORD WINAPI DSP_GetConfig(void *inst, void *config)
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
+      XMP_Log("[WARNING] The plugin config data is corrupted, reconfiguration needed.\n");
+      xmpcfg.on = false;
+      xmpcfg.logfile_limit = false;
+      xmpcfg.logfile_truncate = false;
+      xmpcfg.logfile_debugmode = false;
+   }
 
 	if(xmpcfg.logfile_truncate)
 		XMP_ClearLog();
@@ -395,21 +483,16 @@ void WINAPI DSP_Reset(void *inst)
 
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
@@ -471,10 +554,11 @@ DWORD WINAPI DSP_Process(void *inst, float *buffer, DW
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
 
@@ -522,6 +606,8 @@ void WINAPI DSP_SetFormat(void *inst, const XMPFORMAT 
 		XMP_KillInfoTimer();
 		XMP_KillResetTimer();
 		XMP_KillCUETimer();
+		// If it was dirty it's not anymore:
+		xmpFile.dirty = false;
 	} else
 	{
 		if(infoCounter == -1)
@@ -849,8 +935,13 @@ void XMP_FetchInfo()
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
@@ -922,8 +1013,14 @@ void XMP_SetSubmitTimer()
 		return;
 
 	int diff = XMP_GetPlaybackTime() - xmpFile.tracks[xmpFile.current_track].start;
-	DWORD secLen = (len / 2 < 240 ? len / 2 : 240) - diff;
+	int secLen = (len / 2 < 240 ? len / 2 : 240) - diff;
 	
+	if (secLen <= 0) {
+		xmpFile.resumed = true;
+		XMP_SetDirty();
+		return;
+	}
+	
 	XMP_Log("[DEBUG] len = %d, diff = %d\n", (len / 2 < 240 ? len / 2 : 240), diff);
 
 	xmpCounter = secLen * xmpRate * xmpChans;
@@ -950,9 +1047,7 @@ void XMP_SetInfoTimer()
 
 	if( xmpRate == 0 || xmpChans == 0 ) return;
 
-	XMP_KillInfoTimer();
-
-	infoCounter = DELAY_BEFORE_FETCHINFO * xmpRate * xmpChans;
+	infoCounter = xmpcfg.fetchDelay * xmpRate * xmpChans;
 }
 
 void XMP_KillInfoTimer()
@@ -968,8 +1063,6 @@ void XMP_SetResetTimer()
 	
 	if( xmpRate == 0 || xmpChans == 0 ) return;
 
-	XMP_KillResetTimer();
-
 	resetCounter = DELAY_BEFORE_RESET * xmpRate * xmpChans;
 }
 
@@ -991,7 +1084,7 @@ void XMP_SetCUETimer()
 	if(xmpFile.current_track + 1 < xmpFile.track_count)
 	{
 //		int diff = XMP_GetPlaybackTime() - xmpFile.tracks[xmpFile.current_track].start;
-		int secLen = xmpFile.tracks[xmpFile.current_track].length / 2 + 5;
+		int secLen = xmpFile.tracks[xmpFile.current_track + 1].start - XMP_GetPlaybackTime() + 5;
 
 		cueCounter = secLen * xmpRate * xmpChans;
 
@@ -1024,15 +1117,28 @@ void XMP_SetDirty()
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
+		XMP_Log("[INFO] The current track should already have been submitted, resuming playback.\n");
+		return;
+   }
+	
+	if (xmpFile.current_track == -1 || (xmpFile.current_track >= 0 && !xmpFile.tracks[xmpFile.current_track].submitted &&
+			xmpFile.tracks[xmpFile.current_track].tags_ok))
+	XMP_Log("[WARNING] Seeking during playback is not allowed, the current track is not going to be submitted.\n");
+	
 }
 
 bool XMP_IsDirty()
@@ -1040,11 +1146,6 @@ bool XMP_IsDirty()
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
@@ -1056,6 +1157,7 @@ void XMP_ClearXMPFile()
 	memset(&xmpFile, 0, sizeof(xmpFile));
 
 	xmpFile.dirty = false;
+	xmpFile.resumed = false;
 	xmpFile.current_track = -1;
 }
 
