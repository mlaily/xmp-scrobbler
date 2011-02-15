#ifndef _XMP_SCROBBLER_H
#define _XMP_SCROBBLER_H

#include <windows.h>
#include "xmpdsp.h"

#define CACHE_DEFAULT_SIZE	10
#define CACHE_PACKAGE_SIZE	10 // maximum number of tracks submitted at once

#define TAG_FIELD_SIZE		256

extern XMPFUNC_MISC *xmpfmisc;

struct TRACKDATA
{
	char artist[TAG_FIELD_SIZE];
	char title[TAG_FIELD_SIZE];
	char album[TAG_FIELD_SIZE];
	char mb[TAG_FIELD_SIZE];

	time_t playtime;
	DWORD length; // in seconds
	DWORD status; // -1 - too short, -2 - bad tags
};

struct SUBMITPACKAGE
{
	TRACKDATA tracks[CACHE_PACKAGE_SIZE];
	int size;
};

void XMP_Log( const char *s, ... );

#endif
