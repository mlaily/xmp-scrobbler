// xmp-scrobbler

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>

#include <cstdlib>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>

#include "xmpdsp.h"
#include "resource.h"
#include "xmp-scrobbler.h"
#include "data.h"

#include "libscrobbler\scrobbler.h"

#define XMPSCROBBLER_VERSION		"Pre-0.9.7"
#define MAX_LENGTH					240		// in seconds
#define DELAY_BEFORE_RESET       	5        // in seconds
#define DEFAULT_FETCH_DELAY      	10       // in seconds
#define DEFAULT_FETCH_DELAY_STR  	"10"     // in seconds

HINSTANCE ghInstance;
DWORD instID = 0;

Scrobbler *scrob = NULL;

string pathLog;
string pathCache;

DWORD xmpChans = 0;
DWORD xmpRate = 0;
DWORD secLength = 0;    // Second length is sample count

int xmpCounter = -1;    // submit counter - after it reaches 0 song is submitted
int resetCounter = -1;  // reset counter - used with CUE sheets - to have little delay after DSP_Reset()
int infoCounter = -1;    // info counter - used to have a little delay before reading info text

// CUE counter - set up as a DSP_NewTrack replacement in files with CUE sheets
// because XMPlay doesn't inform us when next track in CUE is played
// so we have to inform ourselves

int cueCounter = -1;

//bool isWinNT = false;   // TRUE if running on WinNT with Unicode

typedef struct {
	BOOL on, delayFetch, everConfigured;
	
	char delayStr[4];
	unsigned short int fetchDelay;

	char username[255];
	char password[255];

	BOOL proxy_enabled;
	BOOL proxy_auth_enabled;

	char proxy_server[255];
	char proxy_port[10];

	char proxy_user[255];
	char proxy_password[255];
	
	BOOL logfile_limit;
	int logfile_limit_size;
	BOOL logfile_truncate;
	BOOL logfile_debugmode;

} XMPScrobblerConfig;

typedef struct {
	BOOL on;

	char username[255];
	char password[255];

	BOOL proxy_enabled;
	BOOL proxy_auth_enabled;

	char proxy_server[255];
	char proxy_port[10];

	char proxy_user[255];
	char proxy_password[255];
	
	BOOL logfile_limit;
	int logfile_limit_size;
	BOOL logfile_truncate;
	BOOL logfile_debugmode;

} XMPScrobblerOldConfig;

// All played music can be represented as two-level array.
// First level is FILE. Second one - TRACK. Most FILEs have only one TRACK inside,
// but some of them has CUE sheet attached and describing which TRACKs are inside.

// XMPTrack sctructure represents one TRACK from FILE.
typedef struct {
	int start;       // Track start in seconds
//	int end;         // Track end (do we need it?)
	int length;      // Length of current track in seconds
	int last_pos;    // Where we were last time
	int played_time; // How long this track was played

	time_t playtime; // When user listened to this track
	bool tags_ok;
	bool submitted;

	char artist[TAG_FIELD_SIZE];// Artist
	char title[TAG_FIELD_SIZE]; // Song title
} XMPTrack;

// XMPFile represents current FILE with all TRACKs inside.
typedef struct {
	int track_count; // Total amount of tracks in this file
	int current_track;  // Last track played
	int length;      // Total length (do we need it?)
	
	bool dirty, resumed;

	char album[TAG_FIELD_SIZE]; // Album name
	char mb[TAG_FIELD_SIZE];    // MusicBrainz ID

	XMPTrack* tracks;// Track array
} XMPFile;

XMPFile xmpFile;

static XMPScrobblerConfig xmpcfg;

void XMP_SetSubmitTimer();
void XMP_KillSubmitTimer();

void XMP_SetInfoTimer();
void XMP_KillInfoTimer();

void XMP_SetResetTimer();
void XMP_KillResetTimer();

void XMP_SetCUETimer();
void XMP_KillCUETimer();

void XMP_SetLogPath( string lp );
void XMP_ClearLog();
bool XMP_ValidateLogSize();

void XMP_ScrobInit( const char *u, const char *p, string cache );
void XMP_ScrobTerm();

void XMP_SubmitProc();

void XMP_FetchInfo();
void print_xmp_file();
int XMP_InTrack(int sec);

bool XMP_IsCUE();
int XMP_GetPlaybackTime();
void XMP_UpdateCurrentTrackIndex();

void XMP_SetDirty();
bool XMP_IsDirty();

void XMP_ClearXMPFile();
void XMP_Welcome();
void XMP_Setup();

XMPFUNC_MISC *xmpfmisc;

extern "C" {

static void *WINAPI DSP_New();
static void WINAPI DSP_Free(void *inst);
static const char *WINAPI DSP_GetDescription(void *inst);
static void WINAPI DSP_Config(void *inst, HWND win);
static DWORD WINAPI DSP_GetConfig(void *inst, void *config);
static BOOL WINAPI DSP_SetConfig(void *inst, void *config, DWORD size);
static void WINAPI DSP_NewTrack(void *inst, const char *file);
static void WINAPI DSP_SetFormat(void *inst, const XMPFORMAT *form);
static void WINAPI DSP_Reset(void *inst);
static DWORD WINAPI DSP_Process(void *inst, float *srce, DWORD count);

}

