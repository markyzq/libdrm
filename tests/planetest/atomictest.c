#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include <xf86drm.h>

#include "dev.h"
#include "bo.h"
#include "modeset.h"

struct vbl_info {
	unsigned int vbl_count;
	struct timeval start;
};

struct plane_inc {
	int x_inc;
	int y_inc;
	int x;
	int y;
};



static int terminate = 0;

static void sigint_handler(int arg)
{
	terminate = 1;
}

static void vblank_handler(int fd, unsigned int frame, unsigned int sec,
			   unsigned int usec, void *data)
{
	drmVBlank vbl;
	struct timeval end;
	struct vbl_info *info = data;
	double t;

	vbl.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
	vbl.request.sequence = 1;
	vbl.request.signal = (unsigned long)data;

	drmWaitVBlank(fd, &vbl);

	info->vbl_count++;

	if (info->vbl_count == 60) {
		gettimeofday(&end, NULL);
		t = end.tv_sec + end.tv_usec * 1e-6 -
			(info->start.tv_sec + info->start.tv_usec * 1e-6);
		printf("freq: %.02fHz\n", info->vbl_count / t);
		info->vbl_count = 0;
		info->start = end;
	}
}

static void incrementor(int *inc, int *val, int increment, int lower, int upper)
{
	if(*inc > 0)
		*inc = *val + increment >= upper ? -1 : 1;
	else
		*inc = *val - increment <= lower ? 1 : -1;
	*val += *inc * increment;
}

static void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
				  unsigned int tv_usec, void *user_data)
{
	//printf("--->yzq %s %d\n",__func__,__LINE__);
}

int main(int argc, char *argv[])
{
	uint32_t plane_w = 512, plane_h = 128;
	int ret, i, j, num_test_planes;
	struct sp_dev *dev;
	struct sp_plane **plane = NULL;
	struct sp_crtc *test_crtc;
	drmModeAtomicReqPtr pset;
	struct vbl_info handler_info;
	drmVBlank vbl;
	drmEventContext event_context = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.vblank_handler = vblank_handler,
		.page_flip_handler = page_flip_handler,
	};
	struct plane_inc **inc = NULL;

	signal(SIGINT, sigint_handler);

	dev = create_sp_dev();
	if (!dev) {
		printf("Failed to create sp_dev\n");
		return -1;
	}

	ret = initialize_screens(dev);
	if (ret) {
		printf("Failed to initialize screens\n");
		goto out;
	}
	test_crtc = &dev->crtcs[0];

	plane = calloc(dev->num_planes, sizeof(*plane));
	if (!plane) {
		printf("Failed to allocate plane array\n");
		goto out;
	}

	/* Create our planes */
	num_test_planes = test_crtc->num_planes;
	for (i = 0; i < num_test_planes; i++) {
		plane[i] = get_sp_plane(dev, test_crtc);
		if (!plane[i]) {
			printf("no unused planes available\n");
			goto out;
		}

		plane[i]->bo = create_sp_bo(dev, plane_w, plane_h, 16, 32,
				plane[i]->format, 0);
		if (!plane[i]->bo) {
			printf("failed to create plane bo\n");
			goto out;
		}

		if (i == 0)
			fill_bo(plane[i]->bo, 0xFF, 0xFF, 0xFF, 0xFF);
		else if (i == 1)
			fill_bo(plane[i]->bo, 0xFF, 0x00, 0xFF, 0xFF);
		else if (i == 2)
			fill_bo(plane[i]->bo, 0xFF, 0x00, 0x00, 0xFF);
		else if (i == 3)
			fill_bo(plane[i]->bo, 0xFF, 0xFF, 0x00, 0x00);
		else
			fill_bo(plane[i]->bo, 0xFF, 0x00, 0xFF, 0x00);
	}

	/* Get current count first */
	vbl.request.type = DRM_VBLANK_RELATIVE;
	vbl.request.sequence = 0;
	ret = drmWaitVBlank(dev->fd, &vbl);
	if (ret != 0) {
		printf("drmWaitVBlank (relative) failed ret: %i\n", ret);
		return -1;
	}

	printf("starting count: %d\n", vbl.request.sequence);

	handler_info.vbl_count = 0;
	gettimeofday(&handler_info.start, NULL);

	/* Queue an event for frame + 1 */
	vbl.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
	vbl.request.sequence = 1;
	vbl.request.signal = (unsigned long)&handler_info;
	ret = drmWaitVBlank(dev->fd, &vbl);
	if (ret != 0) {
		printf("drmWaitVBlank (relative, event) failed ret: %i\n", ret);
		return -1;
	}

	printf("display size=%dx%d@%d\n", test_crtc->crtc->mode.hdisplay, test_crtc->crtc->mode.vdisplay, test_crtc->crtc->mode.vrefresh);
//	printf("--->yzq %s %d num_test_planes=%d sizeof(*inc)=%d\n",__func__,__LINE__, num_test_planes, sizeof(*inc));
	inc = calloc(num_test_planes, sizeof(*inc));

	for (j = 0; j < num_test_planes; j++) {
		inc[j] = calloc(1, sizeof(struct plane_inc));
		inc[j]->x_inc = 0;
		inc[j]->y_inc = 0;
		inc[j]->x = 0;
		inc[j]->y = 0;
	}

	while (!terminate) {
		struct timeval timeout = { .tv_sec = 3, .tv_usec = 0 };
		fd_set fds;

		pset = drmModeAtomicAlloc();
		if (!pset) {
			printf("Failed to allocate the property set\n");
			goto out;
		}

		for (j = 0; j < num_test_planes; j++) {
			incrementor(&inc[j]->x_inc, &inc[j]->x, 5 + j, 0,
				    test_crtc->crtc->mode.hdisplay - plane_w);
			incrementor(&inc[j]->y_inc, &inc[j]->y, 5 + j * 5, 0,
				    test_crtc->crtc->mode.vdisplay - plane_h);
			if (inc[j]->x + plane_w > test_crtc->crtc->mode.hdisplay)
				inc[j]->x = test_crtc->crtc->mode.hdisplay - plane_w;
			if (inc[j]->y + plane_h > test_crtc->crtc->mode.vdisplay)
				inc[j]->y = test_crtc->crtc->mode.vdisplay - plane_h;

			if (j == 0)
			set_sp_plane_pset(dev, plane[j], pset, test_crtc,
					inc[j]->x, inc[j]->y, plane[j]->bo->width * 2, plane[j]->bo->height * 1.5);
			else
			set_sp_plane_pset(dev, plane[j], pset, test_crtc,
					inc[j]->x, inc[j]->y, plane[j]->bo->width, plane[j]->bo->height);
		}

		ret = drmModeAtomicCommit(dev->fd, pset,
				DRM_MODE_PAGE_FLIP_EVENT, NULL);
		if (ret) {
			printf("failed to commit properties ret=%d\n", ret);
			drmModeAtomicFree(pset);
			goto out;
		}

		drmModeAtomicFree(pset);

		FD_ZERO(&fds);
		FD_SET(0, &fds);
		FD_SET(dev->fd, &fds);
		ret = select(dev->fd + 1, &fds, NULL, NULL, &timeout);

		if (ret <= 0) {
			fprintf(stderr, "select timed out or error (ret %d)\n",
				ret);
			continue;

		} else if (FD_ISSET(0, &fds)) {
			break;
		}
		drmHandleEvent(dev->fd, &event_context);
	}

	for (i = 0; i < num_test_planes; i++) {
		put_sp_plane(plane[i]);
		free(inc[i]);
	}

out:
	destroy_sp_dev(dev);
	free(plane);
	free(inc);
	return ret;
}
