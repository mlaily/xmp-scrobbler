// xmp-scrobbler

#include <windows.h>
#include <commctrl.h>

#include <vector>
#include <string>
#include <iostream>
#include <fstream>

#include "xmpdsp.h"
#include "resource.h"
#include "xmp-scrobbler.h"
#include "data.h"

/* We need thread for our radio stream watchdog */



#include "libscrobbler\scrobbler.h"

#define XMPSCROBBLER_VERSION		"0.1"
#define MAX_LENGTH					240		// in seconds
#define DELAY_BEFORE_FETCHINFO		10		// in seconds
#define DELAY_BEFORE_RESET			5		// in seconds
/* Needs for pause checking in watchdog */
#define IPC_ISPLAYING               104

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

/* Don't scrobble radio tracks by default */
bool RadioScrobbling;
/* We need another scrobbling rules for radio tracks, as we don't know
 * real time of tracks */
bool XMP_IsRadio = false;
/* Track changed */
bool PleaseDie = false;
bool XMP_RadioNewTrack_flag = false;
unsigned int XMP_RadioTrackLength = 0;
/* Don't want to lose it */

bool WatchdogCreated = false;

bool isWinNT = false;   // TRUE if running on WinNT with Unicode
bool bProcessed = false;

/* Class with struct meaning. Storing curtrack info */
class RadioStr {
private:
        string artist; 
        string title;
        string album;
public:
       RadioStr() {}
       ~RadioStr() {artist.empty(); title.empty();}
       void fill_fields(char *a, char *t, char *l) {artist.clear(); title.clear(); album.clear(); 
                                    artist+=a; title+=t; album+=l;}
       string get_artist_field() {return artist;}
       string get_title_field() {return title;}
       string get_album_field() {return album;}
};
/* Ugly, but right here */
RadioStr *RadioSt = new RadioStr();

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
	
	BOOL MZ_Not_Abusive;
    BOOL RadioScrobbling;
    BOOL SendAlbums;
    char watchdog_rate[2];
    BOOL SendChanges;

} XMPScrobblerConfig;

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
	
	bool dirty;

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

/* Radio watchdog declaration */
THREADCALL XMP_RadioWatchdog(void *args);

void print_xmp_file();
int XMP_InTrack(int sec);

bool XMP_IsCUE();
int XMP_GetPlaybackTime();
/* Watchdog needs */
bool XMP_IsPaused();
void XMP_UpdateCurrentTrackIndex();

