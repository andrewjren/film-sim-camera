/*
 * modeset - DRM Modesetting Example
 *
 * Written 2012 by David Rheinsberg <david.rheinsberg@gmail.com>
 * Dedicated to the Public Domain.
 */

/*
 * DRM Modesetting Howto
 * This document describes the DRM modesetting API. Before we can use the DRM
 * API, we have to include xf86drm.h and xf86drmMode.h. Both are provided by
 * libdrm which every major distribution ships by default. It has no other
 * dependencies and is pretty small.
 *
 * Please ignore all forward-declarations of functions which are used later. I
 * reordered the functions so you can read this document from top to bottom. If
 * you reimplement it, you would probably reorder the functions to avoid all the
 * nasty forward declarations.
 *
 * For easier reading, we ignore all memory-allocation errors of malloc() and
 * friends here. However, we try to correctly handle all other kinds of errors
 * that may occur.
 *
 * All functions and global variables are prefixed with "modeset_*" in this
 * file. So it should be clear whether a function is a local helper or if it is
 * provided by some external library.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <iomanip>
#include <memory>
#include <thread>
#include <libcamera/libcamera.h>
#include <gbm.h>
#include <EGL/egl.h>
//#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
static std::shared_ptr<libcamera::Camera> camera;

struct modeset_dev;
static int modeset_find_crtc(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct modeset_dev *dev);
static int modeset_create_fb(int fd, struct modeset_dev *dev);
static int modeset_setup_dev(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct modeset_dev *dev);
static int modeset_open(int *out, const char *node);
static int modeset_prepare(int fd);
static void modeset_cleanup(int fd);

int device;
uint32_t connectorId;
drmModeModeInfo mode;
drmModeCrtc *crtc;
struct gbm_device *gbmDevice;
struct gbm_surface *gbmSurface;

static struct gbm_bo *previousBo = NULL;
static uint32_t previousFb;

// added to make render loop easier
int test_width, test_height, test_nrChannels;
unsigned int dstFBO, dstTex;
unsigned int lut_texture;
unsigned int test_texture;
unsigned int fbo;

/*
 * When the linux kernel detects a graphics-card on your machine, it loads the
 * correct device driver (located in kernel-tree at ./drivers/gpu/drm/<xy>) and
 * provides two character-devices to control it. Udev (or whatever hotplugging
 * application you use) will create them as:
 *     /dev/dri/card0
 *     /dev/dri/controlID64
 * We only need the first one. You can hard-code this path into your application
 * like we do here, but it is recommended to use libudev with real hotplugging
 * and multi-seat support. However, this is beyond the scope of this document.
 * Also note that if you have multiple graphics-cards, there may also be
 * /dev/dri/card1, /dev/dri/card2, ...
 *
 * We simply use /dev/dri/card0 here but the user can specify another path on
 * the command line.
 *
 * modeset_open(out, node): This small helper function opens the DRM device
 * which is given as @node. The new fd is stored in @out on success. On failure,
 * a negative error code is returned.
 * After opening the file, we also check for the DRM_CAP_DUMB_BUFFER capability.
 * If the driver supports this capability, we can create simple memory-mapped
 * buffers without any driver-dependent code. As we want to avoid any radeon,
 * nvidia, intel, etc. specific code, we depend on DUMB_BUFFERs here.
 */

static int modeset_open(int *out, const char *node)
{
	int fd, ret;
	uint64_t has_dumb;

	fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		ret = -errno;
		fprintf(stderr, "cannot open '%s': %m\n", node);
		return ret;
	}

	if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 ||
	    !has_dumb) {
		fprintf(stderr, "drm device '%s' does not support dumb buffers\n",
			node);
		close(fd);
		return -EOPNOTSUPP;
	}

	*out = fd;
	return 0;
}

/*
 * As a next step we need to find our available display devices. libdrm provides
 * a drmModeRes structure that contains all the needed information. We can
 * retrieve it via drmModeGetResources(fd) and free it via
 * drmModeFreeResources(res) again.
 *
 * A physical connector on your graphics card is called a "connector". You can
 * plug a monitor into it and control what is displayed. We are definitely
 * interested in what connectors are currently used, so we simply iterate
 * through the list of connectors and try to display a test-picture on each
 * available monitor.
 * However, this isn't as easy as it sounds. First, we need to check whether the
 * connector is actually used (a monitor is plugged in and turned on). Then we
 * need to find a CRTC that can control this connector. CRTCs are described
 * later on. After that we create a framebuffer object. If we have all this, we
 * can mmap() the framebuffer and draw a test-picture into it. Then we can tell
 * the DRM device to show the framebuffer on the given CRTC with the selected
 * connector.
 *
 * As we want to draw moving pictures on the framebuffer, we actually have to
 * remember all these settings. Therefore, we create one "struct modeset_dev"
 * object for each connector+crtc+framebuffer pair that we successfully
 * initialized and push it into the global device-list.
 *
 * Each field of this structure is described when it is first used. But as a
 * summary:
 * "struct modeset_dev" contains: {
 *  - @next: points to the next device in the single-linked list
 *
 *  - @width: width of our buffer object
 *  - @height: height of our buffer object
 *  - @stride: stride value of our buffer object
 *  - @size: size of the memory mapped buffer
 *  - @handle: a DRM handle to the buffer object that we can draw into
 *  - @map: pointer to the memory mapped buffer
 *
 *  - @mode: the display mode that we want to use
 *  - @fb: a framebuffer handle with our buffer object as scanout buffer
 *  - @conn: the connector ID that we want to use with this buffer
 *  - @crtc: the crtc ID that we want to use with this connector
 *  - @saved_crtc: the configuration of the crtc before we changed it. We use it
 *                 so we can restore the same mode when we exit.
 * }
 */