void WINAPI DSP_About(HWND win)
{
	string msg = "xmp-scrobbler ";

	msg += XMPSCROBBLER_VERSION;
	msg += "\n\nJoin the social music revolution at Last.fm.\nIt's fun, it's free, it's all about the music.\n\nhttp://www.last.fm/";

	MessageBox( win, msg.c_str(), "xmp-scrobbler", MB_ICONINFORMATION );
}

XMPDSP dsp = {
	0, // doesn't support multiple instances
	"xmp-scrobbler",
	DSP_About,
	DSP_New,
	DSP_Free,
	DSP_GetDescription,
	DSP_Config,
	DSP_GetConfig,
	DSP_SetConfig,
	DSP_NewTrack,
	DSP_SetFormat,
	DSP_Reset,
	DSP_Process
};

// new DSP instance
void *WINAPI DSP_New()
{
	XMP_Setup();

	return (void*) 1; // no multi-instance, so just return anything not 0
}

// free DSP instance
void WINAPI DSP_Free(void *inst)
{
	if( xmpcfg.on )
	{
		XMP_ScrobTerm();
	}
	XMP_Log("[INFO] Shutting down...\n");
	XMP_Log("----\n");
}

// get description for plugin list
const char *WINAPI DSP_GetDescription(void *inst)
{
	return (const char*) dsp.name;
}

#define MESS(id,m,w,l) SendDlgItemMessage(h,id,m,(WPARAM)(w),(LPARAM)(l))

