--- cachemanager.cpp		Tue Jun 26 15:23:58 2007
+++ orig/cachemanager.cpp	Wed Jun 23 10:57:48 2010
@@ -88,7 +88,7 @@ BOOL CacheManager::Load()
 	int len = static_cast<int>(f.tellg() - begin_pos);
 
 	if( tracks_size < len / sizeof(TRACKDATA) )
-		ResizeArray( len / sizeof(TRACKDATA) );
+		ResizeArray( len / sizeof(TRACKDATA) + 10 );
 
 	f.seekg(0, ios::beg);
 
