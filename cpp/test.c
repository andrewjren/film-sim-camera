#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int main() {
drmModeRes *resources;
int fd = open("/dev/dri/card0", O_RDWR);
resources = drmModeGetResources(fd);
} 
