/*
===========================================================================
Copyright (C) 2006 Tony J. White (tjw@tjw.org)

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/*
 * libneon HTTP download backend for Quake3e.
 *
 * Threading note
 * ==============
 * libneon is synchronous/blocking.  Each download is run in a POSIX
 * background thread so that the main game loop is never stalled.  The
 * main thread polls a volatile status flag each frame and processes
 * completion (file rename, FS_Reload, callbacks) on the game thread
 * where Q3 file-system functions are safe to call.
 *
 * File I/O note
 * =============
 * The background thread writes downloaded data to the temp-file path
 * using standard POSIX fopen/fwrite.  Q3 FS functions (FS_SV_Rename,
 * FS_Reload, etc.) are only called from the main thread.
 */

#ifdef USE_NEON

#include "client.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "ne_session.h"
#include "ne_request.h"
#include "ne_uri.h"
#include "ne_utils.h"
#include "ne_string.h"

/* -----------------------------------------------------------------------
 * Internal per-download state
 * --------------------------------------------------------------------- */

#define NEON_MAX_URL    (MAX_OSPATH * 4)
#define NEON_MAX_REDIRS 5

typedef struct neon_dl_s {
	/* Thread handle */
	pthread_t       thread;
	pthread_mutex_t mutex;
	int             threadStarted;

	/* Read by background thread, set only before thread creation */
	char            url[NEON_MAX_URL];
	char            tempFilePath[NEON_MAX_URL]; /* full OS path */
	char            name[MAX_OSPATH];           /* for display / error msgs */
	qboolean        headerCheck;                /* look for Content-Disposition */
	qboolean        verbose;

	/* Written by background thread, read by main thread.
	 * volatile avoids stale cached reads; formal lock is held for
	 * the progress string update only. */
	volatile int    complete;   /* 0 = running, 1 = success, -1 = error */
	volatile int    abort;      /* main sets 1 to request cancellation   */
	volatile long   totalBytes;
	volatile long   receivedBytes;

	/* Set by background thread on completion */
	int             httpCode;
	char            errorMsg[512];
	char            foundName[MAX_OSPATH]; /* from Content-Disposition */
	qboolean        firstChunk;
} neon_dl_t;

/* -----------------------------------------------------------------------
 * Utility helpers (pure – safe to call from background thread)
 * --------------------------------------------------------------------- */

/*
 * stristr – case-insensitive sub-string search.
 */
static const char *stristr( const char *source, const char *target )
{
	const char *p0, *p1, *p2, *pn;
	char c1, c2;

	if ( *target == '\0' )
		return source;

	pn = source;
	p1 = source;
	p2 = target;

	while ( *++p2 )
		pn++;

	while ( *pn != '\0' ) {
		p0 = p1;
		p2 = target;
		while ( ( c1 = *p1 ) != '\0' && ( c2 = *p2 ) != '\0' ) {
			if ( c1 <= 'Z' && c1 >= 'A' ) c1 += ( 'a' - 'A' );
			if ( c2 <= 'Z' && c2 >= 'A' ) c2 += ( 'a' - 'A' );
			if ( c1 != c2 ) break;
			p1++;
			p2++;
		}
		if ( *p2 == '\0' )
			return p0;
		p1 = p0 + 1;
		pn++;
	}
	return NULL;
}

static int replace1( const char src, const char dst, char *str )
{
	int count = 0;
	if ( !str ) return 0;
	while ( *str != '\0' ) {
		if ( *str == src ) { *str = dst; count++; }
		str++;
	}
	return count;
}

static const char *sizeToString( long size )
{
	static char buf[32];
	if ( size < 1024 )
		sprintf( buf, "%ldB", size );
	else if ( size < 1024 * 1024 )
		sprintf( buf, "%ldKB", size / 1024 );
	else
		sprintf( buf, "%ld.%ldMB", size / ( 1024 * 1024 ),
		         ( size / ( 1024 * 1024 / 10 ) ) % 10 );
	return buf;
}

/*
 * Simple percent-encoding of a URL path component.
 * Only RFC-3986 unreserved characters are left unescaped.
 * Caller must free() the returned string.
 */
