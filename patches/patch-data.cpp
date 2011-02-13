--- data.cpp		Wed Jun 23 23:59:24 2010
+++ orig/data.cpp	Thu Jun 24 00:03:22 2010
@@ -25,22 +25,39 @@ string lowercase( string word )
 
 string XMP_GetDataBlock(string name, string source)
 {
-	string temp = lowercase(source);
-	name = lowercase(name) + ":\n";
+	string temp = lowercase(source), lowercase_name = lowercase(name);
+	// This is the pattern to look for:
+	string full_name = "\n" + lowercase_name + ":\n";
 
-	int start = temp.find(name, 0);
+	int start = temp.find(full_name, 0);
 
-	if( start == string::npos )
-		return "";
+	if( start == string::npos ) {
+		/* Now, there is the posibility that the block we're looking for is the
+		   first one, so there won't be any new line character in the beginning: */
+		full_name = lowercase_name + ":\n";
+		if ((start = temp.find(full_name, 0)) == string::npos)
+			// It's not even the first block.
+			return "";
+	}
 
-	start += name.length();
+	/* This double check was necessary to allow tags to have values like this one:
+			:Night Library:
+		If the pattern to search is block_name + ":\n" when looking for the library
+		data block there would be a match inside a tag which is not the requested
+		data block.
 
-	int next_start = source.find(":\n", start);
-    int block_end = source.length();
+		I know these are strange titles or tags, but could happen. */
 
+	start += full_name.length();	
+	
+	// This part could suffer from the same thing discribed above... Looking for "\n\n" instead.
+	int next_start = source.find("\n\n", start);
+	int block_end = source.length();
+	
 	if(next_start >= 0)
 		block_end = source.find_last_of("\n", next_start);
 
+	// The last LF may not be needed...
 	return source.substr(start, block_end - start - 1) + '\n';
 }
 
@@ -65,6 +82,20 @@ bool XMP_GetTagField( string data, const char *fn, cha
 	return true;
 }
 
+bool XMP_ExtractTags_Library(const char *data, char *artist, char *title, char *album) 
+{
+   string block = XMP_GetDataBlock("library", data);
+
+   // If the artist or the title are not present this block would be discarded:
+   if (!XMP_GetTagField(block, "\nartist\t", artist))
+      return false;
+   if (!XMP_GetTagField(block, "\ntitle\t", title))
+      return false;
+   // The album tag is optional:
+   XMP_GetTagField(block, "\nalbum\t", album);
+   return true;
+}
+
 bool XMP_ExtractTags_ID3v1(const char *data, char *artist, char *title, char *album)
 {
 	string block = XMP_GetDataBlock( "ID3v1", data );
@@ -120,15 +151,15 @@ bool XMP_ExtractTags_Other(const char *data, char *art
 {
 	string tmp(data);
 
-	if(!XMP_GetTagField(tmp, "\nartist\t", artist))
-        return false;
+	// No "\n" in WAV LIST tags because there's no tag id before the actual tags (i.e. "ID3v2:").
+	
+    if(!XMP_GetTagField(tmp, "\nartist\t", artist) || !XMP_GetTagField(tmp, "\ntitle\t", title))
+		if (!XMP_GetTagField(tmp, "iart\t", artist) || !XMP_GetTagField(tmp, "inam\t", title))
+			return false;
 
-    if(!XMP_GetTagField(tmp, "\ntitle\t", title))
-        return false;
-
     // album tag is optional
-
-	XMP_GetTagField(tmp, "\nalbum\t", album);
+	if (!XMP_GetTagField(tmp, "\nalbum\t", album))
+		XMP_GetTagField(tmp, "iprd\t", album);
 	
 	return true;
 }
