#ifndef touch_h
#define touch_h

#define EVENTPATH "/dev/input/event1"

#define READ_X	0x01
#define READ_Y	0x02
#define READ_P	0x04

#define TS_SIZE 4096

#define STATE_NONE 		0x00
#define STATE_TOUCH_DOWN 	0x01
#define STATE_TOUCH_UP 		0x02

struct touch_event {
	int x;
	int y;
	int pressure;
	int touch_state;
};

struct touch_correction {
	int x_min;
	int x_max;

	int y_min;
	int y_max;
};

int touch_read(int fd, struct touch_event *event, struct touch_correction *correction);

#endif /* touch_h */
