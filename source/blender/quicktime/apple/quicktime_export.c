/**
 * $Id$
 *
 * quicktime_export.c
 *
 * Code to create QuickTime Movies with Blender
 *
 * ***** BEGIN GPL/BL DUAL LICENSE BLOCK *****
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version. The Blender
 * Foundation also sells licenses for use in proprietary software under
 * the Blender License.  See http://www.blender.org/BL/ for information
 * about this.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *
 * The Original Code is written by Rob Haarsma (phase)
 *
 * Contributor(s): Stefan Gartner (sgefant)
 *
 * ***** END GPL/BL DUAL LICENSE BLOCK *****
 */

#ifdef WITH_QUICKTIME
#if defined(_WIN32) || defined(__APPLE__)

#include "BKE_global.h"
#include "BKE_scene.h"
#include "BLI_blenlib.h"
#include "BIF_toolbox.h"	/* error() */
#include "BLO_sys_types.h"
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"
#include "MEM_guardedalloc.h"
#include "render.h"
#include "quicktime_import.h"
#include "quicktime_export.h"

#ifdef _WIN32
#include <QTML.h>
#include <Movies.h>
#include <QuicktimeComponents.h>
#include <TextUtils.h> 
#endif /* _WIN32 */

#ifdef __APPLE__
/* evil */
#ifndef __AIFF__
#define __AIFF__
#endif
#include <QuickTime/Movies.h>
#include <QuickTime/QuicktimeComponents.h>
#include <fcntl.h> /* open() */
#include <unistd.h> /* close() */
#include <sys/stat.h> /* file permissions */
#endif /* __APPLE__ */

#define	kMyCreatorType	FOUR_CHAR_CODE('TVOD')
#define	kTrackStart		0
#define	kMediaStart		0

static void QT_StartAddVideoSamplesToMedia (const Rect *trackFrame);
static void QT_DoAddVideoSamplesToMedia (int frame);
static void QT_EndAddVideoSamplesToMedia (void);
static void QT_CreateMyVideoTrack (void);
static void QT_EndCreateMyVideoTrack (void);
static void check_renderbutton_framerate(void);

typedef struct QuicktimeExport {

	FSSpec		theSpec;
	short		resRefNum;
	short		resId;
	short		movieResId;
	Str255		qtfilename;

	Media		theMedia;
	Movie		theMovie;
	Track		theTrack;

	GWorldPtr	theGWorld;
	PixMapHandle	thePixMap;

	ImageDescription	**anImageDescription;
	ImageSequence		anImageSequence;

	ImBuf		*ibuf;	//imagedata for Quicktime's Gworld

} QuicktimeExport;

typedef struct QuicktimeComponentData {

	ComponentInstance	theComponent;
	SCTemporalSettings  gTemporalSettings;
	SCSpatialSettings   gSpatialSettings;
	SCDataRateSettings  aDataRateSetting;
	TimeValue			duration;
	long				kVideoTimeScale;

} QuicktimeComponentData;

static struct QuicktimeExport *qtexport;
static struct QuicktimeComponentData *qtdata;

static int	sframe;


void CheckError(OSErr err, char *msg)
{
	if(err != noErr) printf("%s: %d\n", msg, err);
}