BOOL CALLBACK DSPDialogProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m) {
		case WM_COMMAND:
		{
			switch (LOWORD(w))
			{
				case IDOK:
				{
					xmpcfg.on = MESS(IDC_ENABLE, BM_GETCHECK, 0, 0);
					
				// Delay info fetch stuff:
				if (xmpcfg.delayFetch = MESS(IDC_INFO, BM_GETCHECK, 0, 0)) {
					MESS(IDC_DELAY, WM_GETTEXT, 4, xmpcfg.delayStr);
					if (xmpcfg.delayStr[0] == '\0') {
						xmpcfg.delayFetch = false;
						xmpcfg.fetchDelay = 0;
					} else {
						xmpcfg.fetchDelay = atoi(xmpcfg.delayStr);
					}
				} else {
					xmpcfg.fetchDelay = 0;
					memset(xmpcfg.delayStr, 0, 4);
				}

					char newCredentials[255] = {0};
					MESS(IDC_PASSWORD, WM_GETTEXT, 255, newCredentials);
					if (strcmp(xmpcfg.password, newCredentials)) {
						memcpy(xmpcfg.password, newCredentials, 255);
						if (scrob)
							scrob->setPassword(xmpcfg.password);
					}
					MESS(IDC_USERNAME, WM_GETTEXT, 255, newCredentials);
					if (strcmp(xmpcfg.username, newCredentials)) {
						memcpy(xmpcfg.username, newCredentials, 255);
						if (scrob)
							scrob->setUsername(xmpcfg.username);
					}
					
					MESS(IDC_PROXY_SERVER, WM_GETTEXT, 255, xmpcfg.proxy_server);
					MESS(IDC_PROXY_PORT, WM_GETTEXT, 10, xmpcfg.proxy_port);

					MESS(IDC_PROXY_USER, WM_GETTEXT, 255, xmpcfg.proxy_user);
					MESS(IDC_PROXY_PASSWORD, WM_GETTEXT, 255, xmpcfg.proxy_password);

					xmpcfg.proxy_enabled = MESS(IDC_PROXY_ENABLE, BM_GETCHECK, 0, 0);
					xmpcfg.proxy_auth_enabled = MESS(IDC_PROXY_AUTH, BM_GETCHECK, 0, 0);

					xmpcfg.logfile_limit = MESS(IDC_LOGFILE_LIMIT, BM_GETCHECK, 0, 0);
					xmpcfg.logfile_truncate = MESS(IDC_LOGFILE_TRUNCATE, BM_GETCHECK, 0, 0);
					xmpcfg.logfile_debugmode = MESS(IDC_LOGFILE_DEBUGMODE, BM_GETCHECK, 0, 0);

					xmpcfg.logfile_limit_size = MESS(IDC_LOGFILE_LIMITS, CB_GETCURSEL, 0, 0);

					xmpcfg.everConfigured = true;
					EndDialog( h, 1 );

					break;
				}

				case IDCANCEL:
				{
					EndDialog( h, 0 );
					break;
				}

				/*case IDC_ENABLE:
				{
					//xmpcfg.on = MESS(IDC_ENABLE, BM_GETCHECK, 0, 0);
					//MESS(IDC_PROXY_AUTH, WM_ENABLE, 0, 0);

					break;
				} */
				
				case IDC_INFO: {
					EnableWindow(GetDlgItem(h, IDC_DELAY), MESS(IDC_INFO, BM_GETCHECK, 0, 0));

					break;
				}

				case IDC_PROXY_ENABLE:
				{
					bool bEnabled = MESS(IDC_PROXY_ENABLE, BM_GETCHECK, 0, 0);
					bool bAuthEnabled = MESS(IDC_PROXY_AUTH, BM_GETCHECK, 0, 0);

					EnableWindow(GetDlgItem(h, IDC_PROXY_SERVER), bEnabled);
					EnableWindow(GetDlgItem(h, IDC_PROXY_PORT), bEnabled);

					EnableWindow(GetDlgItem(h, IDC_PROXY_AUTH), bEnabled);
					EnableWindow(GetDlgItem(h, IDC_PROXY_USER), bEnabled && bAuthEnabled);
					EnableWindow(GetDlgItem(h, IDC_PROXY_PASSWORD), bEnabled && bAuthEnabled);

					break;
				}
				
				case IDC_PROXY_AUTH:
				{
					bool bAuthEnabled = MESS(IDC_PROXY_AUTH, BM_GETCHECK, 0, 0);

					EnableWindow(GetDlgItem(h, IDC_PROXY_USER), bAuthEnabled);
					EnableWindow(GetDlgItem(h, IDC_PROXY_PASSWORD), bAuthEnabled);

					break;
				}

				case IDC_LOGFILE_LIMIT:
				{
					bool bEnabled = MESS(IDC_LOGFILE_LIMIT, BM_GETCHECK, 0, 0);

					EnableWindow(GetDlgItem(h, IDC_LOGFILE_LIMITS), bEnabled);

					break;
				}

				case IDC_VIEW_LOG:
				{
					ShellExecute( h, "open", pathLog.c_str(), NULL, NULL, SW_SHOW );
					break;
				}
				
				case IDC_DELETE_LOG:
				{
					XMP_ClearLog();
					break;
				}
			}

			break;
		}

		case WM_INITDIALOG:
		{
			MESS(IDC_ENABLE, BM_SETCHECK, xmpcfg.on ? 1 : 0, 0);
			MESS(IDC_PROXY_ENABLE, BM_SETCHECK, xmpcfg.proxy_enabled ? 1 : 0, 0);
			MESS(IDC_PROXY_AUTH, BM_SETCHECK, xmpcfg.proxy_auth_enabled ? 1 : 0, 0);
			
			if (xmpcfg.everConfigured) {
				MESS(IDC_INFO, BM_SETCHECK, xmpcfg.delayFetch ? 1 : 0, 0);
				EnableWindow(GetDlgItem(h, IDC_DELAY), xmpcfg.delayFetch);
            if (xmpcfg.delayFetch)
				MESS(IDC_DELAY, WM_SETTEXT, 0, xmpcfg.delayStr);
			} else {
				MESS(IDC_INFO, BM_SETCHECK, 1, 0);
				MESS(IDC_DELAY, WM_SETTEXT, 0, DEFAULT_FETCH_DELAY_STR);
			}

			MESS(IDC_USERNAME, WM_SETTEXT, 0, xmpcfg.username);
			MESS(IDC_PASSWORD, WM_SETTEXT, 0, xmpcfg.password);

			MESS(IDC_PROXY_SERVER, WM_SETTEXT, 0, xmpcfg.proxy_server);
			MESS(IDC_PROXY_PORT, WM_SETTEXT, 0, xmpcfg.proxy_port);

			MESS(IDC_PROXY_USER, WM_SETTEXT, 0, xmpcfg.proxy_user);
			MESS(IDC_PROXY_PASSWORD, WM_SETTEXT, 0, xmpcfg.proxy_password);

			EnableWindow(GetDlgItem(h, IDC_PROXY_SERVER), xmpcfg.proxy_enabled);
			EnableWindow(GetDlgItem(h, IDC_PROXY_PORT), xmpcfg.proxy_enabled);

			EnableWindow(GetDlgItem(h, IDC_PROXY_AUTH), xmpcfg.proxy_enabled);
			EnableWindow(GetDlgItem(h, IDC_PROXY_USER), xmpcfg.proxy_enabled && xmpcfg.proxy_auth_enabled);
			EnableWindow(GetDlgItem(h, IDC_PROXY_PASSWORD), xmpcfg.proxy_enabled && xmpcfg.proxy_auth_enabled);

			MESS(IDC_LOGFILE_LIMIT, BM_SETCHECK, xmpcfg.logfile_limit ? 1 : 0, 0);
			MESS(IDC_LOGFILE_TRUNCATE, BM_SETCHECK, xmpcfg.logfile_truncate ? 1 : 0, 0);
			MESS(IDC_LOGFILE_DEBUGMODE, BM_SETCHECK, xmpcfg.logfile_debugmode ? 1 : 0, 0);

			EnableWindow(GetDlgItem(h, IDC_LOGFILE_LIMITS), xmpcfg.logfile_limit);

			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "32 KiB");
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "64 KiB");
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "128 KiB");
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "256 KiB");
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "512 KiB");
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "1 MiB");
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "2 MiB");
			
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_SETCURSEL, xmpcfg.logfile_limit_size, 0);

			return 1;
		}
	}

	return 0;
}

