--- xmpdsp.h		Sat Mar 24 15:28:38 2007
+++ orig/xmpdsp.h	Wed Jun 23 14:56:22 2010
@@ -1,4 +1,4 @@
-// XMPlay DSP plugin header (c) 2004-2007 Ian Luck
+// XMPlay DSP/general plugin header (c) 2004-2009 Ian Luck
 // new plugins can be submitted to plugins@xmplay.com
 
 #pragma once
@@ -15,6 +15,7 @@ extern "C" {
 
 #define XMPDSP_FLAG_MULTI		1 // supports multiple instances
 #define XMPDSP_FLAG_TAIL		2 // effect has a tail
+#define XMPDSP_FLAG_NODSP		4 // no DSP processing (a "general" plugin), exclude from "DSP" saved settings
 
 typedef struct {
 	DWORD flags; // XMPDSP_FLAG_xxx
@@ -31,8 +32,9 @@ typedef struct {
 	DWORD (WINAPI *GetConfig)(void *inst, void *config); // get config (return size of config data)
 	BOOL (WINAPI *SetConfig)(void *inst, void *config, DWORD size); // apply config
 
-	void (WINAPI *NewTrack)(void *inst, const char *file); // new track has been opened (OPTIONAL)
-	void (WINAPI *SetFormat)(void *inst, const XMPFORMAT *form); // set sample format
+	void (WINAPI *NewTrack)(void *inst, const char *file); // a track has been opened or closed (OPTIONAL)
+// the following are optional with the XMPDSP_FLAG_NODSP flag
+	void (WINAPI *SetFormat)(void *inst, const XMPFORMAT *form); // set sample format (if form=NULL output stopped)
 	void (WINAPI *Reset)(void *inst); // reset DSP after seeking
 	DWORD (WINAPI *Process)(void *inst, float *data, DWORD count); // process samples (return number of samples processed)
 // The Process function currently must return the same amount of data as it is given - it can't
