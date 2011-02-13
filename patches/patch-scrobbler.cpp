--- orig/libscrobbler/scrobbler.cpp	Fri Mar 23 14:59:18 2007
+++ libscrobbler/scrobbler.cpp		Fri Feb 11 23:00:03 2011
@@ -87,17 +87,18 @@ void Scrobbler::init()
 	cm = new CacheManager( cachefile );
 
 	clearCache();
-	loadCache();
+	// Load the cache
+	cm->Load();
 	
-	XMP_Log("[DEBUG] Scrobbler::init() - cache loaded, doing handshake...\n");
+	XMP_Log("[DEBUG] Cache loaded, scheduling handshake.\n");
 	
 	doHandshake();
 }
 
 void Scrobbler::term()
 {
-	saveCache(poststring.c_str(), songnum);
-	
+	// The cache file should be up to date, no need to save it when exiting.
+	// cm->Save();
 	delete cm;
 }
 
@@ -128,6 +129,11 @@ void Scrobbler::setUsername(const char *user)
 {
 	if (!strlen(user))
 		return;
+		
+	if (readytosubmit && !clientid.empty()) {
+		XMP_Log("[WARNING] Please restart XMPlay for user change to take effect.\n");
+		return;
+	}
 
    char* u = curl_escape(user,strlen(user));
 	username = u;
@@ -160,12 +166,7 @@ void Scrobbler::setProxy(const char* proxy, const char
 	{
 		proxyserver = proxy;
 
-		std::string temp = "Using proxy server: " + proxyserver;
-		
-		if( proxyauth )
-			temp += " (with authentication - user and password are set in options)";
-		
-		statusUpdate(S_DEBUG,temp.c_str());
+		XMP_Log("[DEBUG] Operating through proxy%s: %s\n", proxyauth ? " with authentication" : "", proxyserver.c_str());
 	}
 
 	if( !readytosubmit )
@@ -191,7 +192,7 @@ string Scrobbler::prepareSubmitString()
 	current_sp = cm->GetSubmitPackage();
 	string submitStr;
 
-	XMP_Log( "[INFO] Preparing to submit %d track(s) from the cache\n", current_sp.size );
+	XMP_Log("[INFO] Preparing to submit %d track%s from cache.\n", current_sp.size, !(current_sp.size - 1) ? "" : "s");
 
 	for( int count = 0; count < current_sp.size; count++ )
 	{
@@ -209,7 +210,7 @@ string Scrobbler::prepareSubmitString()
 		char *m = curl_escape(track.mb, (int)strlen(track.mb));
 		char *buf = (char *)malloc(48 + strlen(a) + strlen(b) + strlen(t) + strlen(i) + strlen(m));
 
-		sprintf(buf, "&a[%i]=%s&b[%i]=%s&t[%i]=%s&i[%i]=%s&l[%i]=%i&m[%i]=%s", count, a, count, b, count, t, count, i, count, track.length, count, track.mb);
+		sprintf(buf, "&a[%d]=%s&b[%d]=%s&t[%d]=%s&i[%d]=%s&l[%d]=%ld&m[%d]=%s", count, a, count, b, count, t, count, i, count, track.length, count, track.mb);
 
 		submitStr += buf;
 		
@@ -230,15 +231,16 @@ string Scrobbler::prepareSubmitString()
 int Scrobbler::addSong( TRACKDATA track )
 {
 	cm->AddTrack( track );
+	cm->Save();
 
 /*	if (track.length <= MINLENGTH || track.length > MAXLENGTH) // made <= to minlength to stop iTMS previews being submitted in iTunes
 		return 0;
 */
-	XMP_Log("[INFO] Track added to the cache for submission\n");
+	XMP_Log("[INFO] Track added to cache for submission.\n");
 
 	if(submitinprogress)
 	{
-		statusUpdate(S_NOT_SUBMITTING,"Previous submission still in progress");
+		XMP_Log("[DEBUG] Previous submission still in progress.\n");
 		return 0;
 	}
 	
@@ -264,9 +266,9 @@ int Scrobbler::addSong( TRACKDATA track )
 		doSubmit();
 		return 1;
 	} else {
-		char msg[128];
-		snprintf(msg, sizeof(msg), "Not submitting, caching for %i more second(s). Cache is %i entries.", (int)(interval + lastconnect - now), songnum);
-		statusUpdate(S_NOT_SUBMITTING,msg);
+		int cache_time = interval + lastconnect - now;
+		XMP_Log("[DEBUG] Not submitting, caching for %d more second%s. Cache has %d %s.\n", cache_time,
+			!(cache_time - 1) ? "" : "s", songnum, !(songnum - 1)? "entry" : "entries");
 		return 2;
 	}
 }
@@ -292,7 +294,7 @@ void Scrobbler::doSubmit()
 {
 	if(submitinprogress)
 	{
-		statusUpdate(S_NOT_SUBMITTING,"Previous submission still in progress");
+		XMP_Log("[DEBUG] Previous submission still in progress.\n");
 		return;
 	}
 	submitinprogress = true;
@@ -300,7 +302,7 @@ void Scrobbler::doSubmit()
 	if (username == "" || password == "" || !readytosubmit)
 		return;
 
-	statusUpdate(S_SUBMITTING,"Submitting...");
+	//XMP_Log("[DEBUG] Submitting.\n");
 	time_t now;
 	time (&now);
 	lastconnect = now;
@@ -325,28 +327,26 @@ void Scrobbler::handleHandshake(char *handshake)
    if(lastRetCode != 200)
    {
       // no point in doing this unless we get an OK
-      //statusUpdate(S_DEBUG,"NOT 200");
+      //XMP_Log("[DEBUG] NOT 200.\n");
       return;
    }
 
 	char seps[] = " \n\r";
 	char *response = strtok(handshake, seps);
 	if (!response) {
-		statusUpdate(S_HANDSHAKE_INVALID_RESPONSE,"Handshake failed: Response invalid");
+		XMP_Log("[DEBUG] Handshake failed, the server response is invalid.\n");
 		return;
 	}
 	do {
 		if (stricmp("UPTODATE", response) == 0) {
-			statusUpdate(S_HANDSHAKE_UP_TO_DATE,"Handshaking: Client up to date.");
+			XMP_Log("[DEBUG] Handshake response: client up to date.\n");
 		} else if (stricmp("UPDATE", response) == 0) {
 			char *updateurl = strtok(NULL, seps);
 			if (!updateurl)
 				break;
-			string msg = "Handshaking: Please update your client at: ";
-			msg += updateurl;
-			statusUpdate(S_HANDSHAKE_OLD_CLIENT,msg.c_str());
+			XMP_Log("[WARNING] Please update the plugin at: %s\n", updateurl);
 		} else if (stricmp("BADUSER", response) == 0) {
-			statusUpdate(S_HANDSHAKE_BAD_USERNAME,"Handshake failed: Bad username");
+			XMP_Log("[WARNING] At least your username is incorrect, please correct it in the plugin config.\n");
 			return;
 		} else {
 			break;
@@ -354,22 +354,20 @@ void Scrobbler::handleHandshake(char *handshake)
 		challenge = strtok(NULL, seps);
 		submiturl = strtok(NULL, seps);
 		
-		statusUpdate(S_DEBUG, submiturl.c_str());
+		XMP_Log("[DEBUG] Submission URL: %s\n", submiturl.c_str());
 		
 		char *inttext = strtok(NULL, seps);
 		if (!inttext || !(stricmp("INTERVAL", inttext) == 0))
 			break;
-		setInterval(atoi(strtok(NULL, seps)));
+		interval = atoi(strtok(NULL, seps));
+		XMP_Log("[DEBUG] Submission interval set to %d second%s.\n", interval, !(interval - 1) ? "" : "s");
 		genSessionKey();
 		readytosubmit = true;
-		statusUpdate(S_HANDSHAKE_SUCCESS,"Handshake successful.");
-		XMP_Log("[INFO] Handshake with Last.fm server successful!\n");
+		XMP_Log("[INFO] Successfully connected to the Last.fm server.\n");
 		return;
 	} while (0);
-	char buf[256];
-	snprintf(buf, sizeof(buf), "Handshake failed: %s", strtok(NULL, "\n"));
-	statusUpdate(S_HANDSHAKE_ERROR,buf);
-	XMP_Log("[WARNING] Handshake with Last.fm server failed!\n");
+	XMP_Log("[DEBUG] Handshake failed: %s.\n", strtok(NULL, "\n"));
+	XMP_Log("[WARNING] The attempt to connect to the Last.fm server failed!\n");
 }
 
 // -----------------------------------------------------------
@@ -388,55 +386,40 @@ void Scrobbler::handleSubmit(char *data)
 	char seps[] = " \n\r";
 	char * response = strtok(data, seps);
 	if (!response) {
-		statusUpdate(S_SUBMIT_INVALID_RESPONSE,"Submission failed: Response invalid");
+		XMP_Log("[DEBUG] Submission failed, the server response is invalid.\n");
 		return;
 	}
 	if (stricmp("OK", response) == 0) {
-		statusUpdate(S_SUBMIT_SUCCESS,"Submission succeeded.");
-		XMP_Log("[INFO] Submission succeeded!\n");
+		XMP_Log("[INFO] Submission succeeded.\n");
 
 		clearCache();
 		cm->DeleteTracks( current_sp.size );
+		cm->Save();
 		char *inttext = strtok(NULL, seps);
 		if (inttext && (stricmp("INTERVAL", inttext) == 0)) {
 			interval = atoi(strtok(NULL, seps));
-			char buf[128];
-			snprintf(buf, sizeof(buf), "Submit interval set to %i second(s).", interval);
-			statusUpdate(S_SUBMIT_INTERVAL,buf);
+			XMP_Log("[DEBUG] Submission interval set to %d second%s.\n", interval, !(interval - 1) ? "" : "s");
 		}
-	} else if (stricmp("BADPASS", response) == 0) {
-		statusUpdate(S_SUBMIT_BAD_PASSWORD,"Submission failed: bad password.");
-	} else if (stricmp("FAILED", response) == 0) {
-		char buf[256];
-		snprintf(buf, sizeof(buf), "Submission failed: %s", strtok(NULL, "\n"));
-		statusUpdate(S_SUBMIT_FAILED, buf);
+	} else if (stricmp("BADAUTH", response) == 0) {
+		XMP_Log("[DEBUG] Submission failed, the server reported bad authorization.\n");
+		XMP_Log("[WARNING] Your login info is incorrect, please correct it in the plugin config.\n");
 		char *inttext = strtok(NULL, seps);
 		if (inttext && (stricmp("INTERVAL", inttext) == 0)) {
 			interval = atoi(strtok(NULL, seps));
-			snprintf(buf, sizeof(buf), "Submit interval set to %i second(s).", interval);
-			statusUpdate(S_SUBMIT_INTERVAL,buf);
+			XMP_Log("[DEBUG] Submission interval set to %d second%s.\n", interval, !(interval - 1) ? "" : "s");
 		}
-	} else if (stricmp("BADAUTH",response) == 0) {
-		statusUpdate(S_SUBMIT_BADAUTH,"Submission failed: bad authorization.");
 	} else {
-		char buf[256];
-		snprintf(buf, sizeof(buf), "Submission failed: %s", strtok(NULL, "\n"));
-		statusUpdate(S_SUBMIT_FAILED,buf);
+		/* FAILED and BADUSER require interval too */
+		XMP_Log("[DEBUG] Submission failed: %s.\n", strtok(NULL, "\n"));
+		char *inttext = strtok(NULL, seps);
+		if (inttext && (stricmp("INTERVAL", inttext) == 0)) {
+			interval = atoi(strtok(NULL, seps));
+			XMP_Log("[DEBUG] Submission interval set to %d second%s.\n", interval, !(interval - 1) ? "" : "s");
+		}		
 	}
 }
 
 // -----------------------------------------------------------
-void Scrobbler::setInterval(int in) {
-	interval = in;
-	string ret;
-	ret = "Submit interval set to ";
-	char i[10];
-	itoa(in, i, 10);
-	ret += i;
-	ret += " second(s).";
-	statusUpdate(S_SUBMIT_INTERVAL, ret.c_str());
-}
-
 void Scrobbler::genSessionKey() {
 	string clear = password + challenge;
     md5_state_t md5state;
@@ -461,33 +444,29 @@ void Scrobbler::handleHeaders(char* header)
 
 	std::string code;
    std::string headerStr = header;
-//   statusUpdate(S_DEBUG,header);
+//   XMP_Log("[DEBUG] %s.\n", header);
    
    if(headerStr.substr(0,4) == "HTTP") // this is the response code
 	{
-      //statusUpdate(S_DEBUG,header);
+      //XMP_Log("[DEBUG] %s.\n", header);
 		size_t pos = headerStr.find(" ");
 		
 		if(pos != headerStr.npos)
 		{
 			code = headerStr.substr(pos+1);
 			pos = code.find(" ");
+
 			if(pos != code.npos)
-			{
 				code = code.substr(0,pos);
-			}
+
 			lastRetCode = atoi(code.c_str());
 /*         if(lastRetCode == 100) // 100-continue
          {
            doSubmit();
          } else*/ if(lastRetCode != 200)
-         {
-            statusUpdate(S_CONNECT_ERROR,"The server reported a processing error.");
-            statusUpdate(S_DEBUG,header);
-         }
+			XMP_Log("[DEBUG] The server reported a processing error:\n\n%s\n\n", header);
 		}
 	}
-
 }
 
 // -----------------------------------------------------------
@@ -502,8 +481,6 @@ void Scrobbler::workerThread()
 		if (closethread)
 			break;
 
-//	statusUpdate(S_DEBUG,"...");
-
 	CURL *curl = curl_easy_init();
 
 	curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1); // Forbid keepalive connections.
@@ -567,16 +544,15 @@ void Scrobbler::workerThread()
 				submitinprogress = false;
 			}
 			
