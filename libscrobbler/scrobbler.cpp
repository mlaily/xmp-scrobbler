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
	loadCache();
	
	XMP_Log("[DEBUG] Scrobbler::init() - cache loaded, doing handshake...\n");
	
	doHandshake();
}

void Scrobbler::term()
{
	saveCache(poststring.c_str(), songnum);
	
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
	strncpy(tmp, "\0", sizeof(tmp));
	for (int j = 0;j < 16;j++) {
		char a[3];
		sprintf(a, "%02x", md5pword[j]);
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

		std::string temp = "Using proxy server: " + proxyserver;
		
		if( proxyauth )
			temp += " (with authentication - user and password are set in options)";
		
		statusUpdate(S_DEBUG,temp.c_str());
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

	XMP_Log( "[INFO] Preparing to submit %d track(s) from the cache\n", current_sp.size );

	for( int count = 0; count < current_sp.size; count++ )
	{
		TRACKDATA track = current_sp.tracks[count];

		char ti[20];
		struct tm *today = gmtime(&track.playtime);

		strftime(ti, sizeof(ti), "%Y-%m-%d %H:%M:%S", today);

		char *a = curl_escape(track.artist, (int)strlen(track.artist));
		char *b = curl_escape(track.album, (int)strlen(track.album));
		char *t = curl_escape(track.title, (int)strlen(track.title));
		char *i = curl_escape(ti, (int)strlen(ti));
		char *m = curl_escape(track.mb, (int)strlen(track.mb));
		char *buf = (char *)malloc(48 + strlen(a) + strlen(b) + strlen(t) + strlen(i) + strlen(m));

		sprintf(buf, "&a[%i]=%s&b[%i]=%s&t[%i]=%s&i[%i]=%s&l[%i]=%i&m[%i]=%s", count, a, count, b, count, t, count, i, count, track.length, count, track.mb);

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

/*	if (track.length <= MINLENGTH || track.length > MAXLENGTH) // made <= to minlength to stop iTMS previews being submitted in iTunes
		return 0;
*/
	XMP_Log("[INFO] Track added to the cache for submission\n");

	if(submitinprogress)
	{
		statusUpdate(S_NOT_SUBMITTING,"Previous submission still in progress");
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
		char msg[128];
		snprintf(msg, sizeof(msg), "Not submitting, caching for %i more second(s). Cache is %i entries.", (int)(interval + lastconnect - now), songnum);
		statusUpdate(S_NOT_SUBMITTING,msg);
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
		statusUpdate(S_NOT_SUBMITTING,"Previous submission still in progress");
		return;
	}
	submitinprogress = true;

	if (username == "" || password == "" || !readytosubmit)
		return;

	statusUpdate(S_SUBMITTING,"Submitting...");
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

   if(lastRetCode != 200)
   {
      // no point in doing this unless we get an OK
      //statusUpdate(S_DEBUG,"NOT 200");
      return;
   }

	char seps[] = " \n\r";
	char *response = strtok(handshake, seps);
	if (!response) {
		statusUpdate(S_HANDSHAKE_INVALID_RESPONSE,"Handshake failed: Response invalid");
		return;
	}
	do {
		if (stricmp("UPTODATE", response) == 0) {
			statusUpdate(S_HANDSHAKE_UP_TO_DATE,"Handshaking: Client up to date.");
		} else if (stricmp("UPDATE", response) == 0) {
			char *updateurl = strtok(NULL, seps);
			if (!updateurl)
				break;
			string msg = "Handshaking: Please update your client at: ";
			msg += updateurl;
			statusUpdate(S_HANDSHAKE_OLD_CLIENT,msg.c_str());
		} else if (stricmp("BADUSER", response) == 0) {
			statusUpdate(S_HANDSHAKE_BAD_USERNAME,"Handshake failed: Bad username");
			return;
		} else {
			break;
		}
		challenge = strtok(NULL, seps);
		submiturl = strtok(NULL, seps);
		
		statusUpdate(S_DEBUG, submiturl.c_str());
		
		char *inttext = strtok(NULL, seps);
		if (!inttext || !(stricmp("INTERVAL", inttext) == 0))
			break;
		setInterval(atoi(strtok(NULL, seps)));
		genSessionKey();
		readytosubmit = true;
		statusUpdate(S_HANDSHAKE_SUCCESS,"Handshake successful.");
		XMP_Log("[INFO] Handshake with Last.fm server successful!\n");
		return;
	} while (0);
	char buf[256];
	snprintf(buf, sizeof(buf), "Handshake failed: %s", strtok(NULL, "\n"));
	statusUpdate(S_HANDSHAKE_ERROR,buf);
	XMP_Log("[WARNING] Handshake with Last.fm server failed!\n");
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
	char seps[] = " \n\r";
	char * response = strtok(data, seps);
	if (!response) {
		statusUpdate(S_SUBMIT_INVALID_RESPONSE,"Submission failed: Response invalid");
		return;
	}
	if (stricmp("OK", response) == 0) {
		statusUpdate(S_SUBMIT_SUCCESS,"Submission succeeded.");
		XMP_Log("[INFO] Submission succeeded!\n");

		clearCache();
		cm->DeleteTracks( current_sp.size );
		char *inttext = strtok(NULL, seps);
		if (inttext && (stricmp("INTERVAL", inttext) == 0)) {
			interval = atoi(strtok(NULL, seps));
			char buf[128];
			snprintf(buf, sizeof(buf), "Submit interval set to %i second(s).", interval);
			statusUpdate(S_SUBMIT_INTERVAL,buf);
		}
	} else if (stricmp("BADPASS", response) == 0) {
		statusUpdate(S_SUBMIT_BAD_PASSWORD,"Submission failed: bad password.");
	} else if (stricmp("FAILED", response) == 0) {
		char buf[256];
		snprintf(buf, sizeof(buf), "Submission failed: %s", strtok(NULL, "\n"));
		statusUpdate(S_SUBMIT_FAILED, buf);
		char *inttext = strtok(NULL, seps);
		if (inttext && (stricmp("INTERVAL", inttext) == 0)) {
			interval = atoi(strtok(NULL, seps));
			snprintf(buf, sizeof(buf), "Submit interval set to %i second(s).", interval);
			statusUpdate(S_SUBMIT_INTERVAL,buf);
		}
	} else if (stricmp("BADAUTH",response) == 0) {
		statusUpdate(S_SUBMIT_BADAUTH,"Submission failed: bad authorization.");
	} else {
		char buf[256];
		snprintf(buf, sizeof(buf), "Submission failed: %s", strtok(NULL, "\n"));
		statusUpdate(S_SUBMIT_FAILED,buf);
	}
}

// -----------------------------------------------------------
void Scrobbler::setInterval(int in) {
	interval = in;
	string ret;
	ret = "Submit interval set to ";
	char i[10];
	itoa(in, i, 10);
	ret += i;
	ret += " second(s).";
	statusUpdate(S_SUBMIT_INTERVAL, ret.c_str());
}

void Scrobbler::genSessionKey() {
	string clear = password + challenge;
    md5_state_t md5state;
	unsigned char md5pword[16];
	md5_init(&md5state);
	md5_append(&md5state, (unsigned const char *)clear.c_str(), (int)clear.length());
	md5_finish(&md5state, md5pword);
	char key[33];
	strncpy(key, "\0", sizeof(key));
	for (int j = 0;j < 16;j++) {
		char a[3];
		sprintf(a, "%02x", md5pword[j]);
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
//   statusUpdate(S_DEBUG,header);
   
   if(headerStr.substr(0,4) == "HTTP") // this is the response code
	{
      //statusUpdate(S_DEBUG,header);
		size_t pos = headerStr.find(" ");
		
		if(pos != headerStr.npos)
		{
			code = headerStr.substr(pos+1);
			pos = code.find(" ");
			if(pos != code.npos)
			{
				code = code.substr(0,pos);
			}
			lastRetCode = atoi(code.c_str());
/*         if(lastRetCode == 100) // 100-continue
         {
           doSubmit();
         } else*/ if(lastRetCode != 200)
         {
            statusUpdate(S_CONNECT_ERROR,"The server reported a processing error.");
            statusUpdate(S_DEBUG,header);
         }
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

//	statusUpdate(S_DEBUG,"...");

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
			
			statusUpdate(S_CONNECT_ERROR,"Could not connect to server.");
			statusUpdate(S_CONNECT_ERROR,curlerror);
		}


		ReleaseMutex(curlmutex);
		// OK, if this was a handshake, it failed since readytosubmit isn't true. Submissions get cached.
		while (!readytosubmit && !closethread) {
			statusUpdate(S_HANDHAKE_NOTREADY,"Unable to handshake: sleeping...");
			XMP_Log("[WARNING] Handshake with Last.fm server failed!\n");
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

int Scrobbler::saveCache(const char *cache, const int numentries)
{
	cm->Save();
	/*
	ofstream f( cachefile.c_str(), ios::out | ios::trunc );
	if( !f.is_open() ) return 0;
	
	f << cache;
	
	f.close();
	*/
	return 1;
}

int Scrobbler::loadCache()
{
	cm->Load();

	return 0;
}