// show config options
void WINAPI DSP_Config(void *inst, HWND win)
{
	DialogBox(ghInstance,(char*)IDD_CONFIG,win,&DSPDialogProc);
}
// get DSP config
DWORD WINAPI DSP_GetConfig(void *inst, void *config)
{
	memcpy( config, &xmpcfg, sizeof(XMPScrobblerConfig) );
	return sizeof(xmpcfg);
}
// set DSP config
BOOL WINAPI DSP_SetConfig(void *inst, void *config, DWORD size)
{
	if (size == sizeof(XMPScrobblerConfig))
		// Current config format, just copy it.
		memcpy(&xmpcfg, config, size);
	else if (size == sizeof(XMPScrobblerOldConfig)) {
		XMP_Log("[INFO] Upgrading old config data format...\n");
		// Old config format, generate the new one from it.
		xmpcfg.on = ((XMPScrobblerOldConfig *) config)->on;
		xmpcfg.delayFetch = true;
		xmpcfg.everConfigured = true;
		strcpy(xmpcfg.delayStr, DEFAULT_FETCH_DELAY_STR);
		xmpcfg.fetchDelay = DEFAULT_FETCH_DELAY;
		/* It's better to avoid crazy things... Memory padding may apply.
		memcpy(&xmpcfg.username, &((XMPScrobblerOldConfig *) config)->username, sizeof(XMPScrobblerOldConfig) - sizeof(BOOL)); */
		strcpy(xmpcfg.username, ((XMPScrobblerOldConfig *) config)->username);
		strcpy(xmpcfg.password, ((XMPScrobblerOldConfig *) config)->password);
		xmpcfg.proxy_enabled = ((XMPScrobblerOldConfig *) config)->proxy_enabled;
		xmpcfg.proxy_auth_enabled = ((XMPScrobblerOldConfig *) config)->proxy_auth_enabled;
		strcpy(xmpcfg.proxy_server, ((XMPScrobblerOldConfig *) config)->proxy_server);
		strcpy(xmpcfg.proxy_port, ((XMPScrobblerOldConfig *) config)->proxy_port);
		strcpy(xmpcfg.proxy_user, ((XMPScrobblerOldConfig *) config)->proxy_user);
		strcpy(xmpcfg.proxy_password, ((XMPScrobblerOldConfig *) config)->proxy_password);
		xmpcfg.logfile_limit = ((XMPScrobblerOldConfig *) config)->logfile_limit;
		xmpcfg.logfile_limit_size = ((XMPScrobblerOldConfig *) config)->logfile_limit_size;
		xmpcfg.logfile_truncate = ((XMPScrobblerOldConfig *) config)->logfile_truncate;
		xmpcfg.logfile_debugmode = ((XMPScrobblerOldConfig *) config)->logfile_debugmode;
   } else {
      // Either very old config format or something is corrupted, fallback config.
      XMP_Log("[WARNING] The plugin config data is corrupted, please reconfigure it.\n");
      xmpcfg.on = false;
      xmpcfg.logfile_limit = false;
      xmpcfg.logfile_truncate = false;
      xmpcfg.logfile_debugmode = false;
   }

	if(xmpcfg.logfile_truncate)
		XMP_ClearLog();

	XMP_Welcome();

	return TRUE;
}

void WINAPI DSP_Reset(void *inst)
{
	if( !xmpcfg.on ) return;

	XMP_Log( "[DEBUG] Reset request.\n" );

	if(xmpFile.track_count <= 0)
	{
		// seeking before XMP_FetchInfo - we don't even need to fetch info
		// anymore now, because this file ain't gonna get submitted

		XMP_KillInfoTimer();
		XMP_SetDirty();
	} 
	
	if (XMP_IsCUE()) {
		XMP_KillSubmitTimer();
		XMP_KillCUETimer();
		XMP_SetResetTimer();
	} else {
		XMP_SetDirty();
	}

}

