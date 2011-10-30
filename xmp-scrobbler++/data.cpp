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
	string lowercaseSource = lowercase(source), lowercaseName = lowercase(name);
	string pattern = "\n" + lowercaseName + ":\n";
	unsigned int start = lowercaseSource.find(pattern, 0);

	if( start == string::npos ) {
		pattern = lowercaseName + ":\n";
		if ((start = lowercaseSource.find(pattern, 0)) == string::npos)
			return "";
	}

	/* This double check is necessary to allow tags to have values like this one:
			:Night Library:
		If the pattern to tool for is block_name + ":\n" when looking for the library
		data block there would be a match inside a tag which leading to a failure.

		Although these are extremely rare... Maybe this double check could be left out. */
	
	// Update the block start:
	start += pattern.length();	
	// Find the end of the block:
	unsigned int nextStart = lowercaseSource.find("\n\n", start);
	if(nextStart++ != string::npos)
		return source.substr(start, nextStart - start);
	else
		return source.substr(start, source.length());
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

bool XMP_ExtractTags_Library(const char *data, char *artist, char *title, char *album) 
{
   string block = XMP_GetDataBlock("library", data);

   // If the artist or the title are not present this block would be discarded:
   if (!XMP_GetTagField(block, "\nartist\t", artist))
      return false;
   if (!XMP_GetTagField(block, "\ntitle\t", title))
      return false;
   // The album tag is optional:
   XMP_GetTagField(block, "\nalbum\t", album);
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

	// No "\n" in WAV LIST tags because there's no tag id before the actual tags (i.e. "ID3v2:").
	
    if(!XMP_GetTagField(tmp, "\nartist\t", artist) || !XMP_GetTagField(tmp, "\ntitle\t", title))
		if (!XMP_GetTagField(tmp, "iart\t", artist) || !XMP_GetTagField(tmp, "inam\t", title))
			return false;

    // album tag is optional
	if (!XMP_GetTagField(tmp, "\nalbum\t", album))
		XMP_GetTagField(tmp, "iprd\t", album);
	
	return true;
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

		unsigned int sepPos = tmp.find(" ", 0); // find owner / ID separator

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
