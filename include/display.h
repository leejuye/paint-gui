#ifndef display_h
#define display_h

#include "macros.h"
#include <stdbool.h>

#define X(POINT)                UPPER16(POINT)
#define Y(POINT)                LOWER16(POINT)

#define WIDTH(SIZE)             UPPER16(SIZE)
#define HEIGHT(SIZE)            LOWER16(SIZE)

#define POINT(X, Y)             COMBINE16(X, Y)
#define SIZE(W, H)              COMBINE16(W, H)

#define DELTA_WIDTH(P0, P1)     ABS(X(P1) - X(P0))
#define DELTA_HEIGHT(P0, P1)    ABS(Y(P1) - Y(P0))

#define IN_RANGE(X, START, END) ((X >= START) && (X <= END))


#define PIXEL(R, G, B) (((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3))

void            disp_map(int fd);
void 			disp_unmap(void);

void            disp_set_direct(bool value);

void 			disp_draw_point(short x, short y, unsigned short color);
void			disp_draw_line(short x0, short y0 , short x1, short y1, unsigned short color);
void 			disp_draw_rect(short x, short y, short width, short height, unsigned short color);
void            disp_draw_whole(unsigned short color);

void            disp_commit(void);
void            disp_commit_partial(short x, short y, short width, short height);
void			disp_cancel(void);

void            disp_clear(void);

#endif /* display_h */