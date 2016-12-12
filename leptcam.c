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
#include "palettes.h"


#define LEPTON_WIDTH 	80
#define LEPTON_HEIGHT 	60
#define FB_WIDTH 		320
#define FB_HEIGHT 		240
#define BYTES_PER_PIXEL	4
//#define PALETTE_IRONBLACK  // Uncomment to use the rainbow palette
//#define FIND_MINMAX    // Uncomment to use automatic min/max image color mapping

// Defined min and max values that seem to work well
// in seeing body heat
#define MINVAL_RAINBOW			7700
#define MAXVAL_RAINBOW			8300
#define MINVAL_IRONBLACK		7600
#define MAXVAL_IRONBLACK		8300

#define COLORMAP_RED_INDEX   0
#define COLORMAP_GREEN_INDEX 1
#define COLORMAP_BLUE_INDEX  2

#ifdef PALETTE_IRONBLACK
const int *colormap = colormap_ironblack;
int g_maxval = MAXVAL_IRONBLACK;
int g_minval = MINVAL_IRONBLACK;
#else
const int *colormap = colormap_rainbow;
int g_maxval = MAXVAL_RAINBOW;
int g_minval = MINVAL_RAINBOW;
#endif

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

int exit_flag = 0;

/* SIGINT Handler */
static void sigint_restore(void);
static void sigint_handler(int signum) {
	printf("\nCaught interrupt -- \n");
	sigint_restore();

	/* Set the exit flag to 1 to exit the main loop */
	exit_flag = 1;
}

/* Interrupt signal setup */
static void sigint_setup(void) {
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_handler = sigint_handler;

	sigaction(SIGINT, &action, NULL );
}

/* Restore default interrupt signal handler */
static void sigint_restore(void) {
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_handler = SIG_DFL;

	sigaction(SIGINT, &action, NULL );
}

static int init_device() {

	if (leptopen())
		return -1;

	// TODO: Allocate and reset the Lepton using
	// GPIO here.

	return 0;
}

static void write_frame(unsigned short *img)
{
	int x, y, xb, yb;
	long int loc = 0;
	int b,g,r,val;

	// Get minval and maxval
	unsigned short minval = 0xffff, maxval = 0;

#ifdef FIND_MINMAX
	for (y = 0; y < LEPTON_HEIGHT*LEPTON_WIDTH; y++) {
		if (img[y] > maxval)
			maxval = img[y];
		if (img[y] < minval)
			minval = img[y];
	}

	// Uncomment to print the max and min value seen in the frame.
	// Useful to determine the min and max for a given palette
	// printf("Max = %d, Min = %d\n",maxval, minval);
#else  // FIND_MINMAX

	// Assign min and max to the global value
	maxval = g_maxval;
	minval = g_minval;

#endif

	// Create and scale false color image from frame data
	for (y = 0; y < LEPTON_HEIGHT; y++) {
		for (x = 0; x < LEPTON_WIDTH; x++) {

			// Assign value from pixel
			val = img[y*LEPTON_WIDTH + x];

			// Value not to exceed min and max
			val = val > maxval ? maxval : val;
			val = val < minval ? minval : val;

			// Scale value to 8-bits
			val = ((val - minval) * 255) / (maxval-minval);
			val &= 0xff;

			// Assign R, G, and B from colormap
			r = colormap[val*3 + COLORMAP_RED_INDEX];
			g = colormap[val*3 + COLORMAP_GREEN_INDEX];
			b = colormap[val*3 + COLORMAP_BLUE_INDEX];

			// Scale up to target size (very basic scaling routine)
			for (yb = 0; yb < mag; yb++) {
				loc = (x * mag) * BYTES_PER_PIXEL + (yb + y * mag) * FB_WIDTH * BYTES_PER_PIXEL;
				for (xb = 0; xb < mag; xb++) {
						*(vidsendbuf + loc++) = 0;
						*(vidsendbuf + loc++) = r;
						*(vidsendbuf + loc++) = g;
						*(vidsendbuf + loc++) = b;
				}
			}
		}
	}
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
static sem_t lock1;

static void *sendvid(void *v)
{
    do {
        sem_wait(&lock1);
        if(exit_flag)
        	break;

        if(front_img)
        	write_frame(front_img);

        if (vidsendsiz != write(v4l2sink, vidsendbuf, vidsendsiz))
            exit(-1);

    } while (!exit_flag);

    printf("Video writing thread exiting...\n");
}

int main(int argc, char **argv)
{
    if( argc == 2 )
        v4l2dev = argv[1];

    unsigned short * back_img, * temp_img;
    int thread_handle;

    // Signal handling
    sigint_setup();

    // Setup buffers.  Use a front/back buffer scheme so that we can operate
    // on buffers in different threads without contending
    back_img = (unsigned short*)img1;
    front_img = (unsigned short*)img2;

    // Variables to store timestamps.  This is useful when profiling the application
    // to determine how long various processes take to run.  I've seen where
    // it takes a great deal longer to read a frame from the SPI than expected
    // This results in some missed frames (hiccups).
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

    // Initialize semaphore to notify thread that
    // an image is ready for processing and sending
    if (sem_init(&lock1, 0, 0) == -1) {
    	fprintf(stderr, "Could not initialize thread sync semaphore\n");
        exit(-1);
    }

   thread_handle = pthread_create(&sender, NULL, sendvid, NULL);

   do {
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
   } while (!exit_flag);

   // Wait for thread to terminate
   pthread_cancel(sender);

   close(v4l2sink);
   free(vidsendbuf);
   stop_device(); // close SPI
   return 0;
}