OSErr QT_SaveCodecSettingsToScene(void)
{	
	QTAtomContainer		myContainer = NULL;
	ComponentResult		myErr = noErr;
	Ptr					myPtr;
	long				mySize = 0;

	CodecInfo			ci;
	char str[255];

	QuicktimeCodecData *qcd = G.scene->r.qtcodecdata;

	// check if current scene already has qtcodec settings, and clear them
	if (qcd) {
		free_qtcodecdata(qcd);
	} else {
		qcd = G.scene->r.qtcodecdata = MEM_callocN(sizeof(QuicktimeCodecData), "QuicktimeCodecData");
	}

	// obtain all current codec settings
	SCSetInfo(qtdata->theComponent, scTemporalSettingsType,	&qtdata->gTemporalSettings);
	SCSetInfo(qtdata->theComponent, scSpatialSettingsType,	&qtdata->gSpatialSettings);
	SCSetInfo(qtdata->theComponent, scDataRateSettingsType,	&qtdata->aDataRateSetting);

	// retreive codecdata from quicktime in a atomcontainer
	myErr = SCGetSettingsAsAtomContainer(qtdata->theComponent,  &myContainer);
	if (myErr != noErr) {
		printf("Quicktime: SCGetSettingsAsAtomContainer failed\n"); 
		goto bail;
	}

	// get the size of the atomcontainer
	mySize = GetHandleSize((Handle)myContainer);

	// lock and convert the atomcontainer to a *valid* pointer
	QTLockContainer(myContainer);
	myPtr = *(Handle)myContainer;

	// copy the Quicktime data into the blender qtcodecdata struct
	if (myPtr) {
		qcd->cdParms = MEM_mallocN(mySize, "qt.cdParms");
		memcpy(qcd->cdParms, myPtr, mySize);
		qcd->cdSize = mySize;

		GetCodecInfo (&ci, qtdata->gSpatialSettings.codecType, 0);
		CopyPascalStringToC(ci.typeName, str);
		sprintf(qcd->qtcodecname, "Codec: %s", str);
	} else {
		printf("Quicktime: SaveExporterSettingsToMem failed\n"); 
	}

	QTUnlockContainer(myContainer);

bail:
	if (myContainer != NULL)
		QTDisposeAtomContainer(myContainer);
		
	return((OSErr)myErr);
}


OSErr QT_GetCodecSettingsFromScene(void)
{	
	Handle				myHandle = NULL;
	ComponentResult		myErr = noErr;
//	CodecInfo ci;
//	char str[255];

	QuicktimeCodecData *qcd = G.scene->r.qtcodecdata;

	// if there is codecdata in the blendfile, convert it to a Quicktime handle 
	if (qcd) {
		myHandle = NewHandle(qcd->cdSize);
		PtrToHand( qcd->cdParms, &myHandle, qcd->cdSize);
	}
		
	// restore codecsettings to the quicktime component
	if(qcd->cdParms && qcd->cdSize) {
		myErr = SCSetSettingsFromAtomContainer((GraphicsExportComponent)qtdata->theComponent, (QTAtomContainer)myHandle);
		if (myErr != noErr) {
			printf("Quicktime: SCSetSettingsFromAtomContainer failed\n"); 
			goto bail;
		}

		// update runtime codecsettings for use with the codec dialog
		SCGetInfo(qtdata->theComponent, scDataRateSettingsType,	&qtdata->aDataRateSetting);
		SCGetInfo(qtdata->theComponent, scSpatialSettingsType,	&qtdata->gSpatialSettings);
		SCGetInfo(qtdata->theComponent, scTemporalSettingsType,	&qtdata->gTemporalSettings);

//		GetCodecInfo (&ci, qtdata->gSpatialSettings.codecType, 0);
//		CopyPascalStringToC(ci.typeName, str);
//		printf("restored Codec: %s\n", str);
	} else {
		printf("Quicktime: GetExporterSettingsFromMem failed\n"); 
	}
bail:
	if (myHandle != NULL)
		DisposeHandle(myHandle);
		
	return((OSErr)myErr);
}


OSErr QT_AddUserDataTextToMovie (Movie theMovie, char *theText, OSType theType)
{
	UserData					myUserData = NULL;
	Handle						myHandle = NULL;
	long						myLength = strlen(theText);
	OSErr						myErr = noErr;

	// get the movie's user data list
	myUserData = GetMovieUserData(theMovie);
	if (myUserData == NULL)
		return(paramErr);
	
	// copy the specified text into a new handle
	myHandle = NewHandleClear(myLength);
	if (myHandle == NULL)
		return(MemError());

	BlockMoveData(theText, *myHandle, myLength);

	// add the data to the movie's user data
	myErr = AddUserDataText(myUserData, myHandle, theType, 1, (short)GetScriptManagerVariable(smRegionCode));

	// clean up
	DisposeHandle(myHandle);
	return(myErr);
}