static char *url_encode( const char *input )
{
	static const char hex[] = "0123456789ABCDEF";
	size_t len = strlen( input );
	char *out = (char *)malloc( len * 3 + 1 );
	const unsigned char *p = (const unsigned char *)input;
	char *d;

	if ( !out ) return NULL;
	d = out;

	while ( *p ) {
		unsigned char c = *p++;
		if ( ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) ||
		     ( c >= '0' && c <= '9' ) ||
		     c == '-' || c == '_' || c == '.' || c == '~' ) {
			*d++ = (char)c;
		} else {
			*d++ = '%';
			*d++ = hex[c >> 4];
			*d++ = hex[c & 0x0f];
		}
	}
	*d = '\0';
	return out;
}

/* -----------------------------------------------------------------------
 * Neon response-header helpers (called from background thread)
 * --------------------------------------------------------------------- */

/*
 * Parse a Content-Disposition: attachment; filename="foo.pk3" header.
 * On success the pak name (without extension) is written to state->foundName.
 */
static void neon_content_disp_handler( void *ud, const char *value )
{
	neon_dl_t *state = (neon_dl_t *)ud;
	char vcopy[512];
	const char *s;
	char name[MAX_OSPATH];
	char *d;
	char quote;
	int len;

	strncpy( vcopy, value, sizeof( vcopy ) - 1 );
	vcopy[sizeof( vcopy ) - 1] = '\0';
	replace1( '\r', '\0', vcopy );
	replace1( '\n', '\0', vcopy );

	s = stristr( vcopy, "filename=" );
	if ( !s ) return;
	s += 9; /* strlen("filename=") */

	quote = ( *s == '\'' || *s == '"' ) ? *s++ : '\0';

	d = name;
	while ( *s != '\0' && *s != quote && (size_t)( d - name ) < sizeof( name ) - 1 )
		*d++ = *s++;
	len = (int)( d - name );
	*d = '\0';

	/* Validate: must end in .pk3, no path separators */
	if ( len < 5 ) return;
	if ( !stristr( name + len - 4, ".pk3" ) ) return;
	{
		int i;
		for ( i = 0; i < len; i++ ) {
			char c = name[i];
			if ( c == '/' || c == '\\' || c == ':' ) return;
			if ( c < ' ' || (unsigned char)c > '~' ) return;
		}
	}

	/* Strip .pk3 extension */
	{
		int n = len - 4;
		if ( name[n] == '.' ) name[n] = '\0';
	}

	strncpy( state->foundName, name, sizeof( state->foundName ) - 1 );
	state->foundName[sizeof( state->foundName ) - 1] = '\0';
}

/* -----------------------------------------------------------------------
 * Neon body reader (called from background thread for each chunk)
 * --------------------------------------------------------------------- */

typedef struct {
	neon_dl_t *state;
	FILE      *fp;
} body_ctx_t;

static int neon_body_reader( void *ud, const char *buf, size_t len )
{
	body_ctx_t *ctx   = (body_ctx_t *)ud;
	neon_dl_t  *state = ctx->state;

	if ( state->abort )
		return -1;

	/* Validate pak signature on the first chunk */
	if ( state->firstChunk ) {
		state->firstChunk = qfalse;
		if ( !CL_ValidPakSignature( (const byte *)buf, (int)len ) ) {
			strncpy( state->errorMsg, "Invalid pak signature",
			         sizeof( state->errorMsg ) - 1 );
			state->complete = -1;
			return -1;
		}
		/* Now safe to open the output file */
		ctx->fp = fopen( state->tempFilePath, "wb" );
		if ( !ctx->fp ) {
			strncpy( state->errorMsg, "Failed to open temp file for writing",
			         sizeof( state->errorMsg ) - 1 );
			state->complete = -1;
			return -1;
		}
		/* Large write buffer: batch fwrite() calls into fewer syscalls. */
		setvbuf( ctx->fp, NULL, _IOFBF, 256 * 1024 );
	}

	if ( fwrite( buf, 1, len, ctx->fp ) != len ) {
		strncpy( state->errorMsg, "Write error",
		         sizeof( state->errorMsg ) - 1 );
		state->complete = -1;
		return -1;
	}

	state->receivedBytes += (long)len;

	return 0;
}

/* -----------------------------------------------------------------------
 * Background download thread
 * --------------------------------------------------------------------- */

