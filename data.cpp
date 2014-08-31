#include <string>
#include <iostream>
#include <fstream>

#include "xmp-scrobbler.h"

using namespace std;

// return true if tags were extracted properly, false if they were not

string lowercase( string word );

string lowercase( string word )
{
 string ret_str = "";

  for (unsigned int index = 0; index < word.size(); index++)
    ret_str += tolower(word[index]);
 
  return ret_str;
}

// returns specified data block from XMPlay info1
// NOTE: block has empty line at the end so that extracting tags is easier

string XMP_GetDataBlock(string name, string source)
{
	string temp = lowercase(source);
	name = lowercase(name) + ":\n";

	int start = temp.find(name, 0);

	if( start == string::npos )
		return "";

	start += name.length();

	int next_start = source.find(":\n", start);
    int block_end = source.length();

	if(next_start >= 0)
		block_end = source.find_last_of("\n", next_start);

	return source.substr(start, block_end - start - 1) + '\n';
}

// extracts the value of the field fn (fn\tvalue)

bool XMP_GetTagField( string data, const char *fn, char *buf )
{
	int fnlen = strlen( fn );
	string tmp = "\n" + lowercase( data );

	DWORD start = tmp.find( fn, 0 );
		
	if( start == string::npos )
		return false;

	start += fnlen;

	DWORD end = tmp.find("\n", start);

	memcpy(buf, data.c_str() + start - 1, end - start);

	return true;
}

bool XMP_ExtractTags_ID3v1(const char *data, char *artist, char *title, char *album)
{
	string block = XMP_GetDataBlock( "ID3v1", data );

	if(!XMP_GetTagField(block, "\nartist\t", artist))
        return false;

    if(!XMP_GetTagField(block, "\ntitle\t", title))
        return false;

    // album tag is optional

	XMP_GetTagField(block, "\nalbum\t", album);
	
	return true;
}

bool XMP_ExtractTags_ID3v2(const char *data, char *artist, char *title, char *album)
{
	string block = XMP_GetDataBlock( "ID3v2", data );

	if(!XMP_GetTagField(block, "\nartist\t", artist))
        return false;

    if(!XMP_GetTagField(block, "\ntitle\t", title))
        return false;

    // album tag is optional

	XMP_GetTagField(block, "\nalbum\t", album);
	
	return true;
}

bool XMP_ExtractTags_WMA(const char *data, char *artist, char *title, char *album)
{
	string tmp(data);

	if(!XMP_GetTagField(tmp, "\nauthor\t", artist))
        return false;

    if(!XMP_GetTagField(tmp, "\ntitle\t", title))
        return false;

    // album tag is optional

	XMP_GetTagField(tmp, "\nwm/albumtitle\t", album);
	
	return true;
}

bool XMP_ExtractTags_Other(const char *data, char *artist, char *title, char *album)
{
	string tmp(data);

	if(!XMP_GetTagField(tmp, "\nartist\t", artist))
        return false;

    if(!XMP_GetTagField(tmp, "\ntitle\t", title))
        return false;

    // album tag is optional

	XMP_GetTagField(tmp, "\nalbum\t", album);
	
	return true;
}

extern bool XMP_IsRadio;               /* xmp-scrobbler.cpp */ 
extern bool RadioScrobbling;           /*                   */

/* Function of extracting datas from Internet radio string */
bool XMP_ExtractTags_NetRadio(const char *data, char *artist, char *title, char *album)
{
     /* Scrobbling of radio streams is disabled by user */
     if(!RadioScrobbling)
        return false;
        
     string tmp(data), track;
     char char_tmp[TAG_FIELD_SIZE];
     char *char_tmp_p = &char_tmp[0];
     char *t;
     int i, j;
     DWORD find_me;
         
     /* XXX Let's think that:
      * 1) Name field appears only in NetRadio strings;
      * 2) Name field is always filled as other do.
      * Never checked both of these thougs, as i currently
      * listen only few stations which does 
      */
     if(!XMP_GetTagField(tmp, "\nname\t", album))
         return false;
     /* Array initialization */
     memset(char_tmp, 0, TAG_FIELD_SIZE);

     /* OGG streams workaround (and other that have valid separated fields) */
     if(XMP_ExtractTags_Other(data, artist, title, char_tmp)) {
         return XMP_IsRadio = true;
     }
     /* Although char_tmp willn't filled, it's better to do this safety measure */
     while(*(char_tmp_p++))
         *char_tmp_p = 0;

     /* XXX Let's presume that Track field always contains
      * track title (or artist + title) instead of anyhing else
      */   
     if(!XMP_GetTagField(tmp, "\ntrack\t", char_tmp))
         return false;
     
     track += char_tmp;

     find_me = track.find(" - ");         
     /* XXX No artist? */
     if(find_me == string::npos)
         return false;

     /* Split title and artist from Track 
      * Pure C jokes :D */
     i = track.length(); j = find_me + 3;
     t = (char *) malloc(i - j);
     t = &char_tmp[j];
     memmove(artist, char_tmp, find_me);
     memmove(title, t, i);
     free(t); 
     t = NULL;
     
     /* This is radio now! */
     return XMP_IsRadio = true;
}

bool XMP_ExtractTags_MBID(const char *data, char *mb)
{
	//string block = XMP_GetDataBlock( "ID3v2", data );
	string block(data);

	memset(mb, 0, TAG_FIELD_SIZE);

	bool status = XMP_GetTagField(block, "\nunique id\t", mb);

	// OGG / FLAC - check for "MUSICBRAINZ_TRACKID" field

	if(!status)
		status = XMP_GetTagField(block, "\nmusicbrainz_trackid\t", mb);

	// M4A - check for "MusicBrainz Track Id" field

	if(!status)
		status = XMP_GetTagField(block, "\nmusicbrainz track id\t", mb);

	if(status)
	{
		string tmp(mb);

		int sepPos = tmp.find(" ", 0); // find owner / ID separator

		if(sepPos != string::npos)
			memcpy( mb, tmp.c_str() + sepPos + 1, tmp.length() - sepPos );

		if(strlen( mb ) == 36) // we possibly have a proper MBID
		{
			return true;
		} else
		{
			memset(mb, 0, TAG_FIELD_SIZE);
			return false;
		}
	}

	return false;
}
