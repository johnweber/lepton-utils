#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include "leptsci.h"

static uint8_t bits = 8;
static uint32_t speed = 20000000;
static uint16_t delay = 0;
static int leptfd;
#define VOSPI_FRAME_SIZE 164
#define LEPTON_NUM_ROWS 60
#define LEPTON_NUM_COLUMNS 80
#define IMX6


#define PRINT_DEBUG_INFO 0
#define PRINT_TIMING_INFO 0

// At 16MHz, a 164-byte message transfer takes about 82 microseconds
// The Lepton has a new frame about every 1/27th second (about 37 milliseconds)
// We need to check for a new at about 3x that rate (every 1 milliseconds).
#define VOSPI_FRAME_DELAY_MICROSECONDS 1000

// Re-sync delay time.  This is the time recommended by FLIR to deassert CS
// and the SPI CLK in order to wait for resync
#define VOSPI_FRAME_RESYNC_TIME_MICROSECONDS 200000

#ifdef IMX6
const char device[] = "/dev/spidev32766.0";
#else
const char device[] = "/dev/spidev0.0";
#endif

int leptopen()
{
    uint8_t mode = SPI_MODE_1;
    leptfd = open(device, O_RDWR);
    if (leptfd < 0)
        return -1;
    if (-1 == ioctl(leptfd, SPI_IOC_WR_MODE, &mode))
        return -2;
    if (-1 == ioctl(leptfd, SPI_IOC_RD_MODE, &mode))
        return -3;
    if (-1 == ioctl(leptfd, SPI_IOC_WR_BITS_PER_WORD, &bits))
        return -4;
    if (-1 == ioctl(leptfd, SPI_IOC_RD_BITS_PER_WORD, &bits))
        return -5;
    if (-1 == ioctl(leptfd, SPI_IOC_WR_MAX_SPEED_HZ, &speed))
        return -6;
    if (-1 == ioctl(leptfd, SPI_IOC_RD_MAX_SPEED_HZ, &speed))
        return -7;
    return 0;
}

int leptclose()
{
    close(leptfd);
    return 0;
}

double time_subtract(struct timespec ts_old_time, struct timespec ts_time) {

	double old_time = (double)ts_old_time.tv_sec + (double)ts_old_time.tv_nsec/(double)1E9;
	double new_time = (double)ts_time.tv_sec + (double)ts_time.tv_nsec/(double)1E9;

	return new_time - old_time;
}

int leptget(unsigned short *img)
{
    int row = -1;
    static int sync = 1;   // Trigger sync in first transfer
    int capturing_frame = 0;
    struct timespec ts_capture_start, ts_capture_end;
    struct timespec ts_transfer_start, ts_transfer_end;
    double total_transfer_time = 0.0;
    int num_transfers = 0;

    do {

    	if(sync) {

    		// Wait for an interval to ensure a timeout of the VoSPI
    		// interface
    		usleep(VOSPI_FRAME_RESYNC_TIME_MICROSECONDS);

    		// Let's not do this again unless we have to
    		sync = 0;
    	}

        uint8_t lepacket[VOSPI_FRAME_SIZE];
        struct spi_ioc_transfer tr = {
            .tx_buf = (unsigned long) NULL,      // NULL if not transmitting
            .rx_buf = (unsigned long) lepacket,
            .len = VOSPI_FRAME_SIZE,
            .delay_usecs = delay,
            .speed_hz = speed,
            .bits_per_word = bits,
            .cs_change = 0,
        };

        clock_gettime(CLOCK_MONOTONIC_RAW, &ts_transfer_start);

        int ret = ioctl(leptfd, SPI_IOC_MESSAGE(1), &tr);
        if (ret < 1) {  // could not transfer
        	fprintf(stderr, "Could not complete SPI transfer\n");
        	return -1;
        }
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts_transfer_end);

        total_transfer_time += time_subtract(ts_transfer_start, ts_transfer_end);
        num_transfers++;

        // Check for dummy frame
        if (((lepacket[0] & 0xf) == 0x0f)) {
        	usleep(VOSPI_FRAME_DELAY_MICROSECONDS);
        	if(PRINT_DEBUG_INFO) printf( "d");
            continue;
        }

        // We are receiving valid packets.
        row = lepacket[1];

        // Check to see if row is out of bounds, which
        // indicates an error (as there can only be a max of 60 rows)
        // TODO:  This should be investigated further as this condition
        // should not happen.
        if (row >= LEPTON_NUM_ROWS) {

        	printf("[%d]\n", row);

            // Trigger resync at the start of the next loop
            sync = 1;
            row = -1;
            continue;
        }

        if(row == 0 && capturing_frame == 0){  // Packet is first line, grab a timestamp
        	capturing_frame = 1;
        	clock_gettime(CLOCK_MONOTONIC_RAW, &ts_capture_start);
        }

        // Transfer packet data to img.
        int i;
        for ( i = 0; i < LEPTON_NUM_COLUMNS; i++) {
            img[row * LEPTON_NUM_COLUMNS + i]
              = (lepacket[2 * i + 4] << 8) | lepacket[2 * i + 5];
        }

        if(PRINT_DEBUG_INFO) printf("p%d",row);

    } while (row < LEPTON_NUM_ROWS - 1);

    // Finalized the frame, grab the time
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts_capture_end);

    if(PRINT_DEBUG_INFO) printf("f\n");

    double frame_time;
    if(PRINT_TIMING_INFO) {
    	printf("leptget: image capture: = %f ", time_subtract(ts_capture_start, ts_capture_end)*(double)1e6);
    	printf("# xfers: %d, avg xfer time %f\n", num_transfers, (total_transfer_time/(double)num_transfers)*(double)1e6);
    }

    return 0;
}