DWORD WINAPI DSP_Process(void *inst, float *buffer, DWORD count)
{
	if( !xmpcfg.on ) return 0;
	
//	XMP_Log( "[DEBUG] DSP_Process -- xmpCounter = %d, xmpChans = %d, xmpRate = %d\n", xmpCounter, xmpChans, xmpRate );

	if( xmpCounter != -1 )
	{
		int sec = xmpCounter / xmpChans / xmpRate;

		xmpCounter -= count * xmpChans;

		if( sec <= 0 )
		{
			XMP_KillSubmitTimer();
			XMP_SubmitProc();

			if(XMP_IsCUE())
				XMP_SetCUETimer();
		}
	}

	if(infoCounter != -1)
	{
		int sec = infoCounter / xmpChans / xmpRate;

		infoCounter -= count * xmpChans;
		
//		XMP_Log( "[DEBUG] infoCounter %d ... %d\n", sec, infoCounter );

		if(sec <= 0)
		{
			XMP_KillInfoTimer();

/*			if(wasResetBeforeFetch)
			{
				XMP_SetDirty();
			} else
			{*/
				XMP_FetchInfo();
				XMP_UpdateCurrentTrackIndex();
				XMP_SetSubmitTimer();
//			}
		}
	}

	if(resetCounter != -1)
	{
		int sec = resetCounter / xmpChans / xmpRate;

		resetCounter -= count * xmpChans;

		if( sec <= 0 )
		{
			XMP_KillResetTimer();
			XMP_UpdateCurrentTrackIndex();

			// Allow 10 seconds for slower machines...
			if (XMP_GetPlaybackTime() < xmpFile.tracks[xmpFile.current_track].start + 10)
				XMP_SetSubmitTimer();
			else
				XMP_SetDirty();
		}
	}

	if(cueCounter != -1)
	{
		int sec = cueCounter / xmpChans / xmpRate;

		cueCounter -= count * xmpChans;

		if( sec <= 0 )
		{
			XMP_KillCUETimer();
			XMP_UpdateCurrentTrackIndex();
			XMP_SetSubmitTimer();
		}
	}

	return 0;
}

void WINAPI DSP_SetFormat(void *inst, const XMPFORMAT *form)
{
	if( !xmpcfg.on ) return;

	if(form != NULL) {
		XMP_Log("[DEBUG] Audio info: %d channels, %d Hz.\n", form->rate, form->chan);

		xmpRate = form->rate;
		xmpChans = form->chan;
	} else {
		XMP_Log("[DEBUG] Audio output stopped.\n");

		xmpRate = 0;
		xmpChans = 0;
	}

	if( xmpRate == 0 && xmpChans == 0 ) // track was stopped
	{
		if(xmpCounter != -1)
			XMP_Log("[INFO] Playback stopped.\n");

		XMP_KillSubmitTimer();
		XMP_KillInfoTimer();
		XMP_KillResetTimer();
		XMP_KillCUETimer();
		// If it was dirty it's not anymore:
		xmpFile.dirty = false;
	} else
	{
		if(infoCounter == -1)
			XMP_SetInfoTimer();
	}
}

// new track has been opened (or closed if file=NULL)
void WINAPI DSP_NewTrack(void *inst, const char *file)
{
	if( !xmpcfg.on ) return;

	XMP_ClearXMPFile();

	if( file != NULL )
	{
		XMP_Log("[DEBUG] A new track has been opened.\n");

		XMP_ScrobInit( xmpcfg.username, xmpcfg.password, pathCache );

		XMP_KillResetTimer();
		XMP_KillSubmitTimer();
		XMP_KillCUETimer();

		XMP_SetInfoTimer();
	}
	else
		XMP_Log("[DEBUG] The track has been closed.\n");
}

extern "C" __declspec( dllexport ) XMPDSP *WINAPI XMPDSP_GetInterface2(DWORD face, InterfaceProc faceproc)
{
	if (face!=XMPDSP_FACE) return NULL;
	xmpfmisc=(XMPFUNC_MISC*)faceproc(XMPFUNC_MISC_FACE); // import "misc" functions
	return &dsp;
}

extern "C" __declspec( dllexport ) BOOL APIENTRY DllMain(HINSTANCE hDLL, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_ATTACH) {
		ghInstance=hDLL;
		DisableThreadLibraryCalls(ghInstance);
	}
	return 1;
}

// ---------------------------------------------------
// ---------------------------------------------------
// ---------------------------------------------------

/*void _vsscanf(char *input, char *format, ...)
{
  va_list argList;

  va_start(argList, format);
  vsscanf(input, format, argList);
}*/

void XMP_Log( const char *s, ... )
{	
	//if(!xmpcfg.logfile_debugmode && tmp.find("[DEBUG]") != string::npos)
	if (!xmpcfg.logfile_debugmode && (s[1] == 'D'))
		return;
	
	// if the log file is too big (depends on the maximum size set in options),
	// we need to clear its contents
	
	if(xmpcfg.logfile_limit && !XMP_ValidateLogSize())
		XMP_ClearLog();

	ofstream logFile(pathLog.c_str(), ios::app);
	char buf[8000] = {0};
	char buft[20] = {0};
	SYSTEMTIME time;

	va_list argList;
	va_start( argList, s );
	
	GetLocalTime(&time);
	
	sprintf( buft, "%02d-%02d-%02d %02d:%02d:%02d\t",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond );

	vsprintf( buf, s, argList );

	logFile << buft << buf;

 	logFile.close();
}