static void *neon_download_thread( void *arg )
{
	neon_dl_t *state = (neon_dl_t *)arg;
	char       currentUrl[NEON_MAX_URL];
	int        redirects;

	strncpy( currentUrl, state->url, NEON_MAX_URL - 1 );
	currentUrl[NEON_MAX_URL - 1] = '\0';

	for ( redirects = 0; redirects <= NEON_MAX_REDIRS; redirects++ ) {
		ne_uri        uri;
		ne_session   *sess;
		ne_request   *req;
		body_ctx_t    bctx;
		char          locationBuf[NEON_MAX_URL];
		char          pathbuf[NEON_MAX_URL];
		const char   *upath;
		int           port;
		int           result;
		int           code;
		const ne_status *status;

		if ( state->abort ) {
			strncpy( state->errorMsg, "Aborted", sizeof( state->errorMsg ) - 1 );
			state->complete = -1;
			return NULL;
		}

		memset( &uri, 0, sizeof( uri ) );
		if ( ne_uri_parse( currentUrl, &uri ) != 0 || !uri.host ) {
			snprintf( state->errorMsg, sizeof( state->errorMsg ),
			          "Failed to parse URL: %.256s", currentUrl );
			state->complete = -1;
			ne_uri_free( &uri );
			return NULL;
		}

		if ( !uri.scheme ||
		     ( strcmp( uri.scheme, "http" ) != 0 &&
		       strcmp( uri.scheme, "https" ) != 0 ) ) {
			snprintf( state->errorMsg, sizeof( state->errorMsg ),
			          "Unsupported URL scheme (only http/https allowed): %s",
			          uri.scheme ? uri.scheme : "(null)" );
			state->complete = -1;
			ne_uri_free( &uri );
			return NULL;
		}

		port = uri.port ? uri.port : ( strcmp( uri.scheme, "https" ) == 0 ? 443 : 80 );
		sess = ne_session_create( uri.scheme, uri.host, port );

		ne_set_session_flag( sess, NE_SESSFLAG_PERSIST, 1 );
		ne_set_connect_timeout( sess, 10 );
		ne_set_read_timeout( sess, 30 );
		ne_set_useragent( sess, Q3_VERSION );

		if ( strcmp( uri.scheme, "https" ) == 0 ) {
			if ( !ne_has_support( NE_FEATURE_SSL ) ) {
				strncpy( state->errorMsg,
				         "HTTPS not supported: neon was built without SSL",
				         sizeof( state->errorMsg ) - 1 );
				state->complete = -1;
				ne_session_destroy( sess );
				ne_uri_free( &uri );
				return NULL;
			}
			ne_ssl_trust_default_ca( sess );
		}

		/* Build request path (with optional query string) */
		upath = ( uri.path && uri.path[0] ) ? uri.path : "/";
		if ( uri.query && uri.query[0] )
			snprintf( pathbuf, sizeof( pathbuf ), "%s?%s", upath, uri.query );
		else {
			strncpy( pathbuf, upath, sizeof( pathbuf ) - 1 );
			pathbuf[sizeof( pathbuf ) - 1] = '\0';
		}

		req = ne_request_create( sess, "GET", pathbuf );
		ne_add_request_header( req, "Referer", state->url );

		/* Body reader – only invoked for 2xx responses */
		locationBuf[0] = '\0';
		bctx.state = state;
		bctx.fp    = NULL;
		ne_add_response_body_reader( req, ne_accept_2xx,
		                             neon_body_reader, &bctx );

		result = ne_request_dispatch( req );
		status = ne_get_status( req );
		code   = status ? status->code : 0;

		/* Read response headers after dispatch completes.
		 * ne_get_response_header() is only valid until ne_request_destroy(). */
		{
			const char *loc   = ne_get_response_header( req, "Location" );
			const char *clen  = ne_get_response_header( req, "Content-Length" );
			const char *cdisp = ( state->headerCheck && redirects == 0 )
			                    ? ne_get_response_header( req, "Content-Disposition" )
			                    : NULL;

			if ( loc ) {
				strncpy( locationBuf, loc, NEON_MAX_URL - 1 );
				locationBuf[NEON_MAX_URL - 1] = '\0';
			}

			if ( clen ) {
				long v = atol( clen );
				if ( v > 0 )
					state->totalBytes = v;
			}

			if ( cdisp )
				neon_content_disp_handler( state, cdisp );
		}

		/* Make sure the output file is flushed/closed before we decide */
		if ( bctx.fp ) {
			fclose( bctx.fp );
			bctx.fp = NULL;
		}

		/* Check for redirect (body reader not called for 3xx) */
		if ( code >= 301 && code <= 308 && code != 304 && locationBuf[0] ) {
			ne_request_destroy( req );
			ne_session_destroy( sess );
			ne_uri_free( &uri );

			/* Resolve relative redirects against the current host */
			if ( locationBuf[0] == '/' ) {
				snprintf( currentUrl, sizeof( currentUrl ),
				          "%s://%s:%d%s",
				          uri.scheme, uri.host, port, locationBuf );
			} else {
				strncpy( currentUrl, locationBuf, sizeof( currentUrl ) - 1 );
				currentUrl[sizeof( currentUrl ) - 1] = '\0';
			}
			continue;
		}

		/* Handle final result */
		state->httpCode = code;

		if ( state->complete == -1 ) {
			/* Error already set by body reader (e.g. invalid pak sig) */
			ne_request_destroy( req );
			ne_session_destroy( sess );
			ne_uri_free( &uri );
			return NULL;
		}

		if ( result == NE_OK && code >= 200 && code < 300 ) {
			state->complete = 1; /* success */
		} else {
			if ( result != NE_OK ) {
				snprintf( state->errorMsg, sizeof( state->errorMsg ),
				          "%s", ne_get_error( sess ) );
			} else {
				snprintf( state->errorMsg, sizeof( state->errorMsg ),
				          "HTTP error %d", code );
			}
			state->complete = -1;
		}

		ne_request_destroy( req );
		ne_session_destroy( sess );
		ne_uri_free( &uri );
		return NULL;
	}

	/* Fell through: too many redirects */
	strncpy( state->errorMsg, "Too many redirects",
	         sizeof( state->errorMsg ) - 1 );
	state->complete = -1;
	return NULL;
}

