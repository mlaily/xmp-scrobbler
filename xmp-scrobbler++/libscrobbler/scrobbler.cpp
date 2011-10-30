/*
    This file is part of libscrobbler.

    libscrobbler is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    libscrobbler is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libscrobbler; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

	Copyright © 2003 Russell Garrett (russ-scrobbler@garrett.co.uk)
*/
#include <iostream>
#include <fstream>
#include "scrobbler.h"
#include "errors.h"

using namespace std;

// -----------------------------------------------------------
Scrobbler::Scrobbler(const char *user, const char *pass, const char *id, const char *ver) 
{
	setPassword(pass);
	setUsername(user);
	clientid = id; clientver = ver;
	char buf[64];
/* There is a bug in MSVC8 
   http://connect.microsoft.com/VisualStudio/feedback/details/99662/templated-vsnprintf-s-and-snprintf-s-buggy
   So give up */
	snprintf(buf, sizeof(buf), "Mozilla/5.0 (compatible; libscrobbler %s; %s %s)", LS_VER, id, ver);
	useragent = buf;
	closethread = false;
	proxyauth = false;
	interval = lastconnect = 0;
	submitinprogress = false;
#if defined(WIN32)
	DWORD threadid; // Needed for Win9x
	workerevent = CreateEvent(NULL, false, false, NULL);
	if (!workerevent)
		throw EOutOfMemory();
	curlmutex = CreateMutex(NULL, false, NULL);
	if (!curlmutex)
		throw EOutOfMemory();
	worker = CreateThread(NULL, 0, threadProc, (LPVOID)this, 0, &threadid);
	if (!worker)
		throw EOutOfMemory();
#elif defined(LINUX)
#error "CURL mutex stuff not ported to linux because I can't be bothered"
	pthread_mutex_init(&workermutex, NULL);
	pthread_cond_init(&workerevent, NULL);
	/*
	  Ian says: this doesn't compile for me, so I'm leaving it out
	   until I can figure out why.
	if (!workermutex || !workerevent)
	  throw EOutOfMemory(); */
	pthread_create(&worker, NULL, threadProc, (void *)this);
	if (!worker)
		throw EOutOfMemory();
	Sleep(0);
#endif
	
}

// -----------------------------------------------------------
Scrobbler::~Scrobbler()
{
	closethread = true;
#if defined(WIN32)
	SetEvent(workerevent);
//	XMP_Log("before\n");
	WaitForSingleObject(curlmutex, INFINITE);
	CloseHandle(curlmutex);
	Sleep(0);
//	TerminateThread(worker, 0);
#elif defined(LINUX)
	pthread_cond_signal(&workerevent);
	Sleep(0);
#endif
}

void Scrobbler::init()
{
	cm = new CacheManager( cachefile );

	clearCache();
	// Load the cache
	cm->Load();
	
	XMP_Log("[DEBUG] Cache loaded, scheduling handshake.\n");
	
	doHandshake();
}

void Scrobbler::term()
{
	// The cache file should be up to date, no need to save it when exiting.
	// cm->Save();
	delete cm;
}

// -----------------------------------------------------------
void Scrobbler::setPassword(const char *pass)
{
	if (!strlen(pass))
		return;
	md5_state_t md5state;
	unsigned char md5pword[16];
	md5_init(&md5state);
	md5_append(&md5state, (unsigned const char *)pass, (int)strlen(pass));
	md5_finish(&md5state, md5pword);
	char tmp[33];
	/* Note: removed strncpy() */
	memset(tmp, '\0', sizeof(tmp));
	for (int j = 0;j < 16;j++) {
		char a[3];
		snprintf(a, sizeof(a), "%02x", md5pword[j]);
		tmp[2*j] = a[0];
		tmp[2*j+1] = a[1];
	}
	password = tmp;
	genSessionKey();
}