void XMP_ClearLog()
{
	ofstream logFile(pathLog.c_str(), ios::trunc | ios::out);
	logFile << "";
 	logFile.close();
}

// checks if log size doesn't exceed maximum size set in options by the user

bool XMP_ValidateLogSize()
{
	ifstream f;

	f.open(pathLog.c_str(), ios_base::binary | ios_base::in);

	if(!f.good() || f.eof() || !f.is_open())
		return true;

	f.seekg(0, ios_base::beg);
  	ifstream::pos_type begin_pos = f.tellg();
	f.seekg(0, ios_base::end);

	int logSize = static_cast<int>(f.tellg() - begin_pos);
	
	f.close();

	int maxSize = 32 * 1024;
	int i = xmpcfg.logfile_limit_size;

	while(i-- > 0) maxSize *= 2;

	return logSize <= maxSize;
}

void XMP_SubmitProc()
{
	XMP_Log("[DEBUG] Scheduling a new submission.\n");
	
	if(XMP_IsDirty())
		return;

	TRACKDATA curTrack;
	
	memset(&curTrack, 0, sizeof(curTrack));

	memcpy( curTrack.artist, xmpFile.tracks[xmpFile.current_track].artist, TAG_FIELD_SIZE );
	memcpy( curTrack.title, xmpFile.tracks[xmpFile.current_track].title, TAG_FIELD_SIZE );
	memcpy( curTrack.album, xmpFile.album, TAG_FIELD_SIZE );
	memcpy( curTrack.mb, xmpFile.mb, TAG_FIELD_SIZE );

	curTrack.playtime = time( NULL );
	curTrack.length = xmpFile.tracks[xmpFile.current_track].length;
	curTrack.status = 0;

	scrob->addSong( curTrack );
	
	xmpFile.tracks[xmpFile.current_track].submitted = true;
}

void XMP_ScrobInit( const char *u, const char *p, string cache )
{
	if( !scrob )
	{
		XMP_Log("[DEBUG] Initializing the submission library...\n");

		scrob = new Scrobbler( u, p, "xmp", "0.1" );

		scrob->setCacheFile( cache );
		scrob->setLogFile( pathLog );

		if( xmpcfg.proxy_enabled )
		{
			string tmp;

			tmp += xmpcfg.proxy_server;
			tmp += ":";
			tmp += xmpcfg.proxy_port;

			if( xmpcfg.proxy_auth_enabled )
				scrob->setProxy( tmp.c_str(), xmpcfg.proxy_user, xmpcfg.proxy_password );
			else
				scrob->setProxy( tmp.c_str(), "", "" );
		}

    	scrob->init();
	}
}

void XMP_ScrobTerm()
{
	if( scrob )
	{
		scrob->term();
		delete scrob;
		scrob = NULL;
	}
}

// returns index of currently played track

int XMP_InTrack(int sec)
{
	int i;

	for(i = xmpFile.track_count - 1; i >= 0; i--)
		if(xmpFile.tracks[i].start <= sec)
			return i;

	return -1;
}

// gets information for the current file,
// fills xmpFile variable and parses info1 from XMPlay

