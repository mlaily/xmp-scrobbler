--- orig/data.cpp	Sun Mar 18 22:30:52 2007
+++ data.cpp		Sat Feb 12 18:51:51 2011
@@ -25,23 +25,31 @@ string lowercase( string word )
 
 string XMP_GetDataBlock(string name, string source)
 {
-	string temp = lowercase(source);
-	name = lowercase(name) + ":\n";
+	string lowercaseSource = lowercase(source), lowercaseName = lowercase(name);
+	string pattern = "\n" + lowercaseName + ":\n";
+	unsigned int start = lowercaseSource.find(pattern, 0);
 
-	int start = temp.find(name, 0);
+	if( start == string::npos ) {
+		pattern = lowercaseName + ":\n";
+		if ((start = lowercaseSource.find(pattern, 0)) == string::npos)
+			return "";
+	}
 
-	if( start == string::npos )
-		return "";
+	/* This double check is necessary to allow tags to have values like this one:
+			:Night Library:
+		If the pattern to tool for is block_name + ":\n" when looking for the library
+		data block there would be a match inside a tag which leading to a failure.
 
-	start += name.length();
-
-	int next_start = source.find(":\n", start);
-    int block_end = source.length();
-
-	if(next_start >= 0)
-		block_end = source.find_last_of("\n", next_start);
-
-	return source.substr(start, block_end - start - 1) + '\n';
+		Although these are extremely rare... Maybe this double check could be left out. */
+	
+	// Update the block start:
+	start += pattern.length();	
+	// Find the end of the block:
+	unsigned int nextStart = lowercaseSource.find("\n\n", start);
+	if(nextStart++ != string::npos)
+		return source.substr(start, nextStart - start);
+	else
+		return source.substr(start, source.length());
 }
 
 // extracts the value of the field fn (fn\tvalue)
@@ -65,6 +73,20 @@ bool XMP_GetTagField( string data, const char *fn, cha
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
@@ -120,15 +142,15 @@ bool XMP_ExtractTags_Other(const char *data, char *art
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
@@ -156,7 +178,7 @@ bool XMP_ExtractTags_MBID(const char *data, char *mb)
 	{
 		string tmp(mb);
 
-		int sepPos = tmp.find(" ", 0); // find owner / ID separator
+		unsigned int sepPos = tmp.find(" ", 0); // find owner / ID separator
 
 		if(sepPos != string::npos)
 			memcpy( mb, tmp.c_str() + sepPos + 1, tmp.length() - sepPos );