struct modeset_dev {
	struct modeset_dev *next;

	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t size;
	uint32_t handle;
	uint8_t *map;

	drmModeModeInfo mode;
	uint32_t fb;
	uint32_t conn;
	uint32_t crtc;
	drmModeCrtc *saved_crtc;
};

static struct modeset_dev *modeset_list = NULL;

/*
 * So as next step we need to actually prepare all connectors that we find. We
 * do this in this little helper function:
 *
 * modeset_prepare(fd): This helper function takes the DRM fd as argument and
 * then simply retrieves the resource-info from the device. It then iterates
 * through all connectors and calls other helper functions to initialize this
 * connector (described later on).
 * If the initialization was successful, we simply add this object as new device
 * into the global modeset device list.
 *
 * The resource-structure contains a list of all connector-IDs. We use the
 * helper function drmModeGetConnector() to retrieve more information on each
 * connector. After we are done with it, we free it again with
 * drmModeFreeConnector().
 * Our helper modeset_setup_dev() returns -ENOENT if the connector is currently
 * unused and no monitor is plugged in. So we can ignore this connector.
 */

static int modeset_prepare(int fd)
{
	drmModeRes *res;
	drmModeConnector *conn;
	unsigned int i;
	struct modeset_dev *dev;
	int ret;

	/* retrieve resources */
	res = drmModeGetResources(fd);
	if (!res) {
		fprintf(stderr, "cannot retrieve DRM resources (%d): %m\n",
			errno);
		return -errno;
	}

	/* iterate all connectors */
	for (i = 0; i < res->count_connectors; ++i) {
		/* get information for each connector */
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn) {
			fprintf(stderr, "cannot retrieve DRM connector %u:%u (%d): %m\n",
				i, res->connectors[i], errno);
			continue;
		}

		/* create a device structure */
		dev = static_cast<modeset_dev*>(malloc(sizeof(*dev)));
		memset(dev, 0, sizeof(*dev));
		dev->conn = conn->connector_id;

		/* call helper function to prepare this connector */
		ret = modeset_setup_dev(fd, res, conn, dev);
		if (ret) {
			if (ret != -ENOENT) {
				errno = -ret;
				fprintf(stderr, "cannot setup device for connector %u:%u (%d): %m\n",
					i, res->connectors[i], errno);
			}
			free(dev);
			drmModeFreeConnector(conn);
			continue;
		}

		/* free connector data and link device into global list */
		drmModeFreeConnector(conn);
		dev->next = modeset_list;
		modeset_list = dev;
	}

	/* free resources again */
	drmModeFreeResources(res);
	return 0;
}

/*
 * Now we dig deeper into setting up a single connector. As described earlier,
 * we need to check several things first:
 *   * If the connector is currently unused, that is, no monitor is plugged in,
 *     then we can ignore it.
 *   * We have to find a suitable resolution and refresh-rate. All this is
 *     available in drmModeModeInfo structures saved for each crtc. We simply
 *     use the first mode that is available. This is always the mode with the
 *     highest resolution.
 *     A more sophisticated mode-selection should be done in real applications,
 *     though.
 *   * Then we need to find an CRTC that can drive this connector. A CRTC is an
 *     internal resource of each graphics-card. The number of CRTCs controls how
 *     many connectors can be controlled indepedently. That is, a graphics-cards
 *     may have more connectors than CRTCs, which means, not all monitors can be
 *     controlled independently.
 *     There is actually the possibility to control multiple connectors via a
 *     single CRTC if the monitors should display the same content. However, we
 *     do not make use of this here.
 *     So think of connectors as pipelines to the connected monitors and the
 *     CRTCs are the controllers that manage which data goes to which pipeline.
 *     If there are more pipelines than CRTCs, then we cannot control all of
 *     them at the same time.
 *   * We need to create a framebuffer for this connector. A framebuffer is a
 *     memory buffer that we can write XRGB32 data into. So we use this to
 *     render our graphics and then the CRTC can scan-out this data from the
 *     framebuffer onto the monitor.
 */

static int modeset_setup_dev(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct modeset_dev *dev)
{
	int ret;

	/* check if a monitor is connected */
	if (conn->connection != DRM_MODE_CONNECTED) {
		fprintf(stderr, "ignoring unused connector %u\n",
			conn->connector_id);
		return -ENOENT;
	}

	/* check if there is at least one valid mode */
	if (conn->count_modes == 0) {
		fprintf(stderr, "no valid mode for connector %u\n",
			conn->connector_id);
		return -EFAULT;
	}

	/* copy the mode information into our device structure */
	memcpy(&dev->mode, &conn->modes[0], sizeof(dev->mode));
	dev->width = conn->modes[0].hdisplay;
	dev->height = conn->modes[0].vdisplay;
	fprintf(stderr, "mode for connector %u is %ux%u\n",
		conn->connector_id, dev->width, dev->height);

	/* find a crtc for this connector */
	ret = modeset_find_crtc(fd, res, conn, dev);
	if (ret) {
		fprintf(stderr, "no valid crtc for connector %u\n",
			conn->connector_id);
		return ret;
	}

	/* create a framebuffer for this CRTC */
	ret = modeset_create_fb(fd, dev);
	if (ret) {
		fprintf(stderr, "cannot create framebuffer for connector %u\n",
			conn->connector_id);
		return ret;
	}

	return 0;
}