void XMP_SetDirty();
bool XMP_IsDirty();
bool XMP_WasResetDirty();

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
	string msg = "xmp-scrobbler++ ";

	msg += XMPSCROBBLER_VERSION;
	msg += "\n\nAdded features:\n"
    "[+] Scrobbling of radio streams";
    msg += "\n\nFixed things:\n"
    "[v] Disabling of MusicBrainz tags floodage";

	MessageBox( win, msg.c_str(), "xmp-scrobbler++", MB_ICONINFORMATION );
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

	XMP_Log("[DEBUG] DSP_Free()\n");

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

					MESS(IDC_USERNAME, WM_GETTEXT, 255, xmpcfg.username);
					MESS(IDC_PASSWORD, WM_GETTEXT, 255, xmpcfg.password);

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
					
					xmpcfg.MZ_Not_Abusive = MESS(IDC_NOT_MZ_ABUSIVE, BM_GETCHECK, 0, 0);
					/* Enable scrobbling of radio streams */
					xmpcfg.RadioScrobbling = MESS(IDC_ENABLE_RADIO_SCROBBLING, BM_GETCHECK, 0, 0);
					xmpcfg.SendAlbums = MESS(IDC_SEND_ALBUMS, BM_GETCHECK, 0, 0);
					MESS(IDC_WATCHDOG_RATE, WM_GETTEXT, 3, xmpcfg.watchdog_rate);
					xmpcfg.SendChanges = MESS(IDC_SEND_CHANGES, BM_GETCHECK, 0, 0);

					EndDialog( h, 1 );

					break;
				}

				case IDCANCEL:
				{
					EndDialog( h, 0 );
					break;
				}

				case IDC_ENABLE:
				{
					//xmpcfg.on = MESS(IDC_ENABLE, BM_GETCHECK, 0, 0);
					//MESS(IDC_PROXY_AUTH, WM_ENABLE, 0, 0);

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
				
				case IDC_ENABLE_RADIO_SCROBBLING:
                {
                    bool bEnabled = MESS(IDC_ENABLE_RADIO_SCROBBLING, BM_GETCHECK, 0, 0);
                    EnableWindow(GetDlgItem(h, IDC_WATCHDOG_RATE), bEnabled);
                    /* EnableWindow(GetDlgItem(h, IDC_SEND_ALBUMS), bEnabled); */
                    
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
			
			MESS(IDC_NOT_MZ_ABUSIVE, BM_SETCHECK, xmpcfg.MZ_Not_Abusive ? 1 : 0, 0);
			MESS(IDC_ENABLE_RADIO_SCROBBLING, BM_SETCHECK, xmpcfg.RadioScrobbling ? 1 : 0, 0);
			MESS(IDC_PROXY_ENABLE, BM_SETCHECK, xmpcfg.proxy_enabled ? 1 : 0, 0);
			MESS(IDC_PROXY_AUTH, BM_SETCHECK, xmpcfg.proxy_auth_enabled ? 1 : 0, 0);
            MESS(IDC_SEND_CHANGES, BM_SETCHECK, xmpcfg.SendChanges ? 1 : 0, 0);
            MESS(IDC_SEND_ALBUMS, BM_SETCHECK, xmpcfg.SendAlbums ? 1 : 0, 0);

			MESS(IDC_USERNAME, WM_SETTEXT, 0, xmpcfg.username);
			MESS(IDC_PASSWORD, WM_SETTEXT, 0, xmpcfg.password);

			MESS(IDC_PROXY_SERVER, WM_SETTEXT, 0, xmpcfg.proxy_server);
			MESS(IDC_PROXY_PORT, WM_SETTEXT, 0, xmpcfg.proxy_port);

			MESS(IDC_PROXY_USER, WM_SETTEXT, 0, xmpcfg.proxy_user);
			MESS(IDC_PROXY_PASSWORD, WM_SETTEXT, 0, xmpcfg.proxy_password);
			
			MESS(IDC_WATCHDOG_RATE, WM_SETTEXT, 0, xmpcfg.watchdog_rate);

			EnableWindow(GetDlgItem(h, IDC_PROXY_SERVER), xmpcfg.proxy_enabled);
			EnableWindow(GetDlgItem(h, IDC_PROXY_PORT), xmpcfg.proxy_enabled);

			EnableWindow(GetDlgItem(h, IDC_PROXY_AUTH), xmpcfg.proxy_enabled);
			EnableWindow(GetDlgItem(h, IDC_PROXY_USER), xmpcfg.proxy_enabled && xmpcfg.proxy_auth_enabled);
			EnableWindow(GetDlgItem(h, IDC_PROXY_PASSWORD), xmpcfg.proxy_enabled && xmpcfg.proxy_auth_enabled);
			
			EnableWindow(GetDlgItem(h, IDC_WATCHDOG_RATE), xmpcfg.RadioScrobbling);

			MESS(IDC_LOGFILE_LIMIT, BM_SETCHECK, xmpcfg.logfile_limit ? 1 : 0, 0);
			MESS(IDC_LOGFILE_TRUNCATE, BM_SETCHECK, xmpcfg.logfile_truncate ? 1 : 0, 0);
			MESS(IDC_LOGFILE_DEBUGMODE, BM_SETCHECK, xmpcfg.logfile_debugmode ? 1 : 0, 0);

			EnableWindow(GetDlgItem(h, IDC_LOGFILE_LIMITS), xmpcfg.logfile_limit);

			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "32 KB");
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "64 KB");
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "128 KB");
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "256 KB");
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "512 KB");
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "1 MB");
			SendMessage(GetDlgItem(h, IDC_LOGFILE_LIMITS), CB_ADDSTRING, 0, (LPARAM) "2 MB");
			
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
	memcpy( &xmpcfg, config, sizeof(XMPScrobblerConfig) );

	if(xmpcfg.logfile_truncate)
		XMP_ClearLog();

	XMP_Welcome();

	return TRUE;
}

