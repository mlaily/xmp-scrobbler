--- data.h		Wed Feb 28 17:21:58 2007
+++ orig/data.h	Wed Jun 23 10:49:40 2010
@@ -8,6 +8,7 @@ using namespace std;
 bool XMP_GetTagField( string data, const char *fn, char *buf );
 string XMP_GetDataBlock(string name, string source);
 
+bool XMP_ExtractTags_Library(const char *data, char *artist, char *title, char *album);
 bool XMP_ExtractTags_ID3v1(const char *data, char *artist, char *title, char *album);
 bool XMP_ExtractTags_ID3v2(const char *data, char *artist, char *title, char *album);
 