/* -----------------------------------------------------------------------
 * neon_dl_t lifecycle helpers
 * --------------------------------------------------------------------- */

static neon_dl_t *neon_alloc( void )
{
	neon_dl_t *s = (neon_dl_t *)calloc( 1, sizeof( neon_dl_t ) );
	if ( !s ) return NULL;
	pthread_mutex_init( &s->mutex, NULL );
	s->firstChunk = qtrue;
	return s;
}

/*
 * Signal the thread to abort, join it, then free the state.
 * Safe to call with NULL (no-op).
 */
static void neon_free( neon_dl_t *s )
{
	if ( !s ) return;
	s->abort = 1;
	if ( s->threadStarted )
		pthread_join( s->thread, NULL );
	pthread_mutex_destroy( &s->mutex );
	free( s );
}

static qboolean neon_start( neon_dl_t *s )
{
	if ( pthread_create( &s->thread, NULL, neon_download_thread, s ) != 0 )
		return qfalse;
	s->threadStarted = 1;
	return qtrue;
}

/* -----------------------------------------------------------------------
 *  CL_Neon_*  –  per-connection pak download (called by cl_main.c)
 * --------------------------------------------------------------------- */

qboolean CL_Neon_Init( void )
{
	clc.neonEnabled = qtrue;
	return qtrue;
}

void CL_Neon_Shutdown( void )
{
	CL_Neon_Cleanup();
	clc.neonEnabled = qfalse;
}

void CL_Neon_Cleanup( void )
{
	if ( clc.downloadNeon ) {
		neon_free( (neon_dl_t *)clc.downloadNeon );
		clc.downloadNeon  = NULL;
		clc.downloadNeonM = NULL;
	}
	if ( clc.download != FS_INVALID_HANDLE ) {
		FS_FCloseFile( clc.download );
		clc.download = FS_INVALID_HANDLE;
	}
}