void WINAPI DSP_Reset(void *inst)
{
	if( !xmpcfg.on ) return;

	XMP_Log( "[DEBUG] DSP_Reset\n" );

	if(xmpFile.track_count <= 0)
	{
		// seeking before XMP_FetchInfo - we don't even need to fetch info
		// anymore now, because this file ain't gonna get submitted

		XMP_KillInfoTimer();
		XMP_SetDirty();
	} else
	{
		if(XMP_IsCUE())
		{
			XMP_KillCUETimer();
			XMP_KillSubmitTimer();

			XMP_SetResetTimer();
		} else
		{
			XMP_SetDirty();
		}
	}

	return;
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
			XMP_Log( "[DEBUG] DSP_Process -- sec <= 0\n" );

			XMP_KillSubmitTimer();
            if(!XMP_IsRadio)
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
				
                /* Don't need this timer for radio streams */
                if(!XMP_IsRadio) {
				   XMP_SetSubmitTimer();
                }
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

			if(XMP_WasResetDirty())
				XMP_SetDirty();
			
			XMP_SetSubmitTimer();
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
		XMP_Log( "[DEBUG] DSP_SetFormat( %d, %d, %d )\n", inst, form->rate, form->chan );

		xmpRate = form->rate;
		xmpChans = form->chan;
	} else {
		XMP_Log( "[DEBUG] DSP_SetFormat( %d, NULL )\n", inst );

		xmpRate = 0;
		xmpChans = 0;
	}

	if( xmpRate == 0 && xmpChans == 0 ) // track was stopped
	{
		XMP_Log( "[DEBUG] DSP_StopTrack\n" );
		
       /* We need re-run watchdog when user changes the radio stream */
       if(WatchdogCreated) {
          PleaseDie = true;     
          XMP_IsRadio = false;               
       }

		if(xmpCounter != -1)
			XMP_Log( "[INFO] Track stopped\n" );

		XMP_KillSubmitTimer();
		XMP_KillInfoTimer();
		XMP_KillResetTimer();
		XMP_KillCUETimer();
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
		XMP_Log( "[DEBUG] DSP_NewTrack (OPEN)\n" );

		XMP_ScrobInit( xmpcfg.username, xmpcfg.password, pathCache );

		XMP_KillResetTimer();
		XMP_KillSubmitTimer();
		XMP_KillCUETimer();

		XMP_SetInfoTimer();
	}
	else
	{
		XMP_Log( "[DEBUG] DSP_NewTrack (CLOSE)\n" );
	}
}

// the exported function that XMPlay calls to get the plugin interface
/*extern "C" __declspec( dllexport ) XMPDSP *WINAPI XMPDSP_GetInterface(DWORD face)
{
	return &dsp;
}*/

extern "C" __declspec( dllexport ) XMPDSP *WINAPI XMPDSP_GetInterface2(DWORD face, InterfaceProc faceproc)
{
	if (face!=XMPDSP_FACE) return NULL;
	xmpfmisc=(XMPFUNC_MISC*)faceproc(XMPFUNC_MISC_FACE); // import "misc" functions
	return &dsp;
}

extern "C" __declspec( dllexport ) BOOL APIENTRY DllMain(HINSTANCE hDLL, DWORD reason, LPVOID reserved)
{
	switch (reason) {
		case DLL_PROCESS_ATTACH:
			ghInstance=hDLL;
			DisableThreadLibraryCalls(ghInstance);
			break;
	}
	return 1;
}

// ---------------------------------------------------
// ---------------------------------------------------
// ---------------------------------------------------

void _vsscanf(char *input, char *format, ...)
{
  va_list argList;

  va_start(argList, format);
  vsscanf(input, format, argList);
}

void XMP_Log( const char *s, ... )
{
	string tmp(s);
	
	if(!xmpcfg.logfile_debugmode && tmp.find("[DEBUG]") != string::npos)
		return;
	
	// if the log file is too big (depends on the maximum size set in options),
	// we need to clear its contents
	
	if(xmpcfg.logfile_limit && !XMP_ValidateLogSize())
		XMP_ClearLog();

	ofstream f( pathLog.c_str(), ios::app );
	char buf[8000];
	char buft[20];
	SYSTEMTIME time;

	memset( buf, 0, 8000 );
	memset( buft, 0, 20 );

	va_list argList;
	va_start( argList, s );
	
	GetLocalTime(&time);
	
	sprintf( buft, "%02d-%02d-%02d %02d:%02d:%02d\t",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond );

	vsprintf( buf, s, argList );

	f << buft << buf;

 	f.close();
}

