#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>                /* low-level i/o */
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <semaphore.h>
#include "leptsci.h"


#define LEPTON_WIDTH 	80
#define LEPTON_HEIGHT 	60
#define FB_WIDTH 		320
#define FB_HEIGHT 		240
#define BYTES_PER_PIXEL	4

static char *v4l2dev = "/dev/video1";
static int v4l2sink = -1;

static char *vidsendbuf = NULL;
static int vidsendsiz = 0;

static unsigned char mag = FB_WIDTH / LEPTON_WIDTH;

// Manage two buffers
static unsigned short img1[LEPTON_HEIGHT*LEPTON_WIDTH];
static unsigned short img2[LEPTON_HEIGHT*LEPTON_WIDTH];
static unsigned short * front_img;

#define PRINT_TIMING_INFO 0


static int init_device() {

	if (leptopen())
		return -1;

	// TODO: Make sure to allocate and reset the Lepton using
	// GPIO here.

	return 0;
}

static void write_frame(unsigned short *img)
{
	int x, y, xb, yb;
	long int loc = 0;

	// Get minval and maxval
	unsigned short minval = 0xffff, maxval = 0;
	for (y = 0; y < LEPTON_HEIGHT*LEPTON_WIDTH; y++) {
//		for (x = 0; x < LEPTON_WIDTH; x++) {
//			if (img[y][x] > maxval)
//				maxval = img[y][x];
//			if (img[y][x] < minval)
//				minval = img[y][x];
//		}

		if (img[y] > maxval)
			maxval = img[y];
		if (img[y] < minval)
			minval = img[y];
	}
	maxval -= minval;

	// Create false color image from frame data
	for (y = 0; y < LEPTON_HEIGHT; y++) {
		for (x = 0; x < LEPTON_WIDTH; x++) {

			int b,g,r,val;
//			val = ((img[y][x] - minval) * 255) / (maxval);

			val = ((img[y*LEPTON_WIDTH + x]- minval) * 255) / maxval;
			val &= 0xff;

			switch (val >> 6) {
			case 0:
				b = 255;
				g = 0;
				r = 255 - (val << 2);
				break;
			case 1:
				r = 0;
				b = 255 - (val << 2);
				g = (val << 2);
				break;
			case 2:
				b = 0;
				g = 255;
				r = (val << 2);
				break;
			case 3:
				b = 0;
				r = 255;
				g = 255 - (val << 2);
				break;
			default:
				break;
			}

			for (yb = 0; yb < mag; yb++) {
				loc = (x * mag) * BYTES_PER_PIXEL + (yb + y * mag) * FB_WIDTH * BYTES_PER_PIXEL;
				for (xb = 0; xb < mag; xb++) {
						*(vidsendbuf + loc++) = r;
						*(vidsendbuf + loc++) = g;
						*(vidsendbuf + loc++) = b;
						*(vidsendbuf + loc++) = 0;
				}
			}
		}
	}
}

static void save_ppm_file(void)
{
	int i;
	int j;

	char image_name[32];
	static int image_index = 0;

	do {
		sprintf(image_name, "IMG_%.4d.ppm", image_index);
		image_index += 1;
		if (image_index > 9999)
		{
			image_index = 0;
			break;
		}

	} while (access(image_name, F_OK) == 0);

	FILE *f = fopen(image_name, "w");
	if (f == NULL)
	{
		printf("Error opening file!\n");
		exit(1);
	}

	fprintf(f,"P3\n%d %d\n%u\n",FB_WIDTH, FB_HEIGHT, 255);

	// For each row...
	for(i=0;i<FB_HEIGHT;i++)
	{
		// Write row to file
		for(j=0; j < FB_WIDTH*BYTES_PER_PIXEL; j+=BYTES_PER_PIXEL)
		{
			fprintf(f,"%d %d %d ",
					vidsendbuf[i*FB_WIDTH*BYTES_PER_PIXEL + j+2],
					vidsendbuf[i*FB_WIDTH*BYTES_PER_PIXEL + j+1],
					vidsendbuf[i*FB_WIDTH*BYTES_PER_PIXEL + j]);
		}
		fprintf(f,"\n");
	}
	fprintf(f,"\n\n");

	fclose(f);
}

