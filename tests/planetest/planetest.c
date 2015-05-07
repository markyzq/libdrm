#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
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

static int terminate = 0;
struct plane_inc {
	int x_inc;
	int y_inc;
	int x;
	int y;
};

static void sigint_handler(int arg)
{
	terminate = 1;
}

static void incrementor(int *inc, int *val, int increment, int lower, int upper)
{
	if(*inc > 0)
		*inc = *val + increment >= upper ? -1 : 1;
	else
		*inc = *val - increment <= lower ? 1 : -1;
	*val += *inc * increment;
}

int main(int argc, char *argv[])
{
	int ret, i, j, num_test_planes;
	uint32_t plane_w = 128, plane_h = 128;
	struct sp_dev *dev;
	struct sp_plane **plane = NULL;
	struct sp_crtc *test_crtc;
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

	inc = calloc(num_test_planes, sizeof(*inc));
	for (j = 0; j < num_test_planes; j++) {
		inc[j] = calloc(1, sizeof(*inc));
		inc[j]->x_inc = 0;
		inc[j]->y_inc = 0;
		inc[j]->x = 0;
		inc[j]->y = 0;
	}

	while (!terminate) {
		for (j = 0; j < num_test_planes; j++) {
			incrementor(&inc[j]->x_inc, &inc[j]->x, 5 + j, 0,
				    test_crtc->crtc->mode.hdisplay - plane_w);
			incrementor(&inc[j]->y_inc, &inc[j]->y, 5 + j * 5, 0,
				    test_crtc->crtc->mode.vdisplay - plane_h);
			if (inc[j]->x + plane_w > test_crtc->crtc->mode.hdisplay)
				inc[j]->x = test_crtc->crtc->mode.hdisplay - plane_w;
			if (inc[j]->y + plane_h > test_crtc->crtc->mode.vdisplay)
				inc[j]->y = test_crtc->crtc->mode.vdisplay - plane_h;

			ret = set_sp_plane(dev, plane[j], test_crtc,
					   inc[j]->x, inc[j]->y);
			if (ret) {
				printf("failed to set plane %d %d\n", j, ret);
				goto out;
			}
		}
		usleep(15 * 1000);
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