/*
 * modeset_find_crtc(fd, res, conn, dev): This small helper tries to find a
 * suitable CRTC for the given connector. We have actually have to introduce one
 * more DRM object to make this more clear: Encoders.
 * Encoders help the CRTC to convert data from a framebuffer into the right
 * format that can be used for the chosen connector. We do not have to
 * understand any more of these conversions to make use of it. However, you must
 * know that each connector has a limited list of encoders that it can use. And
 * each encoder can only work with a limited list of CRTCs. So what we do is
 * trying each encoder that is available and looking for a CRTC that this
 * encoder can work with. If we find the first working combination, we are happy
 * and write it into the @dev structure.
 * But before iterating all available encoders, we first try the currently
 * active encoder+crtc on a connector to avoid a full modeset.
 *
 * However, before we can use a CRTC we must make sure that no other device,
 * that we setup previously, is already using this CRTC. Remember, we can only
 * drive one connector per CRTC! So we simply iterate through the "modeset_list"
 * of previously setup devices and check that this CRTC wasn't used before.
 * Otherwise, we continue with the next CRTC/Encoder combination.
 */

static int modeset_find_crtc(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct modeset_dev *dev)
{
	drmModeEncoder *enc;
	unsigned int i, j;
	int32_t crtc;
	struct modeset_dev *iter;

	/* first try the currently conected encoder+crtc */
	if (conn->encoder_id)
		enc = drmModeGetEncoder(fd, conn->encoder_id);
	else
		enc = NULL;

	if (enc) {
		if (enc->crtc_id) {
			crtc = enc->crtc_id;
			for (iter = modeset_list; iter; iter = iter->next) {
				if (iter->crtc == crtc) {
					crtc = -1;
					break;
				}
			}

			if (crtc >= 0) {
				drmModeFreeEncoder(enc);
				dev->crtc = crtc;
				return 0;
			}
		}

		drmModeFreeEncoder(enc);
	}

	/* If the connector is not currently bound to an encoder or if the
	 * encoder+crtc is already used by another connector (actually unlikely
	 * but lets be safe), iterate all other available encoders to find a
	 * matching CRTC. */
	for (i = 0; i < conn->count_encoders; ++i) {
		enc = drmModeGetEncoder(fd, conn->encoders[i]);
		if (!enc) {
			fprintf(stderr, "cannot retrieve encoder %u:%u (%d): %m\n",
				i, conn->encoders[i], errno);
			continue;
		}

		/* iterate all global CRTCs */
		for (j = 0; j < res->count_crtcs; ++j) {
			/* check whether this CRTC works with the encoder */
			if (!(enc->possible_crtcs & (1 << j)))
				continue;

			/* check that no other device already uses this CRTC */
			crtc = res->crtcs[j];
			for (iter = modeset_list; iter; iter = iter->next) {
				if (iter->crtc == crtc) {
					crtc = -1;
					break;
				}
			}

			/* we have found a CRTC, so save it and return */
			if (crtc >= 0) {
				drmModeFreeEncoder(enc);
				dev->crtc = crtc;
				return 0;
			}
		}

		drmModeFreeEncoder(enc);
	}

	fprintf(stderr, "cannot find suitable CRTC for connector %u\n",
		conn->connector_id);
	return -ENOENT;
}

/*
 * modeset_create_fb(fd, dev): After we have found a crtc+connector+mode
 * combination, we need to actually create a suitable framebuffer that we can
 * use with it. There are actually two ways to do that:
 *   * We can create a so called "dumb buffer". This is a buffer that we can
 *     memory-map via mmap() and every driver supports this. We can use it for
 *     unaccelerated software rendering on the CPU.
 *   * We can use libgbm to create buffers available for hardware-acceleration.
 *     libgbm is an abstraction layer that creates these buffers for each
 *     available DRM driver. As there is no generic API for this, each driver
 *     provides its own way to create these buffers.
 *     We can then use such buffers to create OpenGL contexts with the mesa3D
 *     library.
 * We use the first solution here as it is much simpler and doesn't require any
 * external libraries. However, if you want to use hardware-acceleration via
 * OpenGL, it is actually pretty easy to create such buffers with libgbm and
 * libEGL. But this is beyond the scope of this document.
 *
 * So what we do is requesting a new dumb-buffer from the driver. We specify the
 * same size as the current mode that we selected for the connector.
 * Then we request the driver to prepare this buffer for memory mapping. After
 * that we perform the actual mmap() call. So we can now access the framebuffer
 * memory directly via the dev->map memory map.
 */