static void QT_CreateMyVideoTrack(void)
{
	OSErr err = noErr;
	Rect trackFrame;
	MatrixRecord myMatrix;

	trackFrame.top = 0;
	trackFrame.left = 0;
	trackFrame.bottom = R.recty;
	trackFrame.right = R.rectx;
	
	qtexport->theTrack = NewMovieTrack (qtexport->theMovie, 
							FixRatio(trackFrame.right,1),
							FixRatio(trackFrame.bottom,1), 
							kNoVolume);
	CheckError( GetMoviesError(), "NewMovieTrack error" );

	SetIdentityMatrix(&myMatrix);
	ScaleMatrix(&myMatrix, fixed1, Long2Fix(-1), 0, 0);
	TranslateMatrix(&myMatrix, 0, Long2Fix(trackFrame.bottom));
	SetMovieMatrix(qtexport->theMovie, &myMatrix);

	qtexport->theMedia = NewTrackMedia (qtexport->theTrack,
							VideoMediaType,
							qtdata->kVideoTimeScale,
							nil,
							0);
	CheckError( GetMoviesError(), "NewTrackMedia error" );

	err = BeginMediaEdits (qtexport->theMedia);
	CheckError( err, "BeginMediaEdits error" );

	QT_StartAddVideoSamplesToMedia (&trackFrame);
} 


static void QT_EndCreateMyVideoTrack(void)
{
	OSErr err = noErr;

	QT_EndAddVideoSamplesToMedia ();

	err = EndMediaEdits (qtexport->theMedia);
	CheckError( err, "EndMediaEdits error" );

	err = InsertMediaIntoTrack (qtexport->theTrack,
								kTrackStart,/* track start time */
								kMediaStart,/* media start time */
								GetMediaDuration (qtexport->theMedia),
								fixed1);
	CheckError( err, "InsertMediaIntoTrack error" );
} 


static void QT_StartAddVideoSamplesToMedia (const Rect *trackFrame)
{
	OSErr err = noErr;

	qtexport->ibuf = IMB_allocImBuf (R.rectx, R.recty, 32, IB_rect, 0);

	err = NewGWorldFromPtr( &qtexport->theGWorld,
							k32RGBAPixelFormat,
							trackFrame,
							NULL, NULL, 0,
							(unsigned char *)qtexport->ibuf->rect,
							R.rectx * 4 );
	CheckError (err, "NewGWorldFromPtr error");

	qtexport->thePixMap = GetGWorldPixMap(qtexport->theGWorld);
	LockPixels(qtexport->thePixMap);

	SCDefaultPixMapSettings (qtdata->theComponent, qtexport->thePixMap, true);

	SCSetInfo(qtdata->theComponent, scTemporalSettingsType,	&qtdata->gTemporalSettings);
	SCSetInfo(qtdata->theComponent, scSpatialSettingsType,	&qtdata->gSpatialSettings);
	SCSetInfo(qtdata->theComponent, scDataRateSettingsType,	&qtdata->aDataRateSetting);

	err = SCCompressSequenceBegin(qtdata->theComponent, qtexport->thePixMap, NULL, &qtexport->anImageDescription); 
	CheckError (err, "SCCompressSequenceBegin error" );
}


