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
#ifndef _SCROBBLER_H
#define _SCROBBLER_H

#if !defined(WIN32) && !defined(LINUX)
#	error Port me!
#endif

#if defined(LINUX)
#	include <pthread.h>
#elif defined(WIN32)
#define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <winbase.h>
#	include <winuser.h>
#endif

#include <cstdlib>
#include <string>
#include <time.h>
#include <curl/curl.h>

#include "md5.h"

#include "../xmp-scrobbler.h"
#include "../cachemanager.h"

#ifdef SAFE_FUNCS
#include "..\..\external\safe_funcs\header.h"
#endif

#if defined(WIN32)
#	define THREADCALL DWORD WINAPI
#   define snprintf _snprintf
#elif defined(LINUX)
#	include <unistd.h>
#	define THREADCALL void *
#   define Sleep(milliSec)  usleep(milliSec * 1000)
//#   define stricmp strcasecmp
// My version of libcurl doesn't have curl_free anymore, I'll have to
//  add some stuff in autoconf to automate this (properly)
#   define curl_free free
#endif

using namespace std;

#define LS_VER		"1.4"
#define MINLENGTH	30
#define MAXLENGTH	10800
#define HS_FAIL_WAIT 100000

/**
	Audioscrobbler client class. Needs libcurl to run.
	$Id: scrobbler.h,v 1.14 2004/11/11 14:18:29 xurble Exp $

	@version	1.1
	@author		Russ Garrett (russ-scrobbler@garrett.co.uk)
*/

class Scrobbler {
public:

	/**
		Use this to initially set up the username and password. This sends a 
		handshake request to the server to determine the submit URL and 
		initial submit interval, and submissions will only be sent when 
		it recieves a response to this.
		
		@param user		The user's Audioscrobbler username.
		@param pass		The user's Audioscrobbler password.
		@param id		The 3-letter client id of this client.
		@param ver		The version of this client.
		@see			setPassword()
		@see			setUsername()
	*/
	Scrobbler(const char *user, const char *pass, const char id[2], const char *ver);
	virtual ~Scrobbler();
	
//	void XMP_Log( const char *s, ... );

	/**
		Call this to add a song to the submission queue. The submission will get
		sent immediately unless the server has told it to cache, in which case it
		will get sent with the first song submission after the cache period has 
		expired. 

		@param artist	The artist name.
		@param title	The title of the song.
		@param length	The length of the song in seconds.
		@param ltime	The time (unix time) in seconds when playback of the song started.
		@param trackid	The MusicBrainz TrackID of the song (in guid format).
		@retval 0		Failure. This could be because the song submitted was too short.
		@retval	1		This tripped song submission, Scrobbler is now connecting.
		@retval 2		The submission was cached.
	*/
//	int addSong(const char *artist, const char *title, const char *album, char mbid[37], int length, time_t ltime);
	int addSong( TRACKDATA track );

	/**
		Set the user's password if it's changed since the constructor.

		@param pass		The user's new password.
		@see			Scrobbler()
		@see			setUsername()
	*/
	void setPassword(const char *pass);

	//	should call just after contruction
	void init();

	// should call juts before deletion
	void term();

	/**
		Set the user's user name if it's changed since the constructor.

		@param user		The user's new user name.
		@see			Scrobbler()
		@see			setUsername()
	*/
	void setUsername(const char *user);

	/**
		Set the proxy. Don't call this repeatedly, as it'll try and 
		re-handshake if it hasn't already.

		@param proxy	The proxy name
		@param user		The proxy username (blank if none)
		@param pass		The proxy password (blank if none)
	*/
	void setProxy(const char *proxy, const char* user, const char* pass);

	/**
		Clears the internal submission cache.

		@see			setCache()
	*/
	void clearCache();

	void setCacheFile( string f ) { cachefile = f; };
	void setLogFile( string f ) { logfile = f; };

protected:

	/**
		Sets the contents of the submission cache, to be called from 
		loadCache().

		@param cache	The contents of the cache to set.
		@param numentries	The number of entries in the cache.
		@see			loadCache()
		@see			clearCache()
	*/
	void setCache(const char *cache, const int numentries);

private:
	