// -----------------------------------------------------------
void Scrobbler::setUsername(const char *user)
{
	if (!strlen(user))
		return;
		
	if (readytosubmit && !clientid.empty()) {
		XMP_Log("[WARNING] Please restart XMPlay for user change to take effect.\n");
		return;
	}

   char* u = curl_escape(user,strlen(user));
	username = u;
   curl_free(u);

	hsstring = "http://post.audioscrobbler.com/?hs=true&p=1.1&c=" + clientid + "&v=" + clientver + "&u=" + username;
}

// -----------------------------------------------------------
void Scrobbler::setProxy(const char* proxy, const char* user, const char* password) 
{
	proxyauth = (strlen( user ) > 0) || (strlen( password ) > 0);
	
	if( !proxyauth )
	{
		proxyinfo = "";
	} else
	{
		proxyinfo = user;
		proxyinfo += ":";
		proxyinfo += password;
	}

	if( strlen( proxy ) < 1 ) // no proxy server
	{
		proxyserver.clear();
		proxyinfo.clear();
		proxyauth = false;
	}	else
	{
		proxyserver = proxy;

		XMP_Log("[DEBUG] Operating through proxy%s: %s\n", proxyauth ? " with authentication" : "", proxyserver.c_str());
	}

	if( !readytosubmit )
		doHandshake();
}

// -----------------------------------------------------------
void Scrobbler::clearCache()
{
	songnum = 0;
	poststring = "";
}

// -----------------------------------------------------------
void Scrobbler::setCache(const char *cache, const int numentries)
{
	poststring = cache;
	songnum = numentries;
}

string Scrobbler::prepareSubmitString()
{
	current_sp = cm->GetSubmitPackage();
	string submitStr;

	XMP_Log("[INFO] Preparing to submit %d track%s from cache.\n", current_sp.size, !(current_sp.size - 1) ? "" : "s");

	for( int count = 0; count < current_sp.size; count++ )
	{
		TRACKDATA track = current_sp.tracks[count];

		char ti[20];

/* In case of MSVC use security-enhanced functions */
#if defined(_MSC_VER) && (_MSC_VER >= 1000)
		struct tm today;
		gmtime_s(&today, &track.playtime);
		strftime(ti, sizeof(ti), "%Y-%m-%d %H:%M:%S", &today);
#else
		struct tm *today = gmtime(&track.playtime);
		strftime(ti, sizeof(ti), "%Y-%m-%d %H:%M:%S", today);
#endif
		char *a = curl_escape(track.artist, (int)strlen(track.artist));
		char *b = curl_escape(track.album, (int)strlen(track.album));
		char *t = curl_escape(track.title, (int)strlen(track.title));
		char *i = curl_escape(ti, (int)strlen(ti));
		char *m = curl_escape(track.mb, (int)strlen(track.mb));
		char *buf = (char *)malloc(48 + strlen(a) + strlen(b) + strlen(t) + strlen(i) + strlen(m));

		snprintf(buf, sizeof(buf), "&a[%d]=%s&b[%d]=%s&t[%d]=%s&i[%d]=%s&l[%d]=%ld&m[%d]=%s", count, a, count, b, count, t, count, i, count, track.length, count, track.mb);

		submitStr += buf;
		
		free( buf );
		
		curl_free(a);
		curl_free(b);
		curl_free(t);
		curl_free(i);
		curl_free(m);
	}

	return submitStr;
}

// -----------------------------------------------------------
//int Scrobbler::addSong(const char *artist, const char *title, const char *album, char mbid[37], int length, time_t ltime)
int Scrobbler::addSong( TRACKDATA track )
{
	cm->AddTrack( track );
	cm->Save();

/*	if (track.length <= MINLENGTH || track.length > MAXLENGTH) // made <= to minlength to stop iTMS previews being submitted in iTunes
		return 0;
*/
	XMP_Log("[INFO] Track added to cache for submission.\n");

	if(submitinprogress)
	{
		XMP_Log("[DEBUG] Previous submission still in progress.\n");
		return 0;
	}
	
	/*if(poststring.find(timeCheck) != poststring.npos)
   {
      // we have already tried to add a song at this time stamp
      // I have no idea how this could happen but apparently it does so
      // we stop it now

      statusUpdate(S_NOT_SUBMITTING,submitStr.c_str());
      statusUpdate(S_NOT_SUBMITTING,poststring.c_str());

      statusUpdate(S_NOT_SUBMITTING,"Submission error, duplicate subbmission time found");
      return 3;
   }
   */
  poststring = prepareSubmitString();
  songnum++;

  time_t now;
	time (&now);
	if ((interval + lastconnect) < now) {
		doSubmit();
		return 1;
	} else {
		int cache_time = interval + lastconnect - now;
		XMP_Log("[DEBUG] Not submitting, caching for %d more second%s. Cache has %d %s.\n", cache_time,
			!(cache_time - 1) ? "" : "s", songnum, !(songnum - 1)? "entry" : "entries");
		return 2;
	}
}

