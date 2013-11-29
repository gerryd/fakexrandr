#include <dlfcn.h>
#include <stdio.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

/* Change this: */
#define SPLIT_SCREEN_HEIGHT 1050
#define SPLIT_SCREEN_WIDTH  3360
#define REAL_XRANDR_LIB "/usr/lib/x86_64-linux-gnu/libXrandr.so"
/* -- */

#define XID_SPLIT_MOD 0xf00000

#ifdef DEBUG
#define DEBUGF(...) DEBUGF(__VA_ARGS__)
#else
#define DEBUGF(...)
#endif

static XRRScreenResources *(*_XRRGetScreenResourcesCurrent)(Display *dpy, Window output);
static XRROutputInfo *(*_XRRGetOutputInfo)(Display *dpy, XRRScreenResources *resources, RROutput output);
static XRRCrtcInfo *(*_XRRGetCrtcInfo)(Display *dpy, XRRScreenResources *resources, RRCrtc crtc);
static XRRScreenResources *(*_XRRGetScreenResources)(Display *dpy, Window window);
static XRRPanning *(*_XRRGetPanning)(Display *dpy, XRRScreenResources *resources, RRCrtc crtc);
XRRCrtcGamma *(*_XRRGetCrtcGamma)(Display *dpy, RRCrtc crtc);
Status (*_XRRGetCrtcTransform)(Display	*dpy, RRCrtc crtc, XRRCrtcTransformAttributes **attributes);
int (*_XRRGetCrtcGammaSize)(Display *dpy, RRCrtc crtc);
Status (*_XRRSetCrtcConfig)(Display *dpy, XRRScreenResources *resources, RRCrtc crtc, Time timestamp, int x, int y, RRMode mode, Rotation rotation, RROutput *outputs, int noutputs);

static void _init() __attribute__((constructor));
static void _init() {
	void *xrandr_lib = dlopen(REAL_XRANDR_LIB, RTLD_LAZY | RTLD_GLOBAL);

	_XRRGetScreenResourcesCurrent = dlsym(xrandr_lib, "XRRGetScreenResourcesCurrent");
	_XRRGetOutputInfo = dlsym(xrandr_lib, "XRRGetOutputInfo");
	_XRRGetCrtcInfo = dlsym(xrandr_lib, "XRRGetCrtcInfo");
	_XRRGetScreenResources = dlsym(xrandr_lib, "XRRGetScreenResources");
	_XRRGetPanning = dlsym(xrandr_lib, "XRRGetPanning");
	_XRRGetCrtcGamma = dlsym(xrandr_lib, "XRRGetCrtcGamma");
	_XRRGetCrtcTransform = dlsym(xrandr_lib, "XRRGetCrtcTransform");
	_XRRGetCrtcGammaSize = dlsym(xrandr_lib, "XRRGetCrtcGammaSize");
	_XRRSetCrtcConfig = dlsym(xrandr_lib, "XRRSetCrtcConfig");
}

static bool check_if_crtc_is_wrong(Display *dpy, XRRScreenResources *resources, RRCrtc crtc) {
	XRRCrtcInfo *info = _XRRGetCrtcInfo(dpy, resources, crtc & ~XID_SPLIT_MOD);
	bool retval = info->width == SPLIT_SCREEN_WIDTH && info->height == SPLIT_SCREEN_HEIGHT;
	free(info);
	return retval;
}

static bool check_if_output_is_wrong(Display *dpy, XRRScreenResources *resources, RROutput output) {
	XRROutputInfo *info = _XRRGetOutputInfo(dpy, resources, output & ~XID_SPLIT_MOD);
	bool retval = info->crtc != 0 && check_if_crtc_is_wrong(dpy, resources, info->crtc);
	free(info);
	return retval;
}

static RRCrtc *append_fake_crtc(int *count, RRCrtc **crtcs, RRCrtc real_crtc) {
	(*count)++;

	RRCrtc *new_space = Xmalloc(sizeof(RRCrtc) * *count);
	memcpy(new_space, *crtcs, sizeof(RRCrtc) * (*count - 1));
	// Xfree(*crtcs);
	*crtcs = new_space;
	// *crtcs = realloc(*crtcs, sizeof(RRCrtc) * *count);
	assert((real_crtc & XID_SPLIT_MOD) == 0L);
	(*crtcs)[*count - 1] = real_crtc | XID_SPLIT_MOD;
}

static RRCrtc *append_fake_output(int *count, RRCrtc **outputs, RRCrtc real_output) {
	(*count)++;

	RRCrtc *new_space = Xmalloc(sizeof(RROutput) * *count);
	memcpy(new_space, *outputs, sizeof(RROutput) * (*count - 1));
	// Xfree(*outputs);
	*outputs = new_space;
	// *outputs = realloc(*outputs, sizeof(RROutput) * *count);
	assert((real_output & XID_SPLIT_MOD) == 0L);
	(*outputs)[*count - 1] = real_output | XID_SPLIT_MOD;
}

/*
typedef struct _XRRScreenResources {
    Time	timestamp;
    Time	configTimestamp;
    int		ncrtc;
    RRCrtc	*crtcs;              // <-- store these
    int		noutput;             // <-- intercept here
    RROutput	*outputs;        // <-- and here (see below)
    int		nmode;
    XRRModeInfo	*modes;
} XRRScreenResources;
XRRFreeScreenResources (resources);
Xmalloc(bytes)
*/