	CacheManager *cm;
	SUBMITPACKAGE current_sp;
	
	string prepareSubmitString();

	virtual void genSessionKey();

	void doSubmit();
	void doHandshake();

   // call back functions from curl
   void handleHeaders(char* header);
	void handleHandshake(char *handshake);
	void handleSubmit(char *data);

	void workerThread();

	static THREADCALL threadProc(void *param) { static_cast<Scrobbler*>(param)->workerThread(); return 1; }
	static size_t hs_write_data(void *data, size_t size, size_t nmemb, void *userp) 
   { 
		try
		{
			//	the buffer returned is not null terminated (it could be binary data)
			char* in = static_cast<char *>(data);

			//	since we're all about the strings, we create a new buffer big enough
			//	to hold the old one + a null terminator
			char* buffer = new char[nmemb+1];

			//	wipe it
			memset(buffer,0,nmemb+1);

			//	and copy in the old buffer
			memcpy(buffer,in,nmemb);
			//ATLTRACE(buffer);

			//	then we pass it to our handler class
         static_cast<Scrobbler *>(userp)->handleHandshake(buffer); 

			//	and clean up our new buffer
			delete[] buffer;
		}
		catch(...){XMP_Log("[ERROR] EXCEPTION IN HANDSHAKE DATA\n");}
		return nmemb; 
   }
   
   static size_t submit_write_data(void *data, size_t size, size_t nmemb, void *userp) 
   { 
		try
		{
			//	the buffer returned is not null terminated (it could be binary data)
			char* in = static_cast<char *>(data);

			//	since we're all about the strings, we create a new buffer big enough
			//	to hold the old one + a null terminator
			char* buffer = new char[nmemb+1];

			//	wipe it
			memset(buffer,0,nmemb+1);

			//	and copy in the old buffer
			memcpy(buffer,in,nmemb);
			//ATLTRACE(buffer);

			//	then we pass it to our handler class
         static_cast<Scrobbler *>(userp)->handleSubmit(buffer);

			//	and clean up our new buffer
			delete[] buffer;
		}
		catch(...){XMP_Log("[ERROR] EXCEPTION IN SUBMIT DATA\n");}
		return nmemb; 
   }
   
  /*static int curl_debug_callback (CURL * c, curl_infotype t, char * data, size_t size, void * a)
  {
		std::string tmp(data);
		
		tmp += "\n";
		::XMP_Log("%s",tmp.c_str());
		return 0;
	}*/

	//	this function called back by libcurl
	static size_t curl_header_write(void *data, size_t size, size_t nmemb, void *userp) 
	{ 
		try
		{
			//	the buffer returned is not null terminated (it could be binary data)
			char* in = static_cast<char *>(data);

			//	since we're all about the strings, we create a new buffer big enough
			//	to hold the old one + a null terminator
			char* buffer = new char[nmemb+1];

			//	wipe it
			memset(buffer,0,nmemb+1);

			//	and copy in the old buffer
			memcpy(buffer,in,nmemb);
			//ATLTRACE(buffer);

			//	then we pass it to our handler class
         static_cast<Scrobbler *>(userp)->handleHeaders(buffer);

			//	and clean up our new buffer
			delete[] buffer;
		}
		catch(...){XMP_Log("[ERROR] EXCEPTION IN WRITE HEADER\n");}
		return nmemb; 
	}
	
	char curlerror[CURL_ERROR_SIZE];

	string username;
	string password; // MD5 hash
	string challenge;
	string sessionkey;
	string hsstring;
	string submiturl;
	string submit;
	string clientid;
	string clientver;
	string useragent;
	string proxyserver;
	string proxyinfo;
	string poststring;
	string cachefile;
	string logfile;
	
	bool proxyauth;
	bool readytosubmit;
	bool submitinprogress;

	bool closethread;
#if defined(WIN32)
	HANDLE worker;
	HANDLE workerevent;
	HANDLE curlmutex;
#elif defined(LINUX)
	pthread_t worker;
	pthread_mutex_t workermutex;
	pthread_cond_t workerevent;
#endif

	int success;
	int songnum;

	time_t interval;
	time_t lastconnect;

   int lastRetCode;

};

#endif
