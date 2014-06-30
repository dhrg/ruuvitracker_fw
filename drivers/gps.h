/*
 *  Simcom 908 GPS driver for Ruuvitracker.
 *
 * @author: Tomi Hautakoski
 */

/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Tomi Hautakoski
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef _GPS_H_
#define _GPS_H_
#include "ch.h"
#include <time.h>


/* GPS Fix type */
typedef enum {
    GPS_FIX_TYPE_NONE = 1,
    GPS_FIX_TYPE_2D = 2,
    GPS_FIX_TYPE_3D = 3
} fix_t;

/* GPS Internal State */
enum GPS_state {
    STATE_UNKNOWN = 0,
    STATE_OFF = 1,
    STATE_ON = 2,
    STATE_HAS_2D_FIX = 3,
    STATE_HAS_3D_FIX = 4,
    STATE_ERROR = 5
};
// TODO: Add the standby states too

// Event signal flag
extern EventSource gps_fix_updated;

/* Date and time data */
typedef struct _gps_datetime {
    int hh;
    int mm;
    int sec;
    int msec;
    int day;
    int month;
    int year;
} gps_datetime;

/* Location data */
struct gps_data_t {
    fix_t   fix_type;         /* Satellite fix type */
    int     n_satellites;     /* Number of satellites in view */
    double	lat;              /* Latitude */
    double	lon;
    double	speed;
    double	heading;
    double	altitude;
    double	pdop;
    double	hdop;
    double	vdop;
    gps_datetime dt;
    systime_t last_update;
};


#define GPS_CMD_LINE_END "\r\n"

/******** API ***************/

/**
 * Start GPS module and processing
 */
void gps_start(void);

/**
 * Stop GPS processing and shuts down the module
 */
void gps_stop(void);

/**
 * Reports GPS fix type.
 * \return Fix type.
 */
fix_t gps_has_fix(void);

int gps_get_state(void);
int gps_get_serial_port_validated(void);


/**
 * Request GPS data.
 * This function queries last known values parsed from GPS messages.
 * \return gps_data_t structure.
 */
struct gps_data_t gps_get_data_nonblock(void);

/**
 * Request GPS data.
 * This function waits for next GPS data to arrive.
 * \return gps_data_t structure.
 */
struct gps_data_t gps_get_data(void);

/**
 * Writes to the GPS uart
 * @param char* string to write
 */
void gps_uart_write(const char *str);

/**
 * Sends a command to GPS and reads reply
 *
 * NOTE: reply checking is not yet implemented!!
 *
 * @param char* command to write, linebreaks are added automatically
 * @return int status code
 */
int gps_cmd(const char *cmd);
int gps_cmd_fmt(const char *fmt, ...);

/**
 * Convert GPS datetime to time.h format
 */
void gps_datetime2tm(struct tm *timp, gps_datetime *gpstime);

int gps_set_update_interval(int ms); // PMTK300
int gps_set_standby(bool state); // PMTK161


/*
TODO:
add gps_query_update_interval(); // PMTK400

Add support for the ACK messages to gps_cmd
*/ 

#endif