void CL_Neon_BeginDownload( const char *localName, const char *remoteURL )
{
	neon_dl_t  *state;
	const char *homePath;

	clc.neonUsed = qtrue;
	Com_Printf( "URL: %s\n", remoteURL );

	CL_Neon_Cleanup(); /* drop any previous download */

	Q_strncpyz( clc.downloadURL,      remoteURL, sizeof( clc.downloadURL ) );
	Q_strncpyz( clc.downloadName,     localName, sizeof( clc.downloadName ) );
	Com_sprintf( clc.downloadTempName, sizeof( clc.downloadTempName ),
	             "%s.tmp", localName );

	Cvar_Set( "cl_downloadName",  localName );
	Cvar_Set( "cl_downloadSize",  "0" );
	Cvar_Set( "cl_downloadCount", "0" );
	Cvar_SetIntegerValue( "cl_downloadTime", cls.realtime );

	clc.downloadBlock = 0;
	clc.downloadCount = 0;

	state = neon_alloc();
	if ( !state ) {
		Com_Error( ERR_DROP, "CL_Neon_BeginDownload: out of memory" );
		return;
	}

	Q_strncpyz( state->url,  remoteURL, sizeof( state->url ) );
	Q_strncpyz( state->name, localName, sizeof( state->name ) );

	/* Build the full OS-level path for the temp file */
	homePath = Cvar_VariableString( "fs_homepath" );
	Com_sprintf( state->tempFilePath, sizeof( state->tempFilePath ),
	             "%s%c%s", homePath, PATH_SEP, clc.downloadTempName );

	state->headerCheck = qfalse;
	state->verbose     = ( com_developer->integer != 0 );

	if ( !neon_start( state ) ) {
		neon_free( state );
		Com_Error( ERR_DROP, "CL_Neon_BeginDownload: failed to start download thread" );
		return;
	}

	clc.downloadNeon  = state;
	clc.downloadNeonM = state; /* same pointer; CL_Frame checks downloadNeonM */

	/* Optionally disconnect from the game server while we download */
	if ( !( clc.sv_allowDownload & DLF_NO_DISCONNECT ) && !clc.neonDisconnected ) {
		CL_AddReliableCommand( "disconnect", qtrue );
		CL_WritePacket( 2 );
		clc.neonDisconnected = qtrue;
	}
}

void CL_Neon_PerformDownload( void )
{
	neon_dl_t *state = (neon_dl_t *)clc.downloadNeonM;
	int        result;
	char       errMsg[512];
	int        httpCode;

	if ( !state ) return;

	/* Update progress cvars from thread state */
	clc.downloadSize  = (int)state->totalBytes;
	clc.downloadCount = (int)state->receivedBytes;
	Cvar_SetIntegerValue( "cl_downloadSize",  clc.downloadSize );
	Cvar_SetIntegerValue( "cl_downloadCount", clc.downloadCount );

	if ( !state->complete ) return; /* still in flight */

	/* Thread finished – join it first, then release state */
	pthread_join( state->thread, NULL );
	state->threadStarted = 0;

	result   = state->complete;
	httpCode = state->httpCode;
	Q_strncpyz( errMsg, state->errorMsg, sizeof( errMsg ) );

	/* Free state and clear pointers before any potential longjmp */
	pthread_mutex_destroy( &state->mutex );
	free( state );
	clc.downloadNeon  = NULL;
	clc.downloadNeonM = NULL;

	if ( result == 1 ) {
		FS_SV_Rename( clc.downloadTempName, clc.downloadName );
		clc.downloadRestart = qtrue;
		CL_NextDownload();
	} else {
		Com_Error( ERR_DROP,
		           "Download Error: %s  Code: %d  URL: %s",
		           errMsg, httpCode, clc.downloadURL );
	}
}

/* -----------------------------------------------------------------------
 *  Com_DL_*  –  URL-based pak downloads (dlmap / cl_dlURL)
 * --------------------------------------------------------------------- */

qboolean Com_DL_InProgress( const download_t *dl )
{
	return ( dl->neon && dl->URL[0] ) ? qtrue : qfalse;
}

qboolean Com_DL_ValidFileName( const char *fileName )
{
	int c;
	while ( ( c = *fileName++ ) != '\0' ) {
		if ( c == '/' || c == '\\' || c == ':' ) return qfalse;
		if ( c < ' ' || c > '~' )                return qfalse;
	}
	return qtrue;
}

