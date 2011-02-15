#ifndef _XMP_SCROBBLER_DATA_H
#define _XMP_SCROBBLER_DATA_H

#include <string>

using namespace std;

bool XMP_GetTagField( string data, const char *fn, char *buf );
string XMP_GetDataBlock(string name, string source);

bool XMP_ExtractTags_ID3v1(const char *data, char *artist, char *title, char *album);
bool XMP_ExtractTags_ID3v2(const char *data, char *artist, char *title, char *album);

bool XMP_ExtractTags_WMA(const char *data, char *artist, char *title, char *album);
bool XMP_ExtractTags_Other(const char *data, char *artist, char *title, char *album);
bool XMP_ExtractTags_MBID(const char *data, char *mb);

#endif
