--- orig/cachemanager.cpp	Tue Jun 26 16:23:58 2007
+++ cachemanager.cpp		Sat Feb 12 01:32:25 2011
@@ -31,11 +31,9 @@ void CacheManager::ResizeArray( int new_size )
 	memcpy( tmp, tracks, sizeof(TRACKDATA) * cache_size );
 
 	delete [] tracks;
+	XMP_Log("[DEBUG] Resizing cache array, from %d to %d slots.\n", tracks_size, new_size);
 
 	tracks_size = new_size;
-
-	XMP_Log( "[DEBUG] Resizing cache array - new length: %d\n", tracks_size );
-
 	tracks = tmp;
 }
 
@@ -44,9 +42,8 @@ void CacheManager::AddTrack( TRACKDATA t )
 	if( cache_size + 1 > tracks_size )
 		ResizeArray( tracks_size * 2 );
 
-	XMP_Log( "[DEBUG] AddTrack (cache_size = %d, tracks_size = %d)\n", cache_size, tracks_size );
-
 	tracks[cache_size++] = t;
+	XMP_Log("[DEBUG] Adding track to cache: %d track%s in cace, %d slots.\n", cache_size, !(cache_size - 1) ? "" : "s", tracks_size);
 }
 
 void CacheManager::DeleteTracks( int n )
@@ -74,7 +71,7 @@ BOOL CacheManager::Save()
 
 BOOL CacheManager::Load()
 {
-	XMP_Log("[DEBUG] CacheManager::Load()\n");
+	XMP_Log("[DEBUG] Loading cache file.\n");
 
 	Init();
 
@@ -87,8 +84,8 @@ BOOL CacheManager::Load()
 
 	int len = static_cast<int>(f.tellg() - begin_pos);
 
-	if( tracks_size < len / sizeof(TRACKDATA) )
-		ResizeArray( len / sizeof(TRACKDATA) );
+	if(tracks_size < len / (int) sizeof(TRACKDATA))
+		ResizeArray( len / sizeof(TRACKDATA) + 10 );
 
 	f.seekg(0, ios::beg);
 
@@ -99,9 +96,10 @@ BOOL CacheManager::Load()
 		len -= sizeof(TRACKDATA);
 	}
 
-	XMP_Log( "[INFO] Number of entries in the cache: %d\n", GetCacheSize() );
+	XMP_Log("[INFO] There %s %d %s in cache.\n", !(cache_size - 1) ? "is" : "are", cache_size, !(cache_size - 1) ? "entry" : "entries");
 
 	f.close();
+	return true;
 }
 
 SUBMITPACKAGE CacheManager::GetSubmitPackage()