static void stop_device() {
	leptclose();
}

static int open_vpipe()
{
	// Open device
    v4l2sink = open(v4l2dev, O_WRONLY);
    if (v4l2sink < 0) {
        fprintf(stderr, "Failed to open v4l2sink device. (%s)\n", strerror(errno));
        return -2;
    }

    // setup video for proper format
    struct v4l2_format v;
    int t;
    v.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    t = ioctl(v4l2sink, VIDIOC_G_FMT, &v);

    if( t < 0 ) {
    	printf("Could not get video format\n");
    	return t;
    }

    v.fmt.pix.width = FB_WIDTH;
    v.fmt.pix.height = FB_HEIGHT;
   	v.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB32;

    vidsendsiz = FB_WIDTH * FB_HEIGHT * BYTES_PER_PIXEL;
    v.fmt.pix.sizeimage = vidsendsiz;
    t = ioctl(v4l2sink, VIDIOC_S_FMT, &v);

    if( t < 0 ) {
    	printf("Could not set video format\n");
    	return t;
    }

    // Malloc the video buffer
    vidsendbuf = malloc( vidsendsiz );

    return 0;
}

static pthread_t sender;
static sem_t lock1;  //,lock2;

static void *sendvid(void *v)
{
    for (;;) {
        sem_wait(&lock1);

        if(front_img)
        	write_frame(front_img);

        if (vidsendsiz != write(v4l2sink, vidsendbuf, vidsendsiz))
            exit(-1);

        //sem_post(&lock2);
    }
}

int main(int argc, char **argv)
{
    if( argc == 2 )
        v4l2dev = argv[1];

    unsigned short * back_img, * temp_img;
    int thread_handle;

    // Setup buffers
    back_img = (unsigned short*)img1;
    front_img = (unsigned short*)img2;

    struct timespec ts_capture_start, ts_capture_end, ts_frame_start, ts_frame_end;

    // Open Lepton
    if(init_device()){
    	printf("Could not open Lepton\n");
    	exit(-1);
    }

    printf("   Opened Lepton...\n");

    if(open_vpipe()){
    	printf("Could not properly open video pipe\n");
    	exit(-1);
    }

    printf("   Opened vpipe...\n");

    printf("   Starting loop...\n");

    // open and lock response
    //if (sem_init(&lock2, 0, 1) == -1)
     //   exit(-1);

    // Initialize semaphore to notify thread that
    // an image is ready for processing and sending
    if (sem_init(&lock1, 0, 0) == -1) {
    	fprintf(stderr, "Could not initialize thread sync semaphore\n");
        exit(-1);
    }

   thread_handle = pthread_create(&sender, NULL, sendvid, NULL);

    for (;;) {
        for (;;) {

        	// Capture start
        	clock_gettime(CLOCK_MONOTONIC_RAW, &ts_capture_start);

        	// Grab image from sensor
        	if (leptget(back_img)) {
        		printf("Failed to get data from Lepton\n");
        		return -1;
        	}

        	// Capture end
            clock_gettime(CLOCK_MONOTONIC_RAW, &ts_capture_end);

            // Swap front/back buffer pointers
            //sem_wait(&lock2);

            temp_img  = back_img;
            back_img  = front_img;
            front_img = temp_img;

            sem_post(&lock1);

            if(PRINT_TIMING_INFO) {
				printf("main: Image capture: %f\n",
						time_subtract(ts_capture_start, ts_capture_end)*(double)1e6);
            }

            /*
            clock_gettime(CLOCK_MONOTONIC_RAW, &ts_frame_start);

            clock_gettime(CLOCK_MONOTONIC_RAW, &ts_frame_end);

            printf("Total: %f usecs, image capture: %f, frame gen: %f, video write: %f\n",
            		time_subtract(ts_capture_start, ts_frame_end)*(double)1e6,
            		time_subtract(ts_capture_start, ts_capture_end)*(double)1e6,
            		time_subtract(ts_capture_end, ts_frame_start)*(double)1e6,
        			time_subtract(ts_frame_start, ts_frame_end)*(double)1e6);
        			*/
        }
    }

    close(v4l2sink);
    free(vidsendbuf);
    stop_device(); // close SPI
    return 0;
}