void XMP_ClearLog()
{
	ofstream f(pathLog.c_str(), ios::trunc | ios::out);

	f << "";

 	f.close();
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
	XMP_Log( "[DEBUG] XMP_SubmitProc start\n" );
	
	if(XMP_IsDirty()) {
		return;
    }

	TRACKDATA curTrack;
	
	memset(&curTrack, 0, sizeof(curTrack));

	if(XMP_IsRadio) {
        memmove( curTrack.artist, RadioSt->get_artist_field().c_str(),
            RadioSt->get_artist_field().length());
        memmove( curTrack.title, RadioSt->get_title_field().c_str(),
            RadioSt->get_title_field().length());
        if(xmpcfg.SendAlbums) 
            memmove( curTrack.album, RadioSt->get_album_field().c_str(), RadioSt->get_album_field().length());
        /* Store track length recieved from Watchdog */
        curTrack.length = XMP_RadioTrackLength;
    } else {
        curTrack.length = xmpFile.tracks[xmpFile.current_track].length;        
	    memmove( curTrack.artist, xmpFile.tracks[xmpFile.current_track].artist, TAG_FIELD_SIZE );
	    memmove( curTrack.title, xmpFile.tracks[xmpFile.current_track].title, TAG_FIELD_SIZE );
	    memmove( curTrack.album, xmpFile.album, TAG_FIELD_SIZE );
	    memmove( curTrack.mb, xmpFile.mb, TAG_FIELD_SIZE );
    }    
   	curTrack.playtime = time( NULL );
	curTrack.status = 0;

	scrob->addSong( curTrack );
	
	/* XXX: Or that could crash xmp */
	/* if(!xmpcfg.SendChanges)
          xmpFile.tracks[xmpFile.current_track].submitted = true; */
}