-			statusUpdate(S_CONNECT_ERROR,"Could not connect to server.");
-			statusUpdate(S_CONNECT_ERROR,curlerror);
+			XMP_Log("[DEBUG] %s.\n", curlerror);
 		}
 
 
 		ReleaseMutex(curlmutex);
 		// OK, if this was a handshake, it failed since readytosubmit isn't true. Submissions get cached.
 		while (!readytosubmit && !closethread) {
-			statusUpdate(S_HANDHAKE_NOTREADY,"Unable to handshake: sleeping...");
-			XMP_Log("[WARNING] Handshake with Last.fm server failed!\n");
+			XMP_Log("[DEBUG] Unable to handshake, sleeping before trying again.\n");
+			XMP_Log("[WARNING] Handshake with Last.fm server failed.\n");
 			Sleep(HS_FAIL_WAIT);
 			// and try again.
 			curl_easy_setopt(curl, CURLOPT_URL, hsstring.c_str());
@@ -586,25 +562,4 @@ void Scrobbler::workerThread()
 		}
 		curl_easy_cleanup(curl);
 	}
-}
-
-int Scrobbler::saveCache(const char *cache, const int numentries)
-{
-	cm->Save();
-	/*
-	ofstream f( cachefile.c_str(), ios::out | ios::trunc );
-	if( !f.is_open() ) return 0;
-	
-	f << cache;
-	
-	f.close();
-	*/
-	return 1;
-}
-
-int Scrobbler::loadCache()
-{
-	cm->Load();
-
-	return 0;
 }