// -----------------------------------------------------------
void Scrobbler::doHandshake()
{
	readytosubmit = false;
	time_t now; 
	time (&now);
	lastconnect = now;
	hsstring = "http://post.audioscrobbler.com/?hs=true&p=1.1&c=" + clientid + "&v=" + clientver + "&u=" + username;
#if defined(WIN32)
	SetEvent(workerevent);
#elif defined(LINUX)
	pthread_cond_signal(&workerevent);
	Sleep(0);
#endif
}

// -----------------------------------------------------------
void Scrobbler::doSubmit()
{
	if(submitinprogress)
	{
		XMP_Log("[DEBUG] Previous submission still in progress.\n");
		return;
	}
	submitinprogress = true;

	if (username == "" || password == "" || !readytosubmit)
		return;

	//XMP_Log("[DEBUG] Submitting.\n");
	time_t now;
	time (&now);
	lastconnect = now;
	submit = "u=" + username + "&s=" + sessionkey + poststring;

#if defined(WIN32)
	SetEvent(workerevent);
#elif defined(LINUX)
	pthread_cond_signal(&workerevent);
	Sleep(0);
#endif
}

// -----------------------------------------------------------
void Scrobbler::handleHandshake(char *handshake)
{
	// Doesn't take into account multiple-packet returns (not that I've seen one yet)...

  // Ian says: strtok() is not re-entrant, but since it's only being called
  //  in only one function at a time, it's ok so far.
	/* Same is true for strsep() */

   if(lastRetCode != 200)
   {
      // no point in doing this unless we get an OK
      //XMP_Log("[DEBUG] NOT 200.\n");
      return;
   }

	char seps[] = " \n";
#ifndef SAFE_FUNCS
	char *response = strtok(handshake, seps);
#else
	char *response = strsep(&handshake, seps);
#endif
	
	if (!response) {
		XMP_Log("[DEBUG] Handshake failed, the server response is invalid.\n");
		return;
	}
	do {
		if (strncmp("UPTODATE", response, 8) == 0) {
			XMP_Log("[DEBUG] Handshake response: client up to date.\n");
		} else if (strncmp("UPDATE", response, 6) == 0) {
#ifndef SAFE_FUNCS
			char *updateurl = strtok(NULL, seps);
#else
			char *updateurl = strsep(&handshake, seps);
#endif
			if (!updateurl)
				break;
			XMP_Log("[WARNING] Please update the plugin at: %s\n", updateurl);
		} else if (strncmp("BADUSER", response, 7) == 0) {
			XMP_Log("[WARNING] At least your username is incorrect, please correct it in the plugin config.\n");
			return;
		} else {
			break;
		}
#ifndef SAFE_FUNCS
		challenge = strtok(NULL, seps);
		submiturl = strtok(NULL, seps);
#else
		challenge = strsep(&handshake, seps);
		submiturl = strsep(&handshake, seps);
#endif

		XMP_Log("[DEBUG] Submission URL: %s\n", submiturl.c_str());
		
#ifndef SAFE_FUNCS
		char *inttext = strtok(NULL, seps);
#else
		char *inttext = strsep(&handshake, seps);
#endif
		if (!inttext || !(strncmp("INTERVAL", inttext, 8) == 0))
			break;
#ifndef SAFE_FUNCS
		interval = atoi(strtok(NULL, seps));
#else
		interval = atoi(strsep(&handshake, seps));
#endif
		XMP_Log("[DEBUG] Submission interval set to %d second%s.\n", interval, !(interval - 1) ? "" : "s");
		genSessionKey();
		readytosubmit = true;
		XMP_Log("[INFO] Successfully connected to the Last.fm server.\n");
		return;
	} while (0);
#ifndef SAFE_FUNCS
	XMP_Log("[DEBUG] Handshake failed: %s.\n", strtok(NULL, "\n"));
#else
	XMP_Log("[DEBUG] Handshake failed: %s.\n", strsep(&handshake, "\n"));
#endif
	XMP_Log("[WARNING] The attempt to connect to the Last.fm server failed!\n");
}