static int modeset_create_fb(int fd, struct modeset_dev *dev)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_destroy_dumb dreq;
	struct drm_mode_map_dumb mreq;
	int ret;

	/* create dumb buffer */
	memset(&creq, 0, sizeof(creq));
	creq.width = dev->width;
	creq.height = dev->height;
	creq.bpp = 32;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
	if (ret < 0) {
		fprintf(stderr, "cannot create dumb buffer (%d): %m\n",
			errno);
		return -errno;
	}
	dev->stride = creq.pitch;
	dev->size = creq.size;
	dev->handle = creq.handle;

	enum Steps {create_framebuffer, prepare_buffer, memory_map, clear_buffer, err_fb, err_destroy, success};
	Steps step = create_framebuffer;
	switch(step)
	{
		/* create framebuffer object for the dumb-buffer */
		case create_framebuffer: 
			ret = drmModeAddFB(fd, dev->width, dev->height, 24, 32, dev->stride,
					dev->handle, &dev->fb);
			if (ret) {
				fprintf(stderr, "cannot create framebuffer (%d): %m\n",
					errno);
				ret = -errno;
				step = err_destroy;
			}
			else { step = prepare_buffer; }

		/* prepare buffer for memory mapping */
		case prepare_buffer:
			memset(&mreq, 0, sizeof(mreq));
			mreq.handle = dev->handle;
			ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
			if (ret) {
				fprintf(stderr, "cannot map dumb buffer (%d): %m\n",
					errno);
				ret = -errno;
				step = err_fb;
			}
			else { step = memory_map; }

		/* perform actual memory mapping */
		case memory_map: 
			dev->map = static_cast<uint8_t*>(mmap(0, dev->size, PROT_READ | PROT_WRITE, MAP_SHARED,
						fd, mreq.offset));
			if (dev->map == MAP_FAILED) {
				fprintf(stderr, "cannot mmap dumb buffer (%d): %m\n",
					errno);
				ret = -errno;
				step = err_fb;
			}
			else {step = clear_buffer; }

		/* clear the framebuffer to 0 */
		case clear_buffer: 
			memset(dev->map, 0, dev->size);
			step = success; 
			break; 

		case err_fb: 
			drmModeRmFB(fd, dev->fb);
		case err_destroy:
			memset(&dreq, 0, sizeof(dreq));
			dreq.handle = dev->handle;
			drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
			return ret;
	}

	return 0;
}

static void requestComplete(libcamera::Request *request)
{
    // Code to follow
    if (request->status() == libcamera::Request::RequestCancelled)
	return;

    struct modeset_dev* iter;
    const std::map<const libcamera::Stream *, libcamera::FrameBuffer *> &buffers = request->buffers();

    for (auto bufferPair : buffers) {
	libcamera::FrameBuffer *buffer = bufferPair.second;
        const libcamera::FrameMetadata &metadata = buffer->metadata();
	std::cout << " seq: " << std::setw(6) << std::setfill('0') << metadata.sequence << " bytesused: ";

	unsigned int nplane = 0;
	for (const libcamera::FrameMetadata::Plane &plane : metadata.planes())
	{
	    std::cout << plane.bytesused;
	    if (++nplane < metadata.planes().size()) std::cout << "/";
	}

	for (const libcamera::FrameBuffer::Plane &plane : buffer->planes()) {
	    if (!plane.fd.isValid())
	    {
		    break;
	    }
	    int fd = plane.fd.get();
	    unsigned char * addr = (unsigned char *) mmap(0, plane.length, PROT_READ, MAP_PRIVATE, fd, 0);
	    if (addr == MAP_FAILED) {
	    	    std::cout << "Map Failed" << std::endl;
	    }
	    for (iter = modeset_list; iter; iter = iter->next) {
	    	//int rtn = read(plane.fd.get(),&iter->map,plane.length);
			//glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    			glViewport(0, 0, test_width, test_height); // Set viewport to match texture size
    			//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Clear buffers
			//glBindFramebuffer(GL_FRAMEBUFFER, 0);
			// Load recent image as texture 
			//glViewport(0,0,480,640);
			//glActiveTexture(GL_TEXTURE0);
			std::vector<unsigned char> raw_data(test_width*test_height*4);
			memcpy(raw_data.data(),addr,plane.length);
			glBindTexture(GL_TEXTURE_2D, test_texture);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, test_width, test_height, GL_RGBA, GL_UNSIGNED_BYTE, raw_data.data());

			// Bind textures
			glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
			//glActiveTexture(GL_TEXTURE1);
			//glBindTexture(GL_TEXTURE_3D, lut_texture);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			//std::cout << "after binding textures: " << glGetError() << std::endl;

			// Draw
			glViewport(0,0,test_width,test_height);
			glClear(GL_COLOR_BUFFER_BIT);
			glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
			//std::cout << "after drawing: " << glGetError() << std::endl;

			// Read pixels
			std::vector<unsigned char> pixels(480*640* 4);
			glReadPixels(0, 0, 480, 640, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

			std::cout << " | buffer size: " << sizeof(addr) << std::endl;
			memcpy(&iter->map[0],pixels.data(),pixels.size());
			//memcpy(&iter->map[0],addr,plane.length);
			//std::cout << rtn << std::endl;

	    }
	    //munmap(addr, plane.length);
	}
	std::cout << std::endl;

   	request->reuse(libcamera::Request::ReuseBuffers);
	camera->queueRequest(request); //NOTE: uncomment to make request happen each time 
    }
}

static drmModeConnector *getConnector(drmModeRes *resources)
{
    for (int i = 0; i < resources->count_connectors; i++)
    {
        drmModeConnector *connector = drmModeGetConnector(device, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED)
        {
            return connector;
        }
        drmModeFreeConnector(connector);
    }

    return NULL;
}

static drmModeEncoder *findEncoder(drmModeConnector *connector)
{
    if (connector->encoder_id)
    {
        return drmModeGetEncoder(device, connector->encoder_id);
    }
    return NULL;
}