void XMP_ScrobInit( const char *u, const char *p, string cache )
{
	if( !scrob )
	{
		XMP_Log("[DEBUG] XMP_ScrobInit() started\n");
        /* Taken from AmiScrobbler 0.0.2a 
         * My apologies to Creator of xmp-scrobbler ;) */
		scrob = new Scrobbler( u, p, "xmp", "1.1" );

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
    	
    	XMP_Log("[DEBUG] XMP_ScrobInit() finished\n");
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

unsigned int delay;

void XMP_FetchInfo()
{
	XMP_Log( "[DEBUG] XMP_FetchInfo -- start\n" );

	DWORD size = 0;

	char *info1 = xmpfmisc->GetInfoText(XMPINFO_TEXT_MESSAGE);

//	XMP_Log( "[AUX] info1: \n\n%s\n\n", info1 );

	if(!info1)
	{
		XMP_Log( "[DEBUG] XMP_FetchInfo -- GetInfoText failed!\n" );
		return;
	}

	xmpFile.length = SendMessage( xmpfmisc->GetWindow(), WM_USER, 1, 105 );

	string block = "";
	block = XMP_GetDataBlock( "Cues", (char *)info1 );

	if(block != "")
	{
		// this file contains CUE sheet, we need to parse it
		XMP_Log( "[DEBUG] XMP_FetchInfo() - CUE sheet parsing started\n" );

		int cue_start = -1;
		int cue_end;
		int i, x, y;
		string temp;
		
		vector<int> beg; // this vector holds begin point of each line

//		XMP_Log("block: %s\n", block.c_str());		

		i = 0;
		
		while((cue_start = block.find("\n", cue_start + 1)) >= 0)
			if(i++ > 0){
				beg.push_back(cue_start + 1);
			//	XMP_Log("c: %d\n",cue_start);
			}

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
			
		XMP_Log( "[INFO] CUE sheet found, current file contains %d track(s)\n", xmpFile.track_count );
	} else
	{
		// we are dealing with a regular file (without CUE sheet),
		// so lets try to extract some data from tags
		
		XMPTrack cue_track;
		
		memset(&cue_track, 0, sizeof(cue_track));

		cue_track.length = xmpFile.length;
//		cue_track.end = xmpFile.length;

        RadioScrobbling = xmpcfg.RadioScrobbling;
        /* Not radio scrobbling by default */
        XMP_IsRadio = false;

        /* Check for network radio scrobbling. Treck length is unknown */
		if(!cue_track.tags_ok && SendMessage( xmpfmisc->GetWindow(), WM_USER, 1, 105 ) == 0)
            cue_track.tags_ok = XMP_ExtractTags_NetRadio((char *) info1,
                cue_track.artist, cue_track.title, xmpFile.album);       
        /* Normal tracks go here */        
        if(!XMP_IsRadio && SendMessage( xmpfmisc->GetWindow(), WM_USER, 1, 105 ) > 0) {
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
        }
        else {
            /* Watchdog refresh rate*/
            xmpcfg.watchdog_rate[2] = '\0';
            delay = atoi(xmpcfg.watchdog_rate);
            if(!delay || delay < 9) 
                delay = 9;
            else if(delay > 60)
                delay = 60;
            
           /* Create one instance of watchdog thread */
           if(!WatchdogCreated)
              CreateThread(NULL, 0, XMP_RadioWatchdog, NULL, 0, NULL);
        }

        /* Musicbrainz in streams? :D */
		if(cue_track.tags_ok && !XMP_IsRadio)
		{
			if(XMP_ExtractTags_MBID((char *) info1, xmpFile.mb))
				XMP_Log( "[INFO] Track MusicBrainz ID: %s\n", xmpFile.mb );
			else if(!xmpcfg.MZ_Not_Abusive)
				XMP_Log( "[WARNING] No valid MusicBrainz ID found, consider using MusicBrainz taggers to tag this file properly!\n" );
		}
		else if(XMP_IsRadio) {
             string artist, title; artist += cue_track.artist; title += cue_track.title;
             if(!artist.empty() && !title.empty()) {
                XMP_Log("[DEBUG] Recieved struct:\n%s", info1);
		        XMP_Log("[INFO] Listening now to radio stream: %s - %s (%s)\n", cue_track.artist, 
                   cue_track.title, xmpFile.album);
            }
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

	XMP_Log( "[DEBUG] XMP_FetchInfo -- end\n" );
//	print_xmp_file();
}

/* Radio stream watchdog */
THREADCALL XMP_RadioWatchdog(void *vptr_args)
{    
     XMP_Log( "[DEBUG] XMP_RadioWatchdog -- start\n" );
     
     bool flag, StopLogged;
     /* Only one instance */
     WatchdogCreated = true;
     flag = StopLogged = false;
     time_t track_start_time;
     unsigned int consumpted = 0;
     char char_tmp[TAG_FIELD_SIZE], char_tmp2[TAG_FIELD_SIZE], char_tmp3[TAG_FIELD_SIZE];
     char *info1;
     
     /* Let's start count track length */
     time(&track_start_time); 
/* O to the M to the G */
PlaybackResumpted: 
     /* That's much better than killing thread from outside */
     while(XMP_IsRadio && !PleaseDie && !XMP_IsPaused() ) {
        /* WinAPI timer */
        Sleep(delay * 1000);     
        
        if(! (info1 = xmpfmisc->GetInfoText(XMPINFO_TEXT_MESSAGE)) ) {
             XMP_Log( "[INFO] Radio stream stopped\n" );
             continue;
        }
        string tmp(info1);

        /* Initialisation */
        memset(char_tmp, 0, TAG_FIELD_SIZE);
        memset(char_tmp2, 0, TAG_FIELD_SIZE);
        memset(char_tmp3, 0, TAG_FIELD_SIZE);

        /* Can't get info. Possibly problems or stop playing? */
        if( !XMP_ExtractTags_NetRadio((char *) info1, char_tmp, char_tmp2, char_tmp3) ) {
            if(!StopLogged)
               XMP_Log( "[DEBUG] Didn't find fields in the radio stream\n" );
            StopLogged = true;
            continue;
        }
        else
            StopLogged = false;
        /* Store title and artist for checking for updates */
        if(!flag)
            RadioSt->fill_fields(char_tmp, char_tmp2, char_tmp3);
        flag = true;
        string artist(char_tmp), title(char_tmp2);
        /* New track appeared */
          if(RadioSt->get_artist_field() != artist ||
            RadioSt->get_title_field() != title) {
                    if(!PleaseDie || xmpcfg.SendChanges) {
                        XMP_RadioNewTrack_flag = true;
                        /* Let's store track length */
                        XMP_RadioTrackLength = (unsigned int) difftime(time(NULL), track_start_time) - consumpted;
                        consumpted = 0;
                        /* Re-set start time */
                        time(&track_start_time);
                    }  
        }
        /* New track detected, let's submit it */
        if(XMP_RadioNewTrack_flag) {
            XMP_RadioNewTrack_flag = flag = false;

            /* Actually, last.fm bans short tracks, but anyway, let's 
             * don't break the rules */
             if(XMP_RadioTrackLength >= 30) {
                XMP_Log( "[INFO] Radio track for submission: %s - %s (%s) "
                    "of approximately %ds length\n", 
                        RadioSt->get_artist_field().c_str(),
                        RadioSt->get_title_field().c_str(), 
                        RadioSt->get_album_field().c_str(),
                        XMP_RadioTrackLength );
                /* Start submit proc */
                XMP_SubmitProc();
            } else
                XMP_Log("[INFO] Radio track %s - %s (%s) of approximately %ds length "
                        "willn't submit due of it's shortness\n",
                        RadioSt->get_artist_field().c_str(),
                        RadioSt->get_title_field().c_str(), 
                        RadioSt->get_album_field().c_str(),
                        XMP_RadioTrackLength );
        }
     }
     /* Just trick to prevent XMPlay from crashing.
      * Pauses are baneful */
     if(XMP_IsPaused() && XMP_IsRadio && !PleaseDie) {
        time_t start_time = time(NULL);
        /* Eating cycles, but nothing better else :( */
        while(XMP_IsPaused()) 
            ;
        /* Huh? Hindu code? Just rephrasing lmao */
        if(XMP_IsRadio && !PleaseDie) {
            /* We should substract idling time */
            consumpted = (unsigned int) difftime(time(NULL), start_time);
            goto PlaybackResumpted;
        }
    }
     
     XMP_Log( "[DEBUG] XMP_RadioWatchdog -- end\n" );
     
     /* Not sure that only once, we'll better re-run it when we'll need it again */
     WatchdogCreated = PleaseDie = false;
     return 1;
}

void XMP_SetSubmitTimer()
{
//	XMP_Log( "[DEBUG] XMP_SetSubmitTimer()\n" );

	XMP_UpdateCurrentTrackIndex();

	if(xmpRate == 0 || xmpChans == 0 || xmpFile.current_track == -1) return;

	int len = xmpFile.tracks[xmpFile.current_track].length;

	if( len < 30 )
	{
	    XMP_Log( "[INFO] Track is too short to be submitted (must be at least 30 seconds long)\n" );

		if(XMP_IsCUE())
			XMP_SetCUETimer();
		
	    return;
	}

	XMP_KillSubmitTimer();

	if(XMP_IsDirty())
		return;

	int diff = XMP_GetPlaybackTime() - xmpFile.tracks[xmpFile.current_track].start;
	
	DWORD secLen = (len / 2 < 240 ? len / 2 : 240) - diff;
	
	XMP_Log("[DEBUG] len = %d, diff = %d\n", (len / 2 < 240 ? len / 2 : 240), diff);

	xmpCounter = secLen * xmpRate * xmpChans;

	bProcessed = false;
	xmpFile.tracks[xmpFile.current_track].submitted = false;

	XMP_Log( "[INFO] Current track: %s - %s (%s)\n", xmpFile.tracks[xmpFile.current_track].artist,
		xmpFile.tracks[xmpFile.current_track].title, xmpFile.album );

	XMP_Log( "[INFO] Submitting in %d seconds...\n", secLen );
}

void XMP_KillSubmitTimer()
{
//	XMP_Log( "[DEBUG] XMP_KillSubmitTimer\n" );

	xmpCounter = -1;
}

void XMP_SetInfoTimer()
{
//	XMP_Log( "[DEBUG] XMP_SetInfoTimer()\n" );

	if( xmpRate == 0 || xmpChans == 0 ) return;

	XMP_KillInfoTimer();

	infoCounter = DELAY_BEFORE_FETCHINFO * xmpRate * xmpChans;
}

void XMP_KillInfoTimer()
{
//	XMP_Log( "[DEBUG] XMP_KillInfoTimer\n" );

	infoCounter = -1;
}

void XMP_SetResetTimer()
{
//	XMP_Log( "[DEBUG] XMP_SetResetTimer()\n" );
	
	if( xmpRate == 0 || xmpChans == 0 ) return;

	XMP_KillResetTimer();

	resetCounter = DELAY_BEFORE_RESET * xmpRate * xmpChans;
}

void XMP_KillResetTimer()
{
//	XMP_Log( "[DEBUG] XMP_KillResetTimer\n" );

	resetCounter = -1;
}

void XMP_SetCUETimer()
{
	XMP_KillCUETimer();
	
	XMP_UpdateCurrentTrackIndex();

	// we don't need to set this timer for the last track in the file
	
	if(xmpFile.current_track + 1 < xmpFile.track_count)
	{
//		int diff = XMP_GetPlaybackTime() - xmpFile.tracks[xmpFile.current_track].start;
		int secLen = xmpFile.tracks[xmpFile.current_track].length / 2 + 5;

		cueCounter = secLen * xmpRate * xmpChans;

		XMP_Log("[DEBUG] XMP_SetCUETimer(%d)\n", secLen);
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

bool XMP_IsPaused()
{
    /* Ian says use IPC_ISPLAYING to check is XMPlay playing something now
     * If pause detected then go for sleep */
    return ( SendMessage( xmpfmisc->GetWindow(), WM_USER, 0, IPC_ISPLAYING ) == 3 );
}

void XMP_UpdateCurrentTrackIndex()
{
	xmpFile.current_track = XMP_InTrack(XMP_GetPlaybackTime());
//	XMP_Log("xmpFile.current_track = %d\n", xmpFile.current_track);
}

void XMP_SetDirty()
{
	if(XMP_IsDirty()) return;

	xmpFile.dirty = true;

	XMP_KillSubmitTimer();
	XMP_KillCUETimer();

	if(xmpFile.current_track == -1 ||
		(xmpFile.current_track >= 0 && !xmpFile.tracks[xmpFile.current_track].submitted &&
		xmpFile.tracks[xmpFile.current_track].tags_ok))
		XMP_Log("[WARNING] Seeking during playback is not allowed - current file is not going to be submitted!\n");
}

bool XMP_IsDirty()
{
	return xmpFile.dirty;
}

bool XMP_WasResetDirty()
{
	return XMP_GetPlaybackTime() - xmpFile.tracks[xmpFile.current_track].start > 5;
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
	xmpFile.current_track = -1;
}

void XMP_Welcome()
{
	DWORD dwVersion = GetVersion();

	DWORD dwMajor = (DWORD)(LOBYTE(LOWORD(dwVersion)));
	DWORD dwMinor = (DWORD)(HIBYTE(LOWORD(dwVersion)));
	DWORD dwBuild = 0; // Windows Me/98/95

//	if(dwVersion < 0x80000000)              
		dwBuild = (DWORD)(HIWORD(dwVersion));

	XMP_Log("----\n");
	XMP_Log("[INFO] Hello, this is xmp-scrobbler++ %s\n", XMPSCROBBLER_VERSION);
	XMP_Log("[DEBUG] GetVersion() = %d (%d.%d, build %d)\n", GetVersion(), dwMajor, dwMinor, dwBuild);
	XMP_Log("[DEBUG] curl_version() = %s\n", curl_version());
}

void XMP_Setup()
{
	char path[255];

	GetModuleFileName( NULL, path, 255 );

	string spath( path );

	spath.erase( spath.find_last_of( "\\" ) + 1, string::npos );

	pathLog = spath + "xmp-scrobbler.txt";
	pathCache = spath + "xmp-scrobbler.cache";
}

void print_xmp_file()
{
//	XMP_Log( "XMPFile\n\ntrack_count: %d\n\nlength: %d\n\n", xmpFile.track_count, xmpFile.last_track, xmpFile.length );
	for(int i=0; i<xmpFile.track_count; i++)
	{
		XMP_Log( "[XMPTrack]\n\nstart: %d\nlength: %d\nartist: %s\ntitle: %s\nalbum: %s\n", xmpFile.tracks[i].start, xmpFile.tracks[i].length, xmpFile.tracks[i].artist, xmpFile.tracks[i].title, xmpFile.album );
	}
}