// -----------------------------------------------------------
void Scrobbler::handleSubmit(char *data)
{
	//	submit returned 
	submitinprogress = false; //- this should already have been cancelled by the header callback

   if(lastRetCode != 200)
   {
      // there's no real point in checking this is there?
      return;
   }

	// Doesn't take into account multiple-packet returns (not that I've seen one yet)...
	char seps[] = " \n";
#ifndef SAFE_FUNCS
	char *response = strtok(data, seps);
#else
	char *response = strsep(&data, seps);
#endif
	if (!response) {
		XMP_Log("[DEBUG] Submission failed, the server response is invalid.\n");
		return;
	}
	if (strncmp("OK", response, 2) == 0) {
		XMP_Log("[INFO] Submission succeeded.\n");

		clearCache();
		cm->DeleteTracks( current_sp.size );
		cm->Save();
#ifndef SAFE_FUNCS
		char *inttext = strtok(NULL, seps);
#else
		char *inttext = strsep(&data, seps);
#endif
		if (inttext && (strncmp("INTERVAL", inttext, 8) == 0)) {
#ifndef SAFE_FUNCS
			interval = atoi(strtok(NULL, seps));
#else
			interval = atoi(strsep(&data, seps));
#endif
			XMP_Log("[DEBUG] Submission interval set to %d second%s.\n", interval, !(interval - 1) ? "" : "s");
		}
	} else if (strncmp("BADAUTH", response, 7) == 0) {
		XMP_Log("[DEBUG] Submission failed, the server reported bad authorization.\n");
		XMP_Log("[WARNING] Your login info is incorrect, please correct it in the plugin config.\n");
#ifndef SAFE_FUNCS
		char *inttext = strtok(NULL, seps);
#else
		char *inttext = strsep(&data, seps);
#endif
		if (inttext && (strncmp("INTERVAL", inttext, 8) == 0)) {
#ifndef SAFE_FUNCS
			interval = atoi(strtok(NULL, seps));
#else
			interval = atoi(strsep(&data, seps));
#endif
			XMP_Log("[DEBUG] Submission interval set to %d second%s.\n", interval, !(interval - 1) ? "" : "s");
		}
	} else {
		/* FAILED and BADUSER require interval too */
#ifndef SAFE_FUNCS
		XMP_Log("[DEBUG] Submission failed: %s.\n", strtok(NULL, "\n"));
		char *inttext = strtok(NULL, seps);
#else
		XMP_Log("[DEBUG] Submission failed: %s.\n", strsep(&data, "\n"));
		char *inttext = strsep(&data, seps);
#endif
		if (inttext && (strncmp("INTERVAL", inttext, 8) == 0)) {
#ifndef SAFE_FUNCS
			interval = atoi(strtok(NULL, seps));
#else
			interval = atoi(strsep(&data, seps));
#endif
			XMP_Log("[DEBUG] Submission interval set to %d second%s.\n", interval, !(interval - 1) ? "" : "s");
		}		
	}
}