void Com_DL_Cleanup( download_t *dl )
{
	if ( dl->neon ) {
		neon_free( (neon_dl_t *)dl->neon );
		dl->neon  = NULL;
		dl->neonM = NULL;
	}

	if ( dl->mapAutoDownload ) {
		Cvar_Set( "cl_downloadName",  "" );
		Cvar_Set( "cl_downloadSize",  "0" );
		Cvar_Set( "cl_downloadCount", "0" );
		Cvar_Set( "cl_downloadTime",  "0" );
	}

	dl->Size  = 0;
	dl->Count = 0;

	dl->URL[0]     = '\0';
	dl->Name[0]    = '\0';
	dl->gameDir[0] = '\0';

	if ( dl->TempName[0] ) {
		FS_Remove( dl->TempName );
		dl->TempName[0] = '\0';
	}

	dl->progress[0]     = '\0';
	dl->headerCheck     = qfalse;
	dl->mapAutoDownload = qfalse;
	dl->fHandle         = FS_INVALID_HANDLE;
}

qboolean Com_DL_Begin( download_t *dl, const char *localName,
                       const char *remoteURL, qboolean autoDownload )
{
	char       *escapedName;
	const char *homePath;
	neon_dl_t  *state;
	const char *s;

	if ( Com_DL_InProgress( dl ) ) {
		Com_Printf( S_COLOR_YELLOW " already downloading %s\n", dl->Name );
		return qfalse;
	}

	Com_DL_Cleanup( dl );

	/* --- Build the download URL ---------------------------------------- */
	escapedName = url_encode( localName );
	if ( !escapedName ) {
		Com_Printf( S_COLOR_RED "Com_DL_Begin: url_encode() failed\n" );
		return qfalse;
	}

	Q_strncpyz( dl->URL, remoteURL, sizeof( dl->URL ) );

	if ( !Q_replace( "%1", escapedName, dl->URL, sizeof( dl->URL ) ) ) {
		/* No substitution marker – append encoded name to base URL */
		if ( dl->URL[strlen( dl->URL ) - 1] != '/' )
			Q_strcat( dl->URL, sizeof( dl->URL ), "/" );
		Q_strcat( dl->URL, sizeof( dl->URL ), escapedName );
		dl->headerCheck = qfalse;
	} else {
		dl->headerCheck = qtrue;
	}
	free( escapedName );

	Com_Printf( "URL: %s\n", dl->URL );

	/* --- Determine the download directory ------------------------------ */
	if ( cl_dlDirectory->integer )
		Q_strncpyz( dl->gameDir, FS_GetBaseGameDir(), sizeof( dl->gameDir ) );
	else
		Q_strncpyz( dl->gameDir, FS_GetCurrentGameDir(), sizeof( dl->gameDir ) );

	/* --- Derive pak name (basename without .pk3) ----------------------- */
	s = strrchr( localName, '/' );
	if ( s )
		Q_strncpyz( dl->Name, s + 1, sizeof( dl->Name ) );
	else
		Q_strncpyz( dl->Name, localName, sizeof( dl->Name ) );

	FS_StripExt( dl->Name, ".pk3" );
	if ( !dl->Name[0] ) {
		Com_Printf( S_COLOR_YELLOW " empty filename after extension strip.\n" );
		return qfalse;
	}

	/* --- Build temp-file names ----------------------------------------- */
	Com_sprintf( dl->TempName, sizeof( dl->TempName ),
	             "%s%c%s.%08x.tmp",
	             dl->gameDir, PATH_SEP, dl->Name,
	             rand() | ( rand() << 16 ) );

	/* --- Allocate and configure neon state ----------------------------- */
	state = neon_alloc();
	if ( !state ) {
		Com_Printf( S_COLOR_RED "Com_DL_Begin: out of memory\n" );
		return qfalse;
	}

	Q_strncpyz( state->url,  dl->URL,  sizeof( state->url ) );
	Q_strncpyz( state->name, dl->Name, sizeof( state->name ) );
	state->headerCheck     = dl->headerCheck;
	state->verbose         = ( com_developer->integer != 0 );
	state->firstChunk      = qtrue;

	homePath = Cvar_VariableString( "fs_homepath" );
	Com_sprintf( state->tempFilePath, sizeof( state->tempFilePath ),
	             "%s%c%s", homePath, PATH_SEP, dl->TempName );

	dl->mapAutoDownload = autoDownload;

	if ( autoDownload ) {
		Cvar_Set( "cl_downloadName",  dl->Name );
		Cvar_Set( "cl_downloadSize",  "0" );
		Cvar_Set( "cl_downloadCount", "0" );
		Cvar_SetIntegerValue( "cl_downloadTime", cls.realtime );
	}

	/* --- Launch download thread ---------------------------------------- */
	if ( !neon_start( state ) ) {
		neon_free( state );
		Com_Printf( S_COLOR_RED "Com_DL_Begin: failed to start download thread\n" );
		return qfalse;
	}

	dl->neon  = state;
	dl->neonM = state;
	dl->fHandle = FS_INVALID_HANDLE;

	return qtrue;
}

