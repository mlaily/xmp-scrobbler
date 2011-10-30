#include <string>
#include <iostream>
#include <fstream>

#include "cachemanager.h"
#include "xmp-scrobbler.h"

void CacheManager::Init()
{
	if( tracks )
		delete [] tracks;

	cache_size = 0;
	tracks_size = CACHE_DEFAULT_SIZE;

	tracks = new TRACKDATA[CACHE_DEFAULT_SIZE];

	memset( tracks, 0, sizeof(TRACKDATA) * CACHE_DEFAULT_SIZE );
}

void CacheManager::ResizeArray( int new_size )
{
	// make sure that we will be able to fit current cache

	if( new_size < cache_size )
		return;

	TRACKDATA *tmp = new TRACKDATA[new_size];

	memset( tmp, 0, sizeof(TRACKDATA) * new_size );
	memcpy( tmp, tracks, sizeof(TRACKDATA) * cache_size );

	delete [] tracks;
	XMP_Log("[DEBUG] Resizing cache array, from %d to %d slots.\n", tracks_size, new_size);

	tracks_size = new_size;
	tracks = tmp;
}

void CacheManager::AddTrack( TRACKDATA t )
{
	if( cache_size + 1 > tracks_size )
		ResizeArray( tracks_size * 2 );

	tracks[cache_size++] = t;
	XMP_Log("[DEBUG] Adding track to cache: %d track%s in cace, %d slots.\n", cache_size, !(cache_size - 1) ? "" : "s", tracks_size);
}

void CacheManager::DeleteTracks( int n )
{
	if( n > cache_size )
		n = cache_size;

	memcpy( (char *) tracks, (char *) &tracks[n], sizeof(TRACKDATA) * (cache_size - n) );

	cache_size -= n;
}

BOOL CacheManager::Save()
{
	ofstream f( cacheFile.c_str(), ios::out | ios::trunc );
	if( !f.is_open() ) return false;

	for( int i = 0; i < cache_size; i++ )
		f.write( (char*) &tracks[i], sizeof(TRACKDATA) );

	f.close();
	
	return true;
}

BOOL CacheManager::Load()
{
	XMP_Log("[DEBUG] Loading cache file.\n");

	Init();

	ifstream f( cacheFile.c_str(), ios::in | ios::binary );
	if( !f.is_open() ) return false;

	f.seekg(0, ios::beg);
  	std::ifstream::pos_type begin_pos = f.tellg();
  	f.seekg(0, ios::end);

	int len = static_cast<int>(f.tellg() - begin_pos);

	if(tracks_size < len / (int) sizeof(TRACKDATA))
		ResizeArray( len / sizeof(TRACKDATA) + 10 );

	f.seekg(0, ios::beg);

	while( len > 0 )
	{
		f.read( (char*) &tracks[cache_size++], sizeof(TRACKDATA) );

		len -= sizeof(TRACKDATA);
	}

	XMP_Log("[INFO] There %s %d %s in cache.\n", !(cache_size - 1) ? "is" : "are", cache_size, !(cache_size - 1) ? "entry" : "entries");

	f.close();
	return true;
}

SUBMITPACKAGE CacheManager::GetSubmitPackage()
{
	SUBMITPACKAGE sp;
	int i = 0;

	while( i < CACHE_PACKAGE_SIZE && i < cache_size )
	{
		sp.tracks[i] = tracks[i];
		i++;
	}

	sp.size = i;

	return sp;
}