static int getDisplay(EGLDisplay *display)
{
    drmModeRes *resources = drmModeGetResources(device);
    if (resources == NULL)
    {
        fprintf(stderr, "Unable to get DRM resources\n");
        return -1;
    }

    drmModeConnector *connector = getConnector(resources);
    if (connector == NULL)
    {
        fprintf(stderr, "Unable to get connector\n");
        drmModeFreeResources(resources);
        return -1;
    }

    connectorId = connector->connector_id;
    mode = connector->modes[0];
    printf("resolution: %ix%i\n", mode.hdisplay, mode.vdisplay);

    drmModeEncoder *encoder = findEncoder(connector);
    if (encoder == NULL)
    {
        fprintf(stderr, "Unable to get encoder\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        return -1;
    }

    crtc = drmModeGetCrtc(device, encoder->crtc_id);
    drmModeFreeEncoder(encoder);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    gbmDevice = gbm_create_device(device);
    gbmSurface = gbm_surface_create(gbmDevice, mode.hdisplay, mode.vdisplay, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    *display = eglGetDisplay(gbmDevice);
    return 0;
}

static int matchConfigToVisual(EGLDisplay display, EGLint visualId, EGLConfig *configs, int count)
{
    EGLint id;
    for (int i = 0; i < count; ++i)
    {
        if (!eglGetConfigAttrib(display, configs[i], EGL_NATIVE_VISUAL_ID, &id))
            continue;
        if (id == visualId)
            return i;
    }
    return -1;
}


static void gbmClean()
{
    // set the previous crtc
    drmModeSetCrtc(device, crtc->crtc_id, crtc->buffer_id, crtc->x, crtc->y, &connectorId, 1, &crtc->mode);
    drmModeFreeCrtc(crtc);

    if (previousBo)
    {
        drmModeRmFB(device, previousFb);
        gbm_surface_release_buffer(gbmSurface, previousBo);
    }

    gbm_surface_destroy(gbmSurface);
    gbm_device_destroy(gbmDevice);
}

// Get the EGL error back as a string. Useful for debugging.
static const char *eglGetErrorStr()
{
    switch (eglGetError())
    {
    case EGL_SUCCESS:
        return "The last function succeeded without error.";
    case EGL_NOT_INITIALIZED:
        return "EGL is not initialized, or could not be initialized, for the "
               "specified EGL display connection.";
    case EGL_BAD_ACCESS:
        return "EGL cannot access a requested resource (for example a context "
               "is bound in another thread).";
    case EGL_BAD_ALLOC:
        return "EGL failed to allocate resources for the requested operation.";
    case EGL_BAD_ATTRIBUTE:
        return "An unrecognized attribute or attribute value was passed in the "
               "attribute list.";
    case EGL_BAD_CONTEXT:
        return "An EGLContext argument does not name a valid EGL rendering "
               "context.";
    case EGL_BAD_CONFIG:
        return "An EGLConfig argument does not name a valid EGL frame buffer "
               "configuration.";
    case EGL_BAD_CURRENT_SURFACE:
        return "The current surface of the calling thread is a window, pixel "
               "buffer or pixmap that is no longer valid.";
    case EGL_BAD_DISPLAY:
        return "An EGLDisplay argument does not name a valid EGL display "
               "connection.";
    case EGL_BAD_SURFACE:
        return "An EGLSurface argument does not name a valid surface (window, "
               "pixel buffer or pixmap) configured for GL rendering.";
    case EGL_BAD_MATCH:
        return "Arguments are inconsistent (for example, a valid context "
               "requires buffers not supplied by a valid surface).";
    case EGL_BAD_PARAMETER:
        return "One or more argument values are invalid.";
    case EGL_BAD_NATIVE_PIXMAP:
        return "A NativePixmapType argument does not refer to a valid native "
               "pixmap.";
    case EGL_BAD_NATIVE_WINDOW:
        return "A NativeWindowType argument does not refer to a valid native "
               "window.";
    case EGL_CONTEXT_LOST:
        return "A power management event has occurred. The application must "
               "destroy all contexts and reinitialise OpenGL ES state and "
               "objects to continue rendering.";
    default:
        break;
    }
    return "Unknown error!";
}

// The following are GLSL shaders for rendering a triangle on the screen
//#define STRINGIFY(x) #x
static const char *vertexShaderCode = R"(
#version 300 es
in vec2 aPos;
in vec2 aTexCoord;
out vec2 TexCoord;
void main() {
    TexCoord = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char *fragmentShaderCode = R"(
#version 300 es
precision highp float;
precision highp sampler3D;
uniform sampler2D image;
uniform sampler3D clut;
in vec2 TexCoord;
out vec4 fragColor;

void main()
{
    vec4 color;
    color = texture(image, TexCoord);
    color = texture(clut, color.rgb);
    fragColor = color;
}
)";


// The following code was adopted from
// https://github.com/matusnovak/rpi-opengl-without-x/blob/master/triangle.c
// and is licensed under the Unlicense.
static const EGLint configAttribs[] = {
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_DEPTH_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE};

static const EGLint contextAttribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE};



/*
 * Finally! We have a connector with a suitable CRTC. We know which mode we want
 * to use and we have a framebuffer of the correct size that we can write to.
 * There is nothing special left to do. We only have to program the CRTC to
 * connect each new framebuffer to each selected connector for each combination
 * that we saved in the global modeset_list.
 * This is done with a call to drmModeSetCrtc().
 *
 * So we are ready for our main() function. First we check whether the user
 * specified a DRM device on the command line, otherwise we use the default
 * /dev/dri/card0. Then we open the device via modeset_open(). modeset_prepare()
 * prepares all connectors and we can loop over "modeset_list" and call
 * drmModeSetCrtc() on every CRTC/connector combination.
 *
 * But printing empty black pages is boring so we have another helper function
 * modeset_draw() that draws some colors into the framebuffer for 5 seconds and
 * then returns. And then we have all the cleanup functions which correctly free
 * all devices again after we used them. All these functions are described below
 * the main() function.
 *
 * As a side note: drmModeSetCrtc() actually takes a list of connectors that we
 * want to control with this CRTC. We pass only one connector, though. As
 * explained earlier, if we used multiple connectors, then all connectors would
 * have the same controlling framebuffer so the output would be cloned. This is
 * most often not what you want so we avoid explaining this feature here.
 * Furthermore, all connectors will have to run with the same mode, which is
 * also often not guaranteed. So instead, we only use one connector per CRTC.
 *
 * Before calling drmModeSetCrtc() we also save the current CRTC configuration.
 * This is used in modeset_cleanup() to restore the CRTC to the same mode as was
 * before we changed it.
 * If we don't do this, the screen will stay blank after we exit until another
 * application performs modesetting itself.
 */

int main(int argc, char **argv)
{
	int ret, fd;
	const char *card;
	struct modeset_dev *iter;
	EGLDisplay display;

        /* try to open camera */
	std::unique_ptr<libcamera::CameraManager> cm = std::make_unique<libcamera::CameraManager>();
	cm->start();
	
	for (auto const &camera : cm->cameras())
	    std::cout << camera->id() << std::endl;

	auto cameras = cm->cameras();
	if (cameras.empty()) {
	    std::cout << "No cameras were identified on the system."
       	       << std::endl;
	    cm->stop();
	    return EXIT_FAILURE;
	}

	std::string cameraId = cameras[0]->id();

	camera = cm->get(cameraId);
/*
 * Note that `camera` may not compare equal to `cameras[0]`.
 * In fact, it might simply be a `nullptr`, as the particular
 * device might have disappeared (and reappeared) in the meantime.
 */
	camera->acquire();

	std::unique_ptr<libcamera::CameraConfiguration> config = camera->generateConfiguration( { libcamera::StreamRole::Viewfinder } );
	libcamera::StreamConfiguration &streamConfig = config->at(0);
	std::cout << "Default viewfinder configuration is: " << streamConfig.toString() << std::endl;
	streamConfig.size.width = 480;
	streamConfig.size.height = 640;
	config->validate();
	std::cout << "Validated viewfinder configuration is: " << streamConfig.toString() << std::endl;
	camera->configure(config.get());

	libcamera::FrameBufferAllocator *allocator = new libcamera::FrameBufferAllocator(camera);

	for (libcamera::StreamConfiguration &cfg : *config) {
	    int ret = allocator->allocate(cfg.stream());
	    if (ret < 0) {
	        std::cerr << "Can't allocate buffers" << std::endl;
	        return -ENOMEM;
	    }

	    size_t allocated = allocator->buffers(cfg.stream()).size();
	    std::cout << "Allocated " << allocated << " buffers for stream" << std::endl;
	}

	libcamera::Stream *stream = streamConfig.stream();
	const std::vector<std::unique_ptr<libcamera::FrameBuffer>> &buffers = allocator->buffers(stream);
	std::vector<std::unique_ptr<libcamera::Request>> requests;

	for (unsigned int i = 0; i < buffers.size(); ++i) {
	    std::unique_ptr<libcamera::Request> request = camera->createRequest();
	    if (!request)
	    {
	        std::cerr << "Can't create request" << std::endl;
	        return -ENOMEM;
	    }

	    const std::unique_ptr<libcamera::FrameBuffer> &buffer = buffers[i];
	    int ret = request->addBuffer(stream, buffer.get());
	    if (ret < 0)
	    {
  	        std::cerr << "Can't set buffer for request"
        	      << std::endl;
	        return ret;
	    }

	    requests.push_back(std::move(request));
	}
	camera->requestCompleted.connect(requestComplete);
	//camera->start();
	//for (std::unique_ptr<libcamera::Request> &request : requests)
	//   camera->queueRequest(request.get());

	/* check which DRM device to open */
	if (argc > 1)
		card = argv[1];
	else
		card = "/dev/dri/card1";

	fprintf(stderr, "using card '%s'\n", card);

	/* open the DRM device */
	ret = modeset_open(&fd, card);
	if (ret)
	{
		if (ret) {
			errno = -ret;
			fprintf(stderr, "modeset failed with error %d: %m\n", errno);
		} 
		else {
			fprintf(stderr, "exiting\n");
		}
		return ret;
	}

	/* prepare all connectors and CRTCs */
	ret = modeset_prepare(fd);

	if (ret) {
		close(fd);
		if (ret) {
			errno = -ret;
			fprintf(stderr, "modeset failed with error %d: %m\n", errno);
		} 
		else {
			fprintf(stderr, "exiting\n");
		}
		return ret;
	}

	/* perform actual modesetting on each found connector+CRTC */
	for (iter = modeset_list; iter; iter = iter->next) {
		iter->saved_crtc = drmModeGetCrtc(fd, iter->crtc);
		ret = drmModeSetCrtc(fd, iter->crtc, iter->fb, 0, 0,
				     &iter->conn, 1, &iter->mode);
		if (ret)
			fprintf(stderr, "cannot set CRTC for connector %u (%d): %m\n",
				iter->conn, errno);
	}


    /* OpenGL stuff */ 
    int major, minor;
    GLuint program, vert, frag;
    GLint colorLoc, result;

    device = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
    if (getDisplay(&display) != 0)
    {
        fprintf(stderr, "Unable to get EGL display\n");
        close(device);
        return -1;
    }
    if (eglInitialize(display, &major, &minor) == EGL_FALSE)
    {
        fprintf(stderr, "Failed to get EGL version! Error: %s\n",
                eglGetErrorStr());
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }
    // We will use the screen resolution as the desired width and height for the viewport.
    int desiredWidth = 2592;
    int desiredHeight = 1944;


    // Make sure that we can use OpenGL in this EGL app.
    eglBindAPI(EGL_OPENGL_API);

    printf("Initialized EGL version: %d.%d\n", major, minor);

    EGLint count;
    EGLint numConfigs;
    eglGetConfigs(display, NULL, 0, &count);
    EGLConfig *configs = static_cast<EGLConfig*>(malloc(count * sizeof(configs)));

    if (!eglChooseConfig(display, configAttribs, configs, count, &numConfigs))
    {
        fprintf(stderr, "Failed to get EGL configs! Error: %s\n",
                eglGetErrorStr());
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    // I am not exactly sure why the EGL config must match the GBM format.
    // But it works!
    int configIndex = matchConfigToVisual(display, GBM_FORMAT_XRGB8888, configs, numConfigs);
    if (configIndex < 0)
    {
        fprintf(stderr, "Failed to find matching EGL config! Error: %s\n",
                eglGetErrorStr());
        eglTerminate(display);
        gbm_surface_destroy(gbmSurface);
        gbm_device_destroy(gbmDevice);
        return EXIT_FAILURE;
    }

    EGLContext context =
        eglCreateContext(display, configs[configIndex], EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT)
    {
        fprintf(stderr, "Failed to create EGL context! Error: %s\n", eglGetErrorStr());
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    EGLSurface surface =
        eglCreateWindowSurface(display, configs[configIndex], (EGLNativeWindowType) gbmSurface, NULL);
    if (surface == EGL_NO_SURFACE)
    {
        fprintf(stderr, "Failed to create EGL surface! Error: %s\n",
                eglGetErrorStr());
        eglDestroyContext(display, context);
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    free(configs);
    eglMakeCurrent(display, surface, surface, context);

    // Set GL Viewport size, always needed!
    glViewport(0, 0, desiredWidth, desiredHeight);

    // Get GL Viewport size and test if it is correct.
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    // viewport[2] and viewport[3] are viewport width and height respectively
    printf("GL Viewport size: %dx%d\n", viewport[2], viewport[3]);

    if (viewport[2] != desiredWidth || viewport[3] != desiredHeight)
    {
        fprintf(stderr, "Error! The glViewport returned incorrect values! Something is wrong!\n");
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    // Create a shader program
    // NO ERRRO CHECKING IS DONE! (for the purpose of this example)
    // Read an OpenGL tutorial to properly implement shader creation
    std::cout << "before creating program: " << glGetError() << std::endl;
    program = glCreateProgram();
    //glUseProgram(program);
    vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vertexShaderCode, NULL);
    glCompileShader(vert);
    std::cout << "vertex shade: " << glGetError() << std::endl;
    frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fragmentShaderCode, NULL);
    glCompileShader(frag);
    std::cout << "frag shader: " << glGetError() << std::endl;
    glAttachShader(program, frag);
    glAttachShader(program, vert);
    std::cout << "attaching shaders: " << glGetError() << std::endl;
    glLinkProgram(program);
    std::cout << "linking program: " << glGetError() << std::endl;
    glUseProgram(program);

    // Create Vertex Buffer Object
    // Again, NO ERRRO CHECKING IS DONE! (for the purpose of this example)
    //glGenBuffers(1, &vbo);
    //glBindBuffer(GL_ARRAY_BUFFER, vbo);
    //glBufferData(GL_ARRAY_BUFFER, 9 * sizeof(float), vertices, GL_STATIC_DRAW);

	
    std::cout << "after using program: " << glGetError() << std::endl;
    GLint isCompiled = 0;
    glGetShaderiv(frag, GL_COMPILE_STATUS, &isCompiled);
	if(isCompiled == GL_FALSE)
	{
		GLint maxLength = 0;
		glGetShaderiv(frag, GL_INFO_LOG_LENGTH, &maxLength);

		// The maxLength includes the NULL character
		std::vector<GLchar> errorLog(maxLength);
		glGetShaderInfoLog(frag, maxLength, &maxLength, &errorLog[0]);
		std::cout << &errorLog[0] << std::endl;

		// Provide the infolog in whatever manor you deem best.
		// Exit with failure.
		glDeleteShader(frag); // Don't leak the shader.
		return 0;
	}
	// load test image  
	//int test_width, test_height, test_nrChannels;
	unsigned char *test_data = stbi_load("test.jpg", &test_width, &test_height, &test_nrChannels, 0); 
	if (!test_data)
	{
		std::cout << "Failed to load texture" << std::endl;
		return 0;
	}
	//unsigned int test_texture;
	glGenTextures(1, &test_texture); 
	glBindTexture(GL_TEXTURE_2D, test_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, test_width, test_height, 0, GL_RGB, GL_UNSIGNED_BYTE, test_data);
	//glGenerateMipmap(GL_TEXTURE_2D);
	stbi_image_free(test_data);

	// load lut texture 
	int lut_width, lut_height, lut_depth, lut_nrChannels;
	unsigned char *lut_data = stbi_load("Fuji Velvia 50.png", &lut_width, &lut_height, &lut_nrChannels, 0); 
	if (!lut_data)
	{
		std::cout << "Failed to load texture" << std::endl;
		return 0;
	}
	//unsigned int lut_texture;
	glGenTextures(1, &lut_texture); 
	glBindTexture(GL_TEXTURE_3D, lut_texture);
	glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB, 144, 144, 144, 0, GL_RGB, GL_UNSIGNED_BYTE, lut_data);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	//glGenerateMipmap(GL_TEXTURE_2D);
	stbi_image_free(lut_data);

    std::cout << "before setting uniforms: " << glGetError() << std::endl;
	// Set uniforms
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    glUniform1i(glGetUniformLocation(program, "clut"), 1);

    std::cout << "after setting uniforms: " << glGetError() << std::endl;

	// create fbo bound to image
	//unsigned int fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo); 
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, test_texture, 0);  
	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		std::cout << "framebuffer not complete" << std::endl;
		return 0;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0); 

	// create fbo bound to output 
	//unsigned int dstFBO, dstTex;
    glGenTextures(1, &dstTex);
    glBindTexture(GL_TEXTURE_2D, dstTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, test_width, test_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &dstFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
        std::cerr << "Destination FBO incomplete!\n";
		return 0;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	

    // Setup full screen quad
    float quad[] = {
        -1, -1, 0, 0,
         1, -1, 1, 0,
         1,  1, 1, 1,
        -1,  1, 0, 1
    };

	// run GL program?
	GLuint vao,vbo;
    glGenVertexArrays(1,&vao);
    glBindVertexArray(vao);
    glGenBuffers(1,&vbo);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    GLint posLoc = glGetAttribLocation(program,"aPos");
    GLint uvLoc = glGetAttribLocation(program,"aTexCoord");
    glVertexAttribPointer(posLoc,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(uvLoc,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glEnableVertexAttribArray(uvLoc);

    std::cout << "after running gL program: " << glGetError() << std::endl;

	// Bind textures
	glViewport(0,0,test_width,test_height);
	glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, test_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, lut_texture);
    std::cout << "after binding textures: " << glGetError() << std::endl;

	// Draw
    glViewport(0,0,test_width,test_height);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    std::cout << "after drawing: " << glGetError() << std::endl;

	// Read pixels
    std::vector<unsigned char> pixels(test_width * test_height * 4);
    glReadPixels(0, 0, test_width, test_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

	// Save to PNG
    stbi_write_png("output.png", test_width, test_height, 4, pixels.data(), test_width * 4);
    std::cout << "Saved color-corrected image to output.png\n";

	camera->start();
	for (std::unique_ptr<libcamera::Request> &request : requests)
	   camera->queueRequest(request.get());
	
	std::this_thread::sleep_for(std::chrono::seconds(5));

	// assume that there's a new image in frame (this is bad )
	/*struct modeset_dev *iter;

	for (iter = modeset_list; iter; iter = iter->next) {
	    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, )
		iter->map[0];
		memcpy(&iter->map[0],addr,plane.length);

	}*/

	/* cleanup everything */
	modeset_cleanup(fd);

	camera->stop();
	allocator->free(stream);
	delete allocator;
	camera->release();
	camera.reset();
	cm->stop();


	ret = 0;

	close(fd);
	if (ret) {
		errno = -ret;
		fprintf(stderr, "modeset failed with error %d: %m\n", errno);
	} else {
		fprintf(stderr, "exiting\n");
	}
	return ret;
}


/*
 * modeset_cleanup(fd): This cleans up all the devices we created during
 * modeset_prepare(). It resets the CRTCs to their saved states and deallocates
 * all memory.
 * It should be pretty obvious how all of this works.
 */

static void modeset_cleanup(int fd)
{
	struct modeset_dev *iter;
	struct drm_mode_destroy_dumb dreq;

	while (modeset_list) {
		/* remove from global list */
		iter = modeset_list;
		modeset_list = iter->next;

		/* restore saved CRTC configuration */
		drmModeSetCrtc(fd,
			       iter->saved_crtc->crtc_id,
			       iter->saved_crtc->buffer_id,
			       iter->saved_crtc->x,
			       iter->saved_crtc->y,
			       &iter->conn,
			       1,
			       &iter->saved_crtc->mode);
		drmModeFreeCrtc(iter->saved_crtc);

		/* unmap buffer */
		munmap(iter->map, iter->size);

		/* delete framebuffer */
		drmModeRmFB(fd, iter->fb);

		/* delete dumb buffer */
		memset(&dreq, 0, sizeof(dreq));
		dreq.handle = iter->handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);

		/* free allocated memory */
		free(iter);
	}
}

/*
 * I hope this was a short but easy overview of the DRM modesetting API. The DRM
 * API offers much more capabilities including:
 *  - double-buffering or tripple-buffering (or whatever you want)
 *  - vsync'ed page-flips
 *  - hardware-accelerated rendering (for example via OpenGL)
 *  - output cloning
 *  - graphics-clients plus authentication
 *  - DRM planes/overlays/sprites
 *  - ...
 * If you are interested in these topics, I can currently only redirect you to
 * existing implementations, including:
 *  - plymouth (which uses dumb-buffers like this example; very easy to understand)
 *  - kmscon (which uses libuterm to do this)
 *  - wayland (very sophisticated DRM renderer; hard to understand fully as it
 *             uses more complicated techniques like DRM planes)
 *  - xserver (very hard to understand as it is split across many files/projects)
 *
 * But understanding how modesetting (as described in this document) works, is
 * essential to understand all further DRM topics.
 *
 * Any feedback is welcome. Feel free to use this code freely for your own
 * documentation or projects.
 *
 *  - Hosted on http://github.com/dvdhrm/docs
 *  - Written by David Rheinsberg <david.rheinsberg@gmail.com>
 */