static void QT_DoAddVideoSamplesToMedia (int frame)
{
	OSErr	err = noErr;
	Rect	imageRect;

	register int		boxsize;
	register uint32_t	*readPos;
	register uint32_t	*changePos;
	Ptr					myPtr;

	short	syncFlag;
	long	dataSize;
	Handle	compressedData;

	//get pointers to parse bitmapdata
	myPtr = GetPixBaseAddr(qtexport->thePixMap);
	imageRect = (**qtexport->thePixMap).bounds;

	boxsize = R.rectx * R.recty;
	readPos = (uint32_t *) R.rectot;
	changePos = (uint32_t *) myPtr;

	//parse render bitmap into Quicktime's GWorld
	memcpy(changePos, readPos, boxsize*4);

	err = SCCompressSequenceFrame(qtdata->theComponent,
		qtexport->thePixMap,
		&imageRect,
		&compressedData,
		&dataSize,
		&syncFlag);
	CheckError(err, "SCCompressSequenceFrame error");

	err = AddMediaSample(qtexport->theMedia,
		compressedData,
		0,
		dataSize,
		qtdata->duration,
		(SampleDescriptionHandle)qtexport->anImageDescription,
		1,
		syncFlag,
		NULL);
	CheckError(err, "AddMediaSample error");

	printf ("added frame %3d (frame %3d in movie): ", frame, frame-sframe);
}


static void QT_EndAddVideoSamplesToMedia (void)
{
	SCCompressSequenceEnd(qtdata->theComponent);

	UnlockPixels(qtexport->thePixMap);
	if (qtexport->theGWorld) DisposeGWorld (qtexport->theGWorld);
	if (qtexport->ibuf) IMB_freeImBuf(qtexport->ibuf);
} 


void makeqtstring (char *string) {
	char txt[64];

	if (string==0) return;

	strcpy(string, G.scene->r.pic);
	BLI_convertstringcode(string, G.sce, G.scene->r.cfra);

	RE_make_existing_file(string);

	if (strcasecmp(string + strlen(string) - 4, ".mov")) {
		sprintf(txt, "%04d_%04d.mov", (G.scene->r.sfra) , (G.scene->r.efra) );
		strcat(string, txt);
	}
}


void start_qt(void) {
	OSErr err = noErr;

	char name[2048];
	char theFullPath[255];

#ifdef __APPLE__
	int		myFile;
	FSRef	myRef;
#else
	char	*qtname;
#endif

	if(qtexport == NULL) qtexport = MEM_callocN(sizeof(QuicktimeExport), "QuicktimeExport");

	if(qtdata) {
		if(qtdata->theComponent) CloseComponent(qtdata->theComponent);
		free_qtcomponentdata();
	}

	qtdata = MEM_callocN(sizeof(QuicktimeComponentData), "QuicktimeCodecDataExt");

	if(G.scene->r.qtcodecdata == NULL && G.scene->r.qtcodecdata->cdParms == NULL) {
		get_qtcodec_settings();
	} else {
		qtdata->theComponent = OpenDefaultComponent(StandardCompressionType, StandardCompressionSubType);

		QT_GetCodecSettingsFromScene();
		check_renderbutton_framerate();
	}
	
	if (G.afbreek != 1) {
		sframe = (G.scene->r.sfra);

		makeqtstring(name);

#ifdef __APPLE__
		sprintf(theFullPath, "%s", name);

		/* hack: create an empty file to make FSPathMakeRef() happy */
		myFile = open(theFullPath, O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRUSR|S_IWUSR);
		if (myFile < 0) {
			printf("error while creating file!\n");
			/* do something? */
		}
		close(myFile);
		err = FSPathMakeRef(theFullPath, &myRef, 0);
		CheckError(err, "FsPathMakeRef error");
		err = FSGetCatalogInfo(&myRef, kFSCatInfoNone, NULL, NULL, &qtexport->theSpec, NULL);
		CheckError(err, "FsGetCatalogInfoRef error");
#else
		qtname = get_valid_qtname(name);
		sprintf(theFullPath, "%s", qtname);
		
		CopyCStringToPascal(theFullPath, qtexport->qtfilename);
		err = FSMakeFSSpec(0, 0L, qtexport->qtfilename, &qtexport->theSpec);
#endif

		err = CreateMovieFile (&qtexport->theSpec, 
							kMyCreatorType,
							smCurrentScript, 
							createMovieFileDeleteCurFile | createMovieFileDontCreateResFile,
							&qtexport->resRefNum, 
							&qtexport->theMovie );
		CheckError(err, "CreateMovieFile error");

		if(err != noErr) {
			G.afbreek = 1;
#ifdef __APPLE__
			error("Unable to create Quicktime movie: %s\n", name);
#else
			error("Unable to create Quicktime movie: %s\n", qtname);
			MEM_freeN(qtname);
#endif
		} else {

#ifdef __APPLE__
			printf("Created QuickTime movie: %s\n", name);
#else
			printf("Created QuickTime movie: %s\n", qtname);
			MEM_freeN(qtname);
#endif

			QT_CreateMyVideoTrack();
		}
	}
}


