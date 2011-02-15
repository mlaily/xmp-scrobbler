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

	tracks_size = new_size;

	XMP_Log( "[DEBUG] Resizing cache array - new length: %d\n", tracks_size );

	tracks = tmp;
}

void CacheManager::AddTrack( TRACKDATA t )
{
	if( cache_size + 1 > tracks_size )
		ResizeArray( tracks_size * 2 );

	XMP_Log( "[DEBUG] AddTrack (cache_size = %d, tracks_size = %d)\n", cache_size, tracks_size );

	tracks[cache_size++] = t;
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
	XMP_Log("[DEBUG] CacheManager::Load()\n");

	Init();

	ifstream f( cacheFile.c_str(), ios::in | ios::binary );
	if( !f.is_open() ) return false;

	f.seekg(0, ios::beg);
  	std::ifstream::pos_type begin_pos = f.tellg();
  	f.seekg(0, ios::end);

	int len = static_cast<int>(f.tellg() - begin_pos);

	if( tracks_size < len / sizeof(TRACKDATA) )
		ResizeArray( len / sizeof(TRACKDATA) );

	f.seekg(0, ios::beg);

	while( len > 0 )
	{
		f.read( (char*) &tracks[cache_size++], sizeof(TRACKDATA) );

		len -= sizeof(TRACKDATA);
	}

	XMP_Log( "[INFO] Number of entries in the cache: %d\n", GetCacheSize() );

	f.close();
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