void XMP_FetchInfo()
{
	XMP_Log("[DEBUG] Fetching the track data.\n");

	char *info1 = xmpfmisc->GetInfoText(XMPINFO_TEXT_MESSAGE);

//	XMP_Log( "[AUX] info1: \n\n%s\n\n", info1 );

	if(!info1)
	{
		XMP_Log("[ERROR] The call to GetInfoText(XMPINFO_TEXT_MESSAGE) failed!\n");
		return;
	}

	xmpFile.length = SendMessage( xmpfmisc->GetWindow(), WM_USER, 1, 105 );

	string block = "";
	block = XMP_GetDataBlock( "Cues", (char *)info1 );

	if(block != "")
	{
		// this file contains CUE sheet, we need to parse it

		XMP_Log("[DEBUG] CUE sheet detected, parsing started.\n");

		int cue_start = -1;
		int x, y;
		unsigned int i = 0;
		string temp;
		
		vector<int> beg; // this vector holds begin point of each line

		while((cue_start = block.find("\n", ++cue_start)) >= 0)
			if(i++ > 0)
				beg.push_back(cue_start + 1);

		beg.pop_back();

		xmpFile.track_count = beg.size();
		xmpFile.tracks = new XMPTrack[xmpFile.track_count];

		memset(xmpFile.tracks, 0, sizeof(XMPTrack) * xmpFile.track_count);

		for(i = 0; i < beg.size(); i++)
		{
			// single line here is in the format: MM:SS\tartist - title\n

			x = beg[i];//block.find("\t", beg[i]) + 1;
			y = block.find(":", x);
			temp = block.substr(x, y - x);

			xmpFile.tracks[i].start = atoi(temp.c_str()) * 60;

			x = block.find("\t", y);
			temp = block.substr(y + 1, x - y);

			xmpFile.tracks[i].start += atoi(temp.c_str());

			y = block.find(" - ", x + 1);
			temp = block.substr(x + 1, y - x - 1);

			temp.copy(xmpFile.tracks[i].artist, temp.length());

			x = block.find("\n", y + 3);
			temp = block.substr(y + 3, x - y - 3);

			temp.copy(xmpFile.tracks[i].title, temp.length());
			
			if(i > 0)
				xmpFile.tracks[i - 1].length = xmpFile.tracks[i].start -
					xmpFile.tracks[i - 1].start;
			
			xmpFile.tracks[i].tags_ok = true;
		}

		// set length for the last track

		xmpFile.tracks[xmpFile.track_count - 1].length = xmpFile.length -
			xmpFile.tracks[xmpFile.track_count - 1].start;
			
		XMPTrack tmp;
			
		XMP_ExtractTags_Other((char *) info1, tmp.artist, tmp.title, xmpFile.album);
			
		XMP_Log("[INFO] CUE sheet found, the current file contains %d track(s)\n", xmpFile.track_count);
	} else
	{
		// we are dealing with a regular file (without CUE sheet),
		// so lets try to extract some data from tags
		
		XMPTrack cue_track;
		
		memset(&cue_track, 0, sizeof(cue_track));

		cue_track.length = xmpFile.length;
//		cue_track.end = xmpFile.length;

		// Look for the info in library first:
		cue_track.tags_ok = XMP_ExtractTags_Library((char *) info1,
			cue_track.artist, cue_track.title, xmpFile.album);
			
		if (!cue_track.tags_ok)
			cue_track.tags_ok = XMP_ExtractTags_ID3v2((char *) info1,
				cue_track.artist, cue_track.title, xmpFile.album);

		if(!cue_track.tags_ok)
			cue_track.tags_ok = XMP_ExtractTags_ID3v1((char *) info1,
				cue_track.artist, cue_track.title, xmpFile.album);
				
		if(!cue_track.tags_ok)
			cue_track.tags_ok = XMP_ExtractTags_WMA((char *) info1,
				cue_track.artist, cue_track.title, xmpFile.album);

		if(!cue_track.tags_ok)
			cue_track.tags_ok = XMP_ExtractTags_Other((char *) info1,
				cue_track.artist, cue_track.title, xmpFile.album);

		if(cue_track.tags_ok)
		{
			if(XMP_ExtractTags_MBID((char *) info1, xmpFile.mb))
				XMP_Log( "[INFO] Track MusicBrainz ID: %s\n", xmpFile.mb );
			else
				XMP_Log( "[WARNING] No valid MusicBrainz ID found, consider using MusicBrainz taggers to tag this file properly!\n", xmpFile.mb );
		}

		if(cue_track.tags_ok)
		{
			if( xmpFile.tracks != NULL)
				delete [] xmpFile.tracks;
				
			xmpFile.tracks = new XMPTrack[1];
			
			memset(xmpFile.tracks, 0, sizeof(XMPTrack));
			memcpy(xmpFile.tracks, &cue_track, sizeof(XMPTrack));

			xmpFile.track_count = 1;
			xmpFile.current_track = 0;
		} else
		{
			XMP_Log("[WARNING] Current track is badly tagged - artist or title tag missing!\n");
		}
	}

	xmpfmisc->Free(info1);

	XMP_Log("[DEBUG] Track data fetch ended.\n");
//	print_xmp_file();
}

void XMP_SetSubmitTimer()
{
	XMP_UpdateCurrentTrackIndex();

	if(xmpRate == 0 || xmpChans == 0 || xmpFile.current_track == -1) return;

	int len = xmpFile.tracks[xmpFile.current_track].length;

	if( len < 30 )
	{
		XMP_Log("[INFO] The current track is too short to be submitted (must be at least 30 seconds long).\n");

		if(XMP_IsCUE())
			XMP_SetCUETimer();
		
		return;
	}

	XMP_KillSubmitTimer();

	if(XMP_IsDirty())
		return;

	int diff = XMP_GetPlaybackTime() - xmpFile.tracks[xmpFile.current_track].start;
	int secLen = (len / 2 < 240 ? len / 2 : 240) - diff;
	
	if (secLen <= 0) {
		xmpFile.resumed = true;
		XMP_SetDirty();
		return;
	}
	
	XMP_Log("[DEBUG] Track length: %d seconds, %d already passed.\n", len, diff);

	xmpCounter = secLen * xmpRate * xmpChans;

	xmpFile.tracks[xmpFile.current_track].submitted = false;

	XMP_Log( "[INFO] Current track: %s - %s (%s)\n", xmpFile.tracks[xmpFile.current_track].artist,
		xmpFile.tracks[xmpFile.current_track].title, xmpFile.album );

	XMP_Log("[DEBUG] Caching for submission in %d seconds.\n", secLen);
}

void XMP_KillSubmitTimer()
{
	xmpCounter = -1;
}