static XRRScreenResources *augment_resources(Display *dpy, Window window, XRRScreenResources *retval) {
	int i;
	for(i=0; i<retval->ncrtc; i++) {
		if(check_if_crtc_is_wrong(dpy, retval, retval->crtcs[i])) {
			// Add a second crtc (becomes the virtual split screen)
			append_fake_crtc(&retval->ncrtc, &retval->crtcs, retval->crtcs[i]);
			break;
		}
	}
	for(i=0; i<retval->noutput; i++) {
		if(check_if_output_is_wrong(dpy, retval, retval->outputs[i])) {
			// Add a second output (becomes the virtual split screen)
			append_fake_output(&retval->noutput, &retval->outputs, retval->outputs[i]);
			break;
		}
	}

	return retval;
}

XRRScreenResources *XRRGetScreenResources(Display *dpy, Window window) {
	XRRScreenResources *retval = augment_resources(dpy, window, _XRRGetScreenResources(dpy, window));
	DEBUGF("XRRGetScreenResources called, noutput=%d, first crtc=%lu\n", retval->noutput, *retval->crtcs);
	return retval;
}

XRRScreenResources *XRRGetScreenResourcesCurrent(Display *dpy, Window window) {
	XRRScreenResources *retval = augment_resources(dpy, window, _XRRGetScreenResourcesCurrent(dpy, window));
	DEBUGF("XRRGetScreenResourcesCurrent called, noutput=%d, first crtc=%lu\n", retval->noutput, *retval->crtcs);
	return retval;
}

/*
typedef struct _XRROutputInfo {
    Time	    timestamp;
    RRCrtc	    crtc;           // <-- store this identifier (is XID == ulong) to know which monitor is which
    char	    *name;
    int		    nameLen;
    unsigned long   mm_width;   // <-- override
    unsigned long   mm_height;  // <-- override
    Connection	    connection;
    SubpixelOrder   subpixel_order;
    int		    ncrtc;
    RRCrtc	    *crtcs;
    int		    nclone;
    RROutput	    *clones;
    int		    nmode;
    int		    npreferred;
    RRMode	    *modes;
} XRROutputInfo;
XRRFreeOutputInfo (output);
*/
XRROutputInfo *XRRGetOutputInfo(Display *dpy, XRRScreenResources *resources, RROutput output) {
	XRROutputInfo *retval = _XRRGetOutputInfo(dpy, resources, output & ~XID_SPLIT_MOD);

	if(check_if_output_is_wrong(dpy, resources, output)) {
		retval->mm_width /= 2;
		if(output & XID_SPLIT_MOD) {
			retval->name[retval->nameLen - 1] = '_';
			append_fake_crtc(&retval->ncrtc, &retval->crtcs, retval->crtc);
			retval->crtc = retval->crtc | XID_SPLIT_MOD;
		}
	}

	DEBUGF("XRRGetOutputInfo called: name=%s, mm_width=%lu, mm_height=%lu, crtc=%lu\n", retval->name, retval->mm_width, retval->mm_height, retval->crtc);
	return retval;
}

/*
typedef struct _XRRCrtcInfo {
    Time	    timestamp;
    int		    x, y;        // <-- override
    unsigned int    width, height;  // <-- override
    RRMode	    mode;
    Rotation	    rotation;
    int		    noutput;
    RROutput	    *outputs;
    Rotation	    rotations;
    int		    npossible;
    RROutput	    *possible;
} XRRCrtcInfo;
XRRFreeCrtcInfo (crtc);
*/
XRRCrtcInfo *XRRGetCrtcInfo(Display *dpy, XRRScreenResources *resources, RRCrtc crtc) {
	XRRCrtcInfo *retval = _XRRGetCrtcInfo(dpy, resources, crtc & ~XID_SPLIT_MOD);

	if(check_if_crtc_is_wrong(dpy, resources, crtc)) {
		retval->width /= 2;
		if(crtc & XID_SPLIT_MOD) {
			retval->x += retval->width;
		}
	}

	DEBUGF("XRRGetCrtcInfo called: id=%lu, x=%d, y=%d, width=%u, height=%u\n", crtc, retval->x, retval->y, retval->width, retval->height);
	return retval;
}

XRRPanning *XRRGetPanning(Display *dpy, XRRScreenResources *resources, RRCrtc crtc) {
	return _XRRGetPanning(dpy, resources, crtc & ~XID_SPLIT_MOD);
}

XRRCrtcGamma *XRRGetCrtcGamma(Display *dpy, RRCrtc crtc) {
	return _XRRGetCrtcGamma(dpy, crtc & ~XID_SPLIT_MOD);
}

Status XRRGetCrtcTransform(Display	*dpy, RRCrtc crtc, XRRCrtcTransformAttributes **attributes) {
	return _XRRGetCrtcTransform(dpy, crtc & ~XID_SPLIT_MOD, attributes);
}

int XRRGetCrtcGammaSize(Display *dpy, RRCrtc crtc) {
	return _XRRGetCrtcGammaSize(dpy, crtc & ~XID_SPLIT_MOD);
}

Status XRRSetCrtcConfig(Display *dpy, XRRScreenResources *resources, RRCrtc crtc, Time timestamp, int x, int y, RRMode mode, Rotation rotation, RROutput *outputs, int noutputs) {
	if(crtc & XID_SPLIT_MOD) {
		return 0;
	}
	return _XRRSetCrtcConfig(dpy, resources, crtc, timestamp, x, y, mode, rotation, outputs, noutputs);
}