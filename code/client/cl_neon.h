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

#ifndef __QNEON_H__
#define __QNEON_H__

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

qboolean CL_Neon_Init( void );
void     CL_Neon_Shutdown( void );
void     CL_Neon_BeginDownload( const char *localName, const char *remoteURL );
void     CL_Neon_PerformDownload( void );
void     CL_Neon_Cleanup( void );

/* download_t – used by the Com_DL_* family (URL-based pak downloads). */
typedef struct download_s {
	char         URL[MAX_OSPATH];
	char         Name[MAX_OSPATH];
	char         gameDir[MAX_OSPATH];
	char         TempName[MAX_OSPATH * 2 + 14]; /* gameDir/Name.XXXXXXXX.tmp */
	char         progress[MAX_OSPATH + 64];
	void        *neon;    /* non-NULL while a download thread is active */
	void        *neonM;   /* same pointer; checked by CL_Frame          */
	fileHandle_t fHandle;
	int          Size;
	int          Count;
	qboolean     headerCheck;
	qboolean     mapAutoDownload;
} download_t;

#endif /* __QNEON_H__ */