// -----------------------------------------------------------
void Scrobbler::genSessionKey() {
	string clear = password + challenge;
    md5_state_t md5state;
	unsigned char md5pword[16];
	md5_init(&md5state);
	md5_append(&md5state, (unsigned const char *)clear.c_str(), (int)clear.length());
	md5_finish(&md5state, md5pword);
	char key[33];
	/* Note: removed strncpy() */
	memset(key, '\0', sizeof(key));
	for (int j = 0;j < 16;j++) {
		char a[3];
		snprintf(a, sizeof(a) + 1, "%02x", md5pword[j]);
		key[2*j] = a[0];
		key[2*j+1] = a[1];
	}
	sessionkey = key;
}

void Scrobbler::handleHeaders(char* header)
{
   submitinprogress = false;

	std::string code;
   std::string headerStr = header;
//   XMP_Log("[DEBUG] %s.\n", header);
   
   if(headerStr.substr(0,4) == "HTTP") // this is the response code
	{
      //XMP_Log("[DEBUG] %s.\n", header);
		size_t pos = headerStr.find(" ");
		
		if(pos != headerStr.npos)
		{
			code = headerStr.substr(pos+1);
			pos = code.find(" ");

			if(pos != code.npos)
				code = code.substr(0,pos);

			lastRetCode = atoi(code.c_str());
/*         if(lastRetCode == 100) // 100-continue
         {
           doSubmit();
         } else*/ if(lastRetCode != 200)
			XMP_Log("[DEBUG] The server reported a processing error:\n\n%s\n\n", header);
		}
	}
}

// -----------------------------------------------------------
void Scrobbler::workerThread()
{
	while (1) {
#if defined(WIN32)
		WaitForSingleObject(workerevent, INFINITE);
#elif defined(LINUX)
		pthread_cond_wait(&workerevent, &workermutex);
#endif
		if (closethread)
			break;

	CURL *curl = curl_easy_init();

	curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1); // Forbid keepalive connections.
	curl_easy_setopt(curl, CURLOPT_USERAGENT, useragent.c_str());
	curl_easy_setopt(curl, CURLOPT_PROXY, proxyserver.c_str());
	curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, proxyinfo.c_str());

	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_write);
	curl_easy_setopt(curl, CURLOPT_WRITEHEADER, this);
	
//	curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_debug_callback);
	

	if (proxyauth) {
		curl_easy_setopt(curl, CURLOPT_USERPWD, proxyinfo.c_str());
		curl_easy_setopt(curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
	} else {
		curl_easy_setopt(curl, CURLOPT_PROXYAUTH, 0);
	}
	
	curl_easy_setopt(curl, CURLOPT_TIMEOUT , 30);
	
	curl_slist *slist=NULL;
	
	if (!readytosubmit)
	{
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
		curl_easy_setopt(curl, CURLOPT_URL, hsstring.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, hs_write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlerror);
	} else
	{
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, submit.c_str());
		curl_easy_setopt(curl, CURLOPT_URL, submiturl.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, submit_write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlerror);
		
		

        slist = curl_slist_append(slist, "Expect:");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
	}

		WaitForSingleObject(curlmutex, INFINITE);

		success = curl_easy_perform(curl);
		
		if(slist)
          curl_slist_free_all(slist);

		if(success)
		{
			// non zero success actually means fail
			if(readytosubmit)
			{
				// failed to post, means post is over
				submitinprogress = false;
			}
			
			XMP_Log("[DEBUG] %s.\n", curlerror);
		}


		ReleaseMutex(curlmutex);
		// OK, if this was a handshake, it failed since readytosubmit isn't true. Submissions get cached.
		while (!readytosubmit && !closethread) {
			XMP_Log("[DEBUG] Unable to handshake, sleeping before trying again.\n");
			XMP_Log("[WARNING] Handshake with Last.fm server failed.\n");
			Sleep(HS_FAIL_WAIT);
			// and try again.
			curl_easy_setopt(curl, CURLOPT_URL, hsstring.c_str());
			WaitForSingleObject(curlmutex, INFINITE);
			success = curl_easy_perform(curl);
			ReleaseMutex(curlmutex);
		}
		curl_easy_cleanup(curl);
	}
}
