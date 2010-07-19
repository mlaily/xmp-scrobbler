--- xmpfunc.h		Mon Apr  2 14:57:32 2007
+++ orig/xmpfunc.h	Wed Jun 23 11:05:24 2010
@@ -26,6 +26,8 @@ typedef void *(WINAPI *InterfaceProc)(DWORD face); // 
 #define XMPCONFIG_NET_RECONNECT	2
 #define XMPCONFIG_NET_PROXY		3
 #define XMPCONFIG_NET_PROXYCONF	4
+#define XMPCONFIG_NET_TIMEOUT	5
+#define XMPCONFIG_NET_PREBUF	6
 
 #define XMPINFO_TEXT_GENERAL	0 // General info text
 #define XMPINFO_TEXT_MESSAGE	1 // Message info text
@@ -55,12 +57,18 @@ typedef struct {
 	DWORD res;		// bytes per sample (1=8-bit,2=16-bit,3=24-bit,4=float,0=undefined)
 } XMPFORMAT;
 
+typedef struct {
+	float time;		// cue position
+	const char *title;
+	const char *performer;
+} XMPCUE;
+
 typedef struct { // miscellaneous functions
 	DWORD (WINAPI *GetVersion)(); // get XMPlay version (eg. 0x03040001 = 3.4.0.1)
 	HWND (WINAPI *GetWindow)(); // get XMPlay window handle
 	void *(WINAPI *Alloc)(DWORD len); // allocate memory
 	void *(WINAPI *ReAlloc)(void *mem, DWORD len); // re-allocate memory
-	void (WINAPI *Free)(void *mem); // free allocated memory
+	void (WINAPI *Free)(void *mem); // free allocated memory/text
 	BOOL (WINAPI *CheckCancel)(); // user wants to cancel?
 	DWORD (WINAPI *GetConfig)(DWORD option); // get a config (XMPCONFIG_xxx) value
 	const char *(WINAPI *GetSkinConfig)(const char *name); // get a skinconfig value
@@ -68,9 +76,12 @@ typedef struct { // miscellaneous functions
 	void (WINAPI *RefreshInfo)(DWORD mode); // refresh info displays (XMPINFO_REFRESH_xxx flags)
 	char *(WINAPI *GetInfoText)(DWORD mode); // get info window text (XMPINFO_TEXT_xxx)
 	char *(WINAPI *FormatInfoText)(char *buf, const char *name, const char *value); // format text for info window (tabs & new-lines)
-	char *(WINAPI *GetTag)(int tag); // get a current track's tag (-1=formatted title, -2=filename)
+	char *(WINAPI *GetTag)(int tag); //tags: 0=title,1=artist,2=album,3=year,4=track,5=genre,6=comment,7=filetype, -1=formatted title,
+			// -2=filename,-3=track/cue title
 	BOOL (WINAPI *RegisterShortcut)(const XMPSHORTCUT *cut); // add a shortcut
 	BOOL (WINAPI *PerformShortcut)(DWORD id); // perform a shortcut action
+// version 3.4.0.14
+	const XMPCUE *(WINAPI *GetCue)(DWORD cue); // get a cue entry (0=image, 1=1st track)
 } XMPFUNC_MISC;
 
 typedef struct { // "registry" functions