void append_qt(int frame) {
	QT_DoAddVideoSamplesToMedia(frame);
}


void end_qt(void) {
	OSErr err = noErr;

	if(qtexport->theMovie) {
		QT_EndCreateMyVideoTrack ();

		qtexport->resId = movieInDataForkResID;
		err = AddMovieResource (qtexport->theMovie, qtexport->resRefNum, &qtexport->resId, qtexport->qtfilename);
		CheckError(err, "AddMovieResource error");

		err = QT_AddUserDataTextToMovie(qtexport->theMovie, "Made with Blender", kUserDataTextInformation);
		CheckError(err, "AddUserDataTextToMovie error");

		err = UpdateMovieResource(qtexport->theMovie, qtexport->resRefNum, qtexport->resId, qtexport->qtfilename);
		CheckError(err, "UpdateMovieResource error");

		if(qtexport->resRefNum) CloseMovieFile(qtexport->resRefNum);

		DisposeMovie(qtexport->theMovie);

		printf("Finished QuickTime movie.\n");
	}

	if(qtexport) {
		MEM_freeN(qtexport);
		qtexport = NULL;
	}
}


void free_qtcomponentdata(void) {
	if(qtdata) {
		if(qtdata->theComponent) CloseComponent(qtdata->theComponent);
		MEM_freeN(qtdata);
		qtdata = NULL;
	}
}


static void check_renderbutton_framerate(void) {
	//    To keep float framerates consistent between the codec dialog and frs/sec button.
	OSErr	err;	

	err = SCGetInfo(qtdata->theComponent, scTemporalSettingsType,	&qtdata->gTemporalSettings);
	CheckError(err, "SCGetInfo fr error");

	if( (G.scene->r.frs_sec == 24 || G.scene->r.frs_sec == 30 || G.scene->r.frs_sec == 60) &&
		(qtdata->gTemporalSettings.frameRate == 1571553 ||
		 qtdata->gTemporalSettings.frameRate == 1964113 ||
		 qtdata->gTemporalSettings.frameRate == 3928227)) {;} else
	qtdata->gTemporalSettings.frameRate = G.scene->r.frs_sec << 16;

	err = SCSetInfo(qtdata->theComponent, scTemporalSettingsType,	&qtdata->gTemporalSettings);
	CheckError( err, "SCSetInfo error" );

	if(qtdata->gTemporalSettings.frameRate == 1571553) {			// 23.98 fps
		qtdata->kVideoTimeScale = 2398;
		qtdata->duration = 100;
	} else if (qtdata->gTemporalSettings.frameRate == 1964113) {	// 29.97 fps
		qtdata->kVideoTimeScale = 2997;
		qtdata->duration = 100;
	} else if (qtdata->gTemporalSettings.frameRate == 3928227) {	// 59.94 fps
		qtdata->kVideoTimeScale = 5994;
		qtdata->duration = 100;
	} else {
		qtdata->kVideoTimeScale = (qtdata->gTemporalSettings.frameRate >> 16) * 100;
		qtdata->duration = 100;
	}
}


