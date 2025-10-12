#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <drm_fourcc.h>

int drm_fd;
drmModeConnector *connector;
drmModeEncoder *encoder;
drmModeCrtc *crtc;
uint32_t connector_id, crtc_id;
uint32_t fb_id;
uint32_t *framebuffer_map;

int init_drm() {
    drmModeRes *resources;
    int i;

    // Open the DRM device
    drm_fd = open("/dev/dri/card1", O_RDWR | O_NONBLOCK);
    if (drm_fd < 0) {
        perror("Unable to open DRM device");
        return -1;
    }

    // Get DRM resources (connectors, encoders, CRTCs)
    resources = drmModeGetResources(drm_fd);
    if (!resources) {
        perror("Unable to get DRM resources");
        close(drm_fd);
        return -1;
    }

    // Find a connected connector
    for (i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(drm_fd, resources->connectors[i]);
        if (connector && connector->connection == DRM_MODE_CONNECTED) {
            connector_id = connector->connector_id;
            break;
        }
        drmModeFreeConnector(connector);
    }
    if (!connector) {
        perror("Unable to find a connected connector");
        drmModeFreeResources(resources);
        close(drm_fd);
        return -1;
    }

    // Find the encoder connected to the connector
    for (i = 0; i < resources->count_encoders; i++) {
        encoder = drmModeGetEncoder(drm_fd, resources->encoders[i]);
        if (encoder && encoder->encoder_id == connector->encoder_id) {
            break;
        }
        drmModeFreeEncoder(encoder);
    }
    if (!encoder) {
        perror("Unable to find an encoder");
        drmModeFreeResources(resources);
        drmModeFreeConnector(connector);
        close(drm_fd);
        return -1;
    }

    // Get the CRTC associated with the encoder
    crtc = drmModeGetCrtc(drm_fd, encoder->crtc_id);
    if (!crtc) {
        perror("Unable to get CRTC");
        drmModeFreeResources(resources);
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        close(drm_fd);
        return -1;
    }
    crtc_id = crtc->crtc_id;

    // Add a framebuffer (simple example, using XRGB8888 format)
    uint32_t width = crtc->mode.hdisplay;
    uint32_t height = crtc->mode.vdisplay;
    uint32_t pitch = width * 4; // 4 bytes per pixel for XRGB8888
    uint32_t handle;
    uint32_t size = pitch * height;

    if (drmModeCreateDumbBuffer(drm_fd, width, height, 32, 0, &handle, &pitch, &size)) {
        perror("Failed to create dumb buffer");
        // ... cleanup ...
        return -1;
    }

    if (drmModeAddFB(drm_fd, width, height, 24, 32, pitch, handle, &fb_id)) {
        perror("Failed to add framebuffer");
        // ... cleanup ...
        return -1;
    }

    // Map the framebuffer to user space
    struct drm_mode_map_dumb map_dumb = { .handle = handle };
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb)) {
        perror("Failed to map dumb buffer");
        // ... cleanup ...
        return -1;
    }
    framebuffer_map = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, map_dumb.offset);
    if (framebuffer_map == MAP_FAILED) {
        perror("Failed to mmap framebuffer");
        // ... cleanup ...
        return -1;
    }

    drmModeFreeResources(resources);
    return 0;
}

void write_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (framebuffer_map) {
        uint32_t color = (r << 16) | (g << 8) | b;
        framebuffer_map[y * crtc->mode.hdisplay + x] = color;
    }
}

void cleanup() {
    if (framebuffer_map != MAP_FAILED) {
        munmap(framebuffer_map, crtc->mode.hdisplay * crtc->mode.vdisplay * 4); // Assuming XRGB8888
    }
    if (fb_id) {
        drmModeRmFB(drm_fd, fb_id);
    }
    if (crtc) {
        drmModeFreeCrtc(crtc);
    }
    if (encoder) {
        drmModeFreeEncoder(encoder);
    }
    if (connector) {
        drmModeFreeConnector(connector);
    }
    if (drm_fd >= 0) {
        close(drm_fd);
    }
}

int main() {
    if (init_drm() < 0) {
        fprintf(stderr, "Failed to initialize DRM\n");
        return 1;
    }

    // Set the CRTC to use our new framebuffer
    drmModeSetCrtc(drm_fd, crtc_id, fb_id, 0, 0, &connector_id, 1, &crtc->mode);

    // Draw a red rectangle
    for (int y = 50; y < 150; y++) {
        for (int x = 50; x < 150; x++) {
            write_pixel(x, y, 255, 0, 0); // Red
        }
    }

    printf("Displaying red rectangle. Press Enter to exit.\n");
    getchar(); // Wait for user input

    // Restore original CRTC if needed (not implemented in this simple example)
    // drmModeSetCrtc(drm_fd, crtc_id, original_fb_id, original_x, original_y, &connector_id, 1, &original_mode);

    cleanup();
    return 0;
}
