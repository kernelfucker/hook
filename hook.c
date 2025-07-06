/* See LICENSE file for license details */
/* hook - minimal screenshot utility */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <zlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_image.h>
#include <xcb/randr.h>

#define version "0.1"

typedef struct{
	int x;
	int y;
	int w;
	int h;
} Last;

typedef struct{
	int selreg;
	int freeze;
	int cndn;
	int delay;
	int monitor;
	int show_version;
	const char *filename;
} Options;

static Options opt = {0};

static int drawing = 0;

static uint32_t crct[256];
static int crcr = 0;

void make_crct(){
	for(int i = 0; i < 256; i++){
		uint32_t c = i;
		for(int l = 0; l < 8; l++) c = (c & 1) ? 0xEDB88320 ^ (c >> 1) : c >> 1;
		crct[i] = c;
	}

	crcr = 1;
}

uint32_t last_crc32(uint32_t crc, const uint8_t *buf, size_t len){
	if(!crcr) make_crct();
	crc ^= 0xffffffff;
	for(size_t i = 0;i < len; i++) crc = crct[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
	return crc ^ 0xffffffff;
}

void write_a(FILE *f, const char *type, const uint8_t *data, size_t len){
	uint8_t lenb[4] = {
		(uint8_t)(len >> 24),
		(uint8_t)(len >> 16),
		(uint8_t)(len >> 8),
		(uint8_t)(len)
	};

	fwrite(lenb, 1, 4, f);
	fwrite(type, 1, 4, f);
	if(len) fwrite(data, 1, len, f);
	uint32_t crc = last_crc32(0, (const uint8_t *)type, 4);
	if(len) crc = last_crc32(crc, data, len);
	uint8_t crcb[4] = {
		(uint8_t)(crc >> 24),
		(uint8_t)(crc >> 16),
		(uint8_t)(crc >> 8),
		(uint8_t)(crc)
	};

	fwrite(crcb, 1, 4, f);
}

void write_c(FILE *f, uint8_t *pixs, size_t len){
	size_t buf_size = compressBound(len);
	uint8_t *buf = malloc(buf_size);
	if(!buf){
		perror("malloc");
		return;
	}

	uLongf dst_len = buf_size;
	if(compress(buf, &dst_len, pixs, len) != Z_OK){
	fprintf(stderr, "compress failed\n");
	free(buf);

	return;

	}

	fwrite(buf, 1, dst_len, f);
	free(buf);
}

void write_p(const char *filename, uint8_t *pixs, int width, int height){
	FILE *f = fopen(filename, "wb");
	if(!f){
		perror("fopen");
		return;
	}

	const uint8_t png_sig[] = {
		137, 80, 78, 71, 13, 10, 26, 10
	};

	fwrite(png_sig, 1, 8, f);
	uint8_t h[13] = {
		(uint8_t)(width >> 24),
		(uint8_t)(width >> 16),
		(uint8_t)(width >> 8),
		(uint8_t)(width),
		(uint8_t)(height >> 24),
		(uint8_t)(height >> 16),(uint8_t)(height >> 8),
		(uint8_t)(height),
		8, 2, 0, 0, 0
	};

	write_a(f, "IHDR", h, 13);
	size_t rowl = width*3 + 1;
	uint8_t *sz = malloc(height * rowl);
	for(int l = 0; l < height; l++){
		sz[l * rowl] = 0;
		memcpy(sz + l * rowl + 1, pixs + l * width * 3, width * 3);
	}

	size_t max_c = compressBound(height * rowl);
	uint8_t *c = malloc(max_c);
	uLongf c_len = max_c;
	if(compress(c, &c_len, sz, height * rowl) != Z_OK){
		fprintf(stderr, "zlib compression failed\n");
		free(sz);
		free(c);
		fclose(f);

		return;
	}

	write_a(f, "IDAT", c, c_len);
	write_a(f, "IEND", NULL, 0);
	free(sz);
	free(c);
	fclose(f);
}

void convert_pixs(xcb_image_t *img, uint8_t *out){
	for(int i = 0; i < img->width * img->height; i++){
		uint32_t l = ((uint32_t *)img->data)[i];
		out[i * 3 + 0] = (l >> 16) & 0xff;
		out[i * 3 + 1] = (l >> 8) & 0xff;
		out[i * 3 + 2] = (l) & 0xff;
	}
}

int get_monitor_r(xcb_connection_t *conn, xcb_screen_t *scrn, Last *r){
	xcb_randr_get_screen_resources_current_cookie_t resc = xcb_randr_get_screen_resources_current(conn, scrn->root);
	xcb_randr_get_screen_resources_current_reply_t *res = xcb_randr_get_screen_resources_current_reply(conn, resc, NULL);
	if(!res) return 0;
	int num_outps = xcb_randr_get_screen_resources_current_outputs_length(res);
	xcb_randr_output_t *outps = xcb_randr_get_screen_resources_current_outputs(res);
	xcb_query_pointer_cookie_t pcookie = xcb_query_pointer(conn, scrn->root);
	xcb_query_pointer_reply_t *preply = xcb_query_pointer_reply(conn, pcookie, NULL);
	if(!preply){
		free(res);
		return 0;
	}

	int px = preply->root_x;
	int py = preply->root_y;
	free(preply);
	for(int i = 0; i <num_outps; i++){
		xcb_randr_get_output_info_cookie_t ocookie = xcb_randr_get_output_info(conn, outps[i], 0);
		xcb_randr_get_output_info_reply_t *oinfo = xcb_randr_get_output_info_reply(conn, ocookie, NULL);
	if(!oinfo) continue;
	if(oinfo->connection == XCB_RANDR_CONNECTION_CONNECTED && oinfo->crtc){
		xcb_randr_get_crtc_info_cookie_t ccookie = xcb_randr_get_crtc_info(conn, oinfo->crtc, 0);
		xcb_randr_get_crtc_info_reply_t *crtc = xcb_randr_get_crtc_info_reply(conn, ccookie, NULL);
		if(crtc){
			if (px >= crtc->x && px < crtc->x + crtc->width && py >= crtc->y && py < crtc->y + crtc->height){
				r->x = crtc->x; r->y = crtc->y; r->w = crtc->width; r->h = crtc->height;
				free(crtc);
				free(oinfo);
				free(res);

				return 1;
			}

			free(crtc);
		}
	}

	free(oinfo);

	}

	free(res);

	return 0;
}

void usage(const char *hook){
	fprintf(stderr, "usage: %s [options]..\n", hook);
	fprintf(stderr, "options:\n");
	fprintf(stderr, "  -s	select region immediately\n");
	fprintf(stderr, "  -f	freeze screen when selecting, only with -s\n");
	fprintf(stderr, "  -c	countdown before screenshot\n");
	fprintf(stderr, "  -d	delay seconds before screenshot\n");
	fprintf(stderr, "  -m	screenshot current monitor only\n");
	fprintf(stderr, "  -v	show version information\n");
	fprintf(stderr, "  -h	display this\n");
	exit(EXIT_FAILURE);
}

void pargs(int argc, char **argv){
	int x;
	while((x = getopt(argc, argv, "sfcd:mvh")) != -1){
		switch(x){
			case 's': opt.selreg = 1;
			break;
			case 'f': opt.freeze = 1;
			break;
			case 'c': opt.cndn = 1;
			break;
			case 'd': opt.delay = atoi(optarg);
			break;
			case 'm': opt.monitor = 1;
			break;
			case 'v': opt.show_version = 1;
			break;
			case 'h': usage(argv[0]);
			break;
			default: usage(argv[0]);
		}
	}

	if(optind < argc){
		opt.filename = argv[optind];
	}
}

Last selreg(xcb_connection_t *conn, xcb_screen_t *scrn){
	Last r = {0, 0, 0, 0};
	xcb_window_t root = scrn->root;
	xcb_grab_pointer_cookie_t gcookie = xcb_grab_pointer(conn, 0, root, XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE|XCB_EVENT_MASK_POINTER_MOTION, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, XCB_NONE, XCB_CURRENT_TIME);
	xcb_grab_pointer_reply_t *greply = xcb_grab_pointer_reply(conn, gcookie, NULL);
	if(!greply || greply->status != XCB_GRAB_STATUS_SUCCESS){
		fprintf(stderr, "cannot grab pointer\n");
		free(greply);
		return r;
	}

	free(greply);

	int x0 = 0;
	int y0 = 0;
	int x1 = 0;
	int y1 = 0;
	xcb_generic_event_t *e;
	while((e = xcb_wait_for_event(conn))){
		uint8_t t = e->response_type & ~0x80;
		if(t == XCB_BUTTON_PRESS){
			xcb_button_press_event_t *bp = (xcb_button_press_event_t *)e;
			x0 = bp->event_x;
			y0 = bp->event_y;
			x1 = x0;
			y1 = y0;
			drawing = 1;
			free(e);
		} else if(t == XCB_MOTION_NOTIFY && drawing){
			xcb_motion_notify_event_t *mn = (xcb_motion_notify_event_t *)e;
			x1 = mn->event_x;
			y1 = mn->event_y;
			free(e);
		} else if(t == XCB_BUTTON_RELEASE && drawing){
			xcb_button_release_event_t *br = (xcb_button_release_event_t *)e;
			x1 = br->event_x;
			y1 = br->event_y;
			drawing = 0;
			free(e);
			break;
		} else {
			free(e);
		}
	}

	r.x = (x0 < x1) ? x0 : x1;
	r.y = (y0 < y1) ? y0 : y1;
	r.w = abs(x1 - x0);
	r.h = abs(y1 - y0);

	return r;
}

char *genfilename(int w, int h){
	time_t now = time(NULL);
	struct tm *tm= localtime(&now);
	char *name = malloc(256);
	snprintf(name, 256, "%04d-%02d-%02d-%02d%02d%02d-%dx%d-hook.png", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, w, h);

	return name;
}

void hook(xcb_connection_t *conn, xcb_screen_t *scrn, Last r, const char *filename){
	if(r.w == 0 || r.h == 0){
		r.x = 0; r.y = 0;
		r.w = scrn->width_in_pixels;
		r.h = scrn->height_in_pixels;
	}

	xcb_image_t *img = xcb_image_get(conn, scrn->root, r.x, r.y, r.w, r.h, 0xFFFFFFFF, XCB_IMAGE_FORMAT_Z_PIXMAP);
	if(!img){
		fprintf(stderr, "failed to get image\n");
		return;
	}

	uint8_t *rgb = malloc(r.w * r.h * 3);
	convert_pixs(img, rgb);
	write_p(filename, rgb, r.w, r.h);
	free(rgb);
	xcb_image_destroy(img);
}

int main(int argc, char **argv){
	pargs(argc, argv);
	if(opt.show_version){
		printf("hook-%s\n", version);
		return 0;
	}

	if(opt.delay > 0){
		if(opt.cndn){
			for(int i = opt.delay; i > 0; i--){
				printf("%d..\n", i);
				fflush(stdout);
				sleep(1);
			}
		} else {
			sleep(opt.delay);
		}
	}

	xcb_connection_t *conn = xcb_connect(NULL, NULL);
	if(xcb_connection_has_error(conn)){
		fprintf(stderr, "cannot open display\n");
		return 1;
	}

	xcb_screen_t *scrn = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	if(!scrn){
		fprintf(stderr, "cannot get screen\n");
		xcb_disconnect(conn);
		return 1;
	}

	Last r = {0, 0, 0, 0};
	if(opt.selreg){
		r = selreg(conn, scrn);
		if(r.w == 0 || r.h == 0){
			fprintf(stderr, "invalid selection\n");
			xcb_disconnect(conn);
			return 1;
		}
	} else if(opt.monitor){
		if(!get_monitor_r(conn, scrn, &r)){
			fprintf(stderr, "failed to get current monitor geometry\n");
			xcb_disconnect(conn);
			return 1;
		}
	}

	 if(r.w == 0 || r.h == 0){
		r.x = 0;
		r.y = 0;
		r.w = scrn->width_in_pixels;
		r.h = scrn->height_in_pixels;
	}

	char *filename = opt.filename ? strdup(opt.filename) : genfilename(r.w, r.h);
	hook(conn, scrn, r, filename);
	free(filename);
	xcb_disconnect(conn);

	return 0;
}
