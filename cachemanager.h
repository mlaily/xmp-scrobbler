#ifndef _XMP_SCROBBLER_CACHE_MANAGER_H
#define _XMP_SCROBBLER_CACHE_MANAGER_H

#include <string>
#include "xmp-scrobbler.h"

using namespace std;

class CacheManager
{
	public:

		CacheManager() { tracks = NULL; };
		CacheManager( string path ) { tracks = NULL; SetCacheFile( path ); };

		void AddTrack( TRACKDATA t );
		void DeleteTracks( int n );

		BOOL Load();
		BOOL Save();

		SUBMITPACKAGE GetSubmitPackage();

		void SetCacheFile( string path ) { cacheFile = path; }
		string GetCacheFile() { return cacheFile; }

		int GetCacheSize() { return cache_size; }

	private:

		void Init();
		void ResizeArray( int new_size );

		string cacheFile;

		TRACKDATA *tracks;

		int tracks_size;
		int cache_size;
};

#endif