void XMP_SetInfoTimer()
{
	if( xmpRate == 0 || xmpChans == 0 ) return;

	infoCounter = xmpcfg.fetchDelay * xmpRate * xmpChans;
}

void XMP_KillInfoTimer()
{
	infoCounter = -1;
}

void XMP_SetResetTimer()
{	
	if( xmpRate == 0 || xmpChans == 0 ) return;

	resetCounter = DELAY_BEFORE_RESET * xmpRate * xmpChans;
}

void XMP_KillResetTimer()
{
	resetCounter = -1;
}

void XMP_SetCUETimer()
{
	XMP_KillCUETimer();
	XMP_UpdateCurrentTrackIndex();

	if(xmpFile.current_track + 1 < xmpFile.track_count)
	{
//		int diff = XMP_GetPlaybackTime() - xmpFile.tracks[xmpFile.current_track].start;
		int secLen = xmpFile.tracks[xmpFile.current_track + 1].start - XMP_GetPlaybackTime() + 5;
		cueCounter = secLen * xmpRate * xmpChans;
		XMP_Log("[DEBUG] Looking for the next track in the CUE sheet in %d seconds.\n", secLen);
	}
}

void XMP_KillCUETimer()
{
	cueCounter = -1;
}

bool XMP_IsCUE()
{
	return xmpFile.track_count > 1;
}

int XMP_GetPlaybackTime()
{
	return SendMessage( xmpfmisc->GetWindow(), WM_USER, 0, 105 ) / 1000;
}

void XMP_UpdateCurrentTrackIndex()
{
	xmpFile.current_track = XMP_InTrack(XMP_GetPlaybackTime());
//	XMP_Log("xmpFile.current_track = %d\n", xmpFile.current_track);
}

void XMP_SetDirty()
{
	if(XMP_IsDirty()) return;

	XMP_KillSubmitTimer();
	XMP_KillCUETimer();

	// NOTE: The current track will not be submitted, even if the seeking was to the start of it.
	
	// Allow submitting the rest of the tracks in a CUE list:
	if (XMP_IsCUE()) {
		XMP_SetCUETimer();
	} else {
		xmpFile.dirty = true;
	}

	if (xmpFile.resumed) {
		xmpFile.resumed = false;
		XMP_Log("[INFO] The current playback was already submitted in the previous XMPlay execution.\n");
		return;
   }
	
	if (xmpFile.current_track == -1 || (xmpFile.current_track >= 0 && !xmpFile.tracks[xmpFile.current_track].submitted &&
			xmpFile.tracks[xmpFile.current_track].tags_ok))
	XMP_Log("[WARNING] Seeking during playback is not allowed, the current track is not going to be submitted.\n");
	
}

bool XMP_IsDirty()
{
	return xmpFile.dirty;
}

void XMP_ClearXMPFile()
{
	if(xmpFile.tracks != NULL)
	{
		delete [] xmpFile.tracks;
		xmpFile.tracks = NULL;
	}

	memset(&xmpFile, 0, sizeof(xmpFile));

	xmpFile.dirty = false;
	xmpFile.resumed = false;
	xmpFile.current_track = -1;
}

void XMP_Welcome()
{
	DWORD dwVersion = GetVersion();

	XMP_Log("----\n");
	XMP_Log("[INFO] Hello, this is xmp-scrobbler %s.\n", XMPSCROBBLER_VERSION);
	XMP_Log("[DEBUG] System data and plugin configuration:\n\n · Operating System: %d.%d, build %d.\n · Internal libraries: %s\n"
		" · Tag fetching %s %hu seconds.\n · %s%s\n\n", LOBYTE(LOWORD(dwVersion)), HIBYTE(LOWORD(dwVersion)), HIWORD(dwVersion),
		curl_version(), xmpcfg.delayFetch? "delayed" : "not delayed,", xmpcfg.fetchDelay, xmpcfg.proxy_enabled? "Operating through "
		"proxy" : "Not operating through proxy", (xmpcfg.proxy_enabled && xmpcfg.proxy_auth_enabled)? " with authentication." : ".");
}

void XMP_Setup()
{
	char path[MAX_PATH];

	GetModuleFileName(NULL, path, MAX_PATH);

	string spath( path );

	spath.erase( spath.find_last_of( "\\" ) + 1, string::npos );

	pathLog = spath + "xmp-scrobbler.txt";
	pathCache = spath + "xmp-scrobbler.cache";
}

/*void print_xmp_file()
{
//	XMP_Log( "XMPFile\n\ntrack_count: %d\n\nlength: %d\n\n", xmpFile.track_count, xmpFile.last_track, xmpFile.length );
	for(int i=0; i<xmpFile.track_count; i++)
	{
		XMP_Log( "[XMPTrack]\n\nstart: %d\nlength: %d\nartist: %s\ntitle: %s\nalbum: %s\n", xmpFile.tracks[i].start, xmpFile.tracks[i].length, xmpFile.tracks[i].artist, xmpFile.tracks[i].title, xmpFile.album );
	}
}*/
