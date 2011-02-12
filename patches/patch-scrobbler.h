--- orig/libscrobbler/scrobbler.h	Fri Feb  9 18:21:02 2007
+++ libscrobbler/scrobbler.h		Sat Feb 12 01:20:12 2011
@@ -27,11 +27,13 @@
 #if defined(LINUX)
 #	include <pthread.h>
 #elif defined(WIN32)
+#define WIN32_LEAN_AND_MEAN
 #	include <windows.h>
 #	include <winbase.h>
 #	include <winuser.h>
 #endif
 
+#include <cstdlib>
 #include <string>
 #include <time.h>
 #include <curl/curl.h>
@@ -62,41 +64,7 @@ using namespace std;
 #define HS_FAIL_WAIT 100000
 
 /**
-	An enumeration of the status messages so that clients can
-	do things with them, and not have to parse the text
-	to work out what's going on.
-	If you don't care about these, then you can continue to
-	just override the char* only version of statusUpdate
-*/
-enum ScrobbleStatus
-{
-	S_SUBMITTING = 0,
-
-	S_NOT_SUBMITTING,
-
-   S_CONNECT_ERROR,
-	
-	S_HANDSHAKE_SUCCESS,
-	S_HANDSHAKE_UP_TO_DATE,
-	S_HANDSHAKE_OLD_CLIENT,
-	S_HANDSHAKE_INVALID_RESPONSE,
-	S_HANDSHAKE_BAD_USERNAME,
-	S_HANDSHAKE_ERROR,
-	S_HANDHAKE_NOTREADY,
-
-	S_SUBMIT_SUCCESS,
-	S_SUBMIT_INVALID_RESPONSE,
-	S_SUBMIT_INTERVAL,
-	S_SUBMIT_BAD_PASSWORD,
-	S_SUBMIT_FAILED,
-	S_SUBMIT_BADAUTH,
-
-   S_DEBUG
-
-};
-
-/**
-	Audioscrobbler client class. Needs libCurl to run.
+	Audioscrobbler client class. Needs libcurl to run.
 	$Id: scrobbler.h,v 1.14 2004/11/11 14:18:29 xurble Exp $
 
 	@version	1.1
@@ -129,16 +97,12 @@ class Scrobbler { (public)
 		sent immediately unless the server has told it to cache, in which case it
 		will get sent with the first song submission after the cache period has 
 		expired. 
-		
-		@note Submission is not synchronous, so song submission status is only available through the statusUpdate callback.
-		@note There's no way to tell the class to submit explicitly, it'll try to submit when you addSong.
 
 		@param artist	The artist name.
 		@param title	The title of the song.
 		@param length	The length of the song in seconds.
 		@param ltime	The time (unix time) in seconds when playback of the song started.
 		@param trackid	The MusicBrainz TrackID of the song (in guid format).
-		@see			statusUpdate()
 		@retval 0		Failure. This could be because the song submitted was too short.
 		@retval	1		This tripped song submission, Scrobbler is now connecting.
 		@retval 2		The submission was cached.
@@ -161,7 +125,6 @@ class Scrobbler { (public)
 	// should call juts before deletion
 	void term();
 
-
 	/**
 		Set the user's user name if it's changed since the constructor.
 
@@ -211,60 +174,6 @@ class Scrobbler { (public)
 	
 	string prepareSubmitString();
 
-	/**
-		Override this to receive updates on the status of the scrobbler 
-		client to display to the user. You don't have to override this,
-		but it helps.
-	
-		If you do ovveride it, the old text only version won't
-		work any more.
-
-		@param status	The status code.
-		@param text		The text of the status update.
-	*/
-	virtual void statusUpdate(ScrobbleStatus status,const char *text) 
-	{
-      //if(status != S_DEBUG) // don't pass out pointless debug messages to foobar
-		   statusUpdate(text);
-	};
-
-	/**
-		old status update - text only
-		
-		Override this to receive updates on the status of the scrobbler 
-		client to display to the user. You don't have to override this,
-		but it helps.
-
-		@param text		The text of the status update.
-	*/
-	virtual void statusUpdate(const char *text)
-	{
-		XMP_Log( "[DEBUG] %s\n", text );
-	};
-
-	virtual void setInterval(int in);
-
-	/**
-		Override this to save the cache - called from the destructor.
-
-		@param cache	The contents of the cache to save.
-		@param numentries	The number of entries in the cache - save this too.
-		@see			loadCache()
-		@retval	1		You saved some cache.
-		@retval	0		You didn't save cache, or can't.
-	*/
-	virtual int saveCache(const char *cache, const int numentries);
-
-	/**
-		This is called when you should load the cache. Use setCache.
-
-		@see			saveCache()
-		@see			setCache()
-		@retval 1		You loaded some cache.
-		@retval 0		There was no cache to load, or you can't.
-	*/
-	virtual int loadCache();
-
 	virtual void genSessionKey();
 
 	void doSubmit();
@@ -302,12 +211,10 @@ class Scrobbler { (public)
 			//	and clean up our new buffer
 			delete[] buffer;
 		}
-		catch(...){ static_cast<Scrobbler *>(userp)->statusUpdate(S_DEBUG,"EXCEPTION IN HANDSHAKE DATA");  }
+		catch(...){XMP_Log("[ERROR] EXCEPTION IN HANDSHAKE DATA\n");}
 		return nmemb; 
    }
-
    
-   
    static size_t submit_write_data(void *data, size_t size, size_t nmemb, void *userp) 
    { 
 		try
@@ -332,18 +239,18 @@ class Scrobbler { (public)
 			//	and clean up our new buffer
 			delete[] buffer;
 		}
-		catch(...){ static_cast<Scrobbler *>(userp)->statusUpdate(S_DEBUG,"EXCEPTION IN SUBMIT DATA"); }
+		catch(...){XMP_Log("[ERROR] EXCEPTION IN SUBMIT DATA\n");}
 		return nmemb; 
    }
    
-  static int curl_debug_callback (CURL * c, curl_infotype t, char * data, size_t size, void * a)
+  /*static int curl_debug_callback (CURL * c, curl_infotype t, char * data, size_t size, void * a)
   {
 		std::string tmp(data);
 		
 		tmp += "\n";
 		::XMP_Log("%s",tmp.c_str());
 		return 0;
-	}
+	}*/
 
 	//	this function called back by libcurl
 	static size_t curl_header_write(void *data, size_t size, size_t nmemb, void *userp) 
@@ -370,7 +277,7 @@ class Scrobbler { (public)
 			//	and clean up our new buffer
 			delete[] buffer;
 		}
-		catch(...){ static_cast<Scrobbler *>(userp)->statusUpdate(S_DEBUG,"EXCEPTION IN WRITE HEADER"); }
+		catch(...){XMP_Log("[ERROR] EXCEPTION IN WRITE HEADER\n");}
 		return nmemb; 
 	}
 	
@@ -394,7 +301,6 @@ class Scrobbler { (public)
 	
 	bool proxyauth;
 	bool readytosubmit;
-
 	bool submitinprogress;
 
 	bool closethread;