int get_qtcodec_settings(void) 
{
	OSErr	err = noErr;

	// erase any existing codecsetting
	if(qtdata) {
		if(qtdata->theComponent) CloseComponent(qtdata->theComponent);
		free_qtcomponentdata();
	}

	// allocate new
	qtdata = MEM_callocN(sizeof(QuicktimeComponentData), "QuicktimeComponentData");
	qtdata->theComponent = OpenDefaultComponent(StandardCompressionType, StandardCompressionSubType);

	// get previous selected codecsetting, if any 
	if(G.scene->r.qtcodecdata && G.scene->r.qtcodecdata->cdParms) {
		QT_GetCodecSettingsFromScene();
		check_renderbutton_framerate();
	} else {

	// configure the standard image compression dialog box
	// set some default settings
//		qtdata->gSpatialSettings.codecType = nil;     
		qtdata->gSpatialSettings.codec = anyCodec;         
//		qtdata->gSpatialSettings.depth;         
		qtdata->gSpatialSettings.spatialQuality = codecMaxQuality;

		qtdata->gTemporalSettings.temporalQuality = codecMaxQuality;
//		qtdata->gTemporalSettings.frameRate;      
		qtdata->gTemporalSettings.keyFrameRate = 25;   

		qtdata->aDataRateSetting.dataRate = 90 * 1024;          
//		qtdata->aDataRateSetting.frameDuration;     
//		qtdata->aDataRateSetting.minSpatialQuality; 
//		qtdata->aDataRateSetting.minTemporalQuality;

		err = SCSetInfo(qtdata->theComponent, scTemporalSettingsType,	&qtdata->gTemporalSettings);
		CheckError(err, "SCSetInfo1 error");
		err = SCSetInfo(qtdata->theComponent, scSpatialSettingsType,	&qtdata->gSpatialSettings);
		CheckError(err, "SCSetInfo2 error");
		err = SCSetInfo(qtdata->theComponent, scDataRateSettingsType,	&qtdata->aDataRateSetting);
		CheckError(err, "SCSetInfo3 error");
	}

	check_renderbutton_framerate();

	// put up the dialog box
	err = SCRequestSequenceSettings(qtdata->theComponent);
 
	if (err == scUserCancelled) {
		G.afbreek = 1;
		return 0;
	}

	// get user selected data
	SCGetInfo(qtdata->theComponent, scTemporalSettingsType,	&qtdata->gTemporalSettings);
	SCGetInfo(qtdata->theComponent, scSpatialSettingsType,	&qtdata->gSpatialSettings);
	SCGetInfo(qtdata->theComponent, scDataRateSettingsType,	&qtdata->aDataRateSetting);

	QT_SaveCodecSettingsToScene();

	// framerate jugglin'
	if(qtdata->gTemporalSettings.frameRate == 1571553) {			// 23.98 fps
		qtdata->kVideoTimeScale = 2398;
		qtdata->duration = 100;

		G.scene->r.frs_sec = 24;
	} else if (qtdata->gTemporalSettings.frameRate == 1964113) {	// 29.97 fps
		qtdata->kVideoTimeScale = 2997;
		qtdata->duration = 100;

		G.scene->r.frs_sec = 30;
	} else if (qtdata->gTemporalSettings.frameRate == 3928227) {	// 59.94 fps
		qtdata->kVideoTimeScale = 5994;
		qtdata->duration = 100;

		G.scene->r.frs_sec = 60;
	} else {
		qtdata->kVideoTimeScale = 600;
		qtdata->duration = qtdata->kVideoTimeScale / (qtdata->gTemporalSettings.frameRate / 65536);

		G.scene->r.frs_sec = (qtdata->gTemporalSettings.frameRate / 65536);
	}

	return 1;
}

#endif /* _WIN32 || __APPLE__ */
#endif /* WITH_QUICKTIME */

