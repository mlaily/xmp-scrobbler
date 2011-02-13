--- scrobbler.cpp					Fri Mar 23 14:59:18 2007
+++ orig/libscrobbler/scrobbler.cpp	Wed Jun 23 15:51:12 2010
@@ -418,6 +418,7 @@ void Scrobbler::handleSubmit(char *data)
 		}
 	} else if (stricmp("BADAUTH",response) == 0) {
 		statusUpdate(S_SUBMIT_BADAUTH,"Submission failed: bad authorization.");
+		XMP_Log("[WARNING] Your login info is not correct, check the plugin config.\n");
 	} else {
 		char buf[256];
 		snprintf(buf, sizeof(buf), "Submission failed: %s", strtok(NULL, "\n"));