qboolean Com_DL_Perform( download_t *dl )
{
	neon_dl_t *state = (neon_dl_t *)dl->neon;
	char       name[sizeof( dl->TempName )];
	int        result;
	int        httpCode;
	char       errMsg[512];
	qboolean   autoDownload;

	if ( !state ) return qfalse;

	/* --- Update live progress ------------------------------------------ */
	dl->Size  = (int)state->totalBytes;
	dl->Count = (int)state->receivedBytes;

	if ( dl->mapAutoDownload && cls.state == CA_CONNECTED ) {
		Cvar_SetIntegerValue( "cl_downloadSize",  dl->Size );
		Cvar_SetIntegerValue( "cl_downloadCount", dl->Count );
	}

	/* Rebuild the on-screen progress string */
	if ( dl->Size > 0 ) {
		int pct = (int)( ( (long)dl->Count * 100 ) / (long)dl->Size );
		Com_sprintf( dl->progress, sizeof( dl->progress ),
		             " downloading %s: %s (%d%%)",
		             dl->Name, sizeToString( dl->Count ), pct );
	} else {
		Com_sprintf( dl->progress, sizeof( dl->progress ),
		             " downloading %s: %s",
		             dl->Name, sizeToString( dl->Count ) );
	}

	if ( !state->complete ) return qtrue; /* still downloading */

	/* --- Thread finished ----------------------------------------------- */
	pthread_join( state->thread, NULL );
	state->threadStarted = 0;

	result      = state->complete;
	httpCode    = state->httpCode;
	autoDownload = dl->mapAutoDownload;
	Q_strncpyz( errMsg, state->errorMsg, sizeof( errMsg ) );

	if ( result == 1 && state->foundName[0] ) {
		/* Content-Disposition supplied a file name – use it */
		Q_strncpyz( dl->Name, state->foundName, sizeof( dl->Name ) );
	}

	if ( result == 1 ) {
		/* Success: rename temp file and trigger FS reload */
		Com_sprintf( name, sizeof( name ), "%s%c%s.pk3",
		             dl->gameDir, PATH_SEP, dl->Name );

		if ( !FS_SV_FileExists( name ) ) {
			FS_SV_Rename( dl->TempName, name );
		} else {
			int cksum;
			cksum = FS_GetZipChecksum( name );
			Com_sprintf( name, sizeof( name ), "%s%c%s.%08x.pk3",
			             dl->gameDir, PATH_SEP, dl->Name, cksum );
			if ( FS_SV_FileExists( name ) )
				FS_Remove( name );
			FS_SV_Rename( dl->TempName, name );
		}

		/* Clear TempName so Com_DL_Cleanup does not try to delete the
		   just-renamed file. */
		dl->TempName[0] = '\0';

		Com_DL_Cleanup( dl );
		FS_Reload();

		Com_Printf( S_COLOR_GREEN "%s downloaded\n", name );

		if ( autoDownload ) {
			if ( cls.state == CA_CONNECTED && !clc.demoplaying ) {
				CL_AddReliableCommand( "donedl", qfalse );
			} else if ( clc.demoplaying ) {
				cls.startCgame = qtrue;
				Cbuf_ExecuteText( EXEC_APPEND, "vid_restart\n" );
			}
		}
		return qfalse;
	} else {
		/* Failure */
		Q_strncpyz( name, dl->TempName, sizeof( name ) );
		Com_DL_Cleanup( dl );
		FS_Remove( name );

		Com_Printf( S_COLOR_RED "Download Error: %s  Code: %d\n",
		            errMsg, httpCode );

		if ( autoDownload && cls.state == CA_CONNECTED ) {
			Com_Error( ERR_DROP, "download error" );
		}
	}

	return qtrue;
}

#endif /* USE_NEON */

