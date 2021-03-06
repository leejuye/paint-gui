#include "touch.h"
#include "display.h"
#include "paint.h"
#include "shape.h"
#include "ui.h"

#include <unistd.h>

/*************************  [ 이 소스파일에서만 쓰이는 인라인함수들 (시작) ] *************************/

/**
 * paint 구조체에 적절한 초기값을 대입합니다.
 */
static inline void _init(struct paint *context) {
    context->canvas_x = X(UI_CANVAS_LOCATION);
    context->canvas_y = Y(UI_CANVAS_LOCATION);
    context->canvas_width = WIDTH(UI_CANVAS_SIZE);
    context->canvas_height = HEIGHT(UI_CANVAS_SIZE);
    
    context->canvas_x0 = X(UI_CANVAS_LOCATION);
    context->canvas_y0 = Y(UI_CANVAS_LOCATION);
    context->canvas_x1 = X(UI_CANVAS_LOCATION) + WIDTH(UI_CANVAS_SIZE) - 1;
    context->canvas_y1 = Y(UI_CANVAS_LOCATION) + HEIGHT(UI_CANVAS_SIZE) - 1;
    
    context->draw_mode = MODE_LINE;
    context->fill = false;
    context->draw_color = PAINT_DEFAULT_DRAW_COLOR;
    context->canvas_color = UI_DEFAULT_CANVAS_COLOR;
    
    context->touch_state = TOUCH_STATE_DONE;
    
    LIST_HEAD_REINIT(context->shapes);
}

/**
 * 한 점이 캔버스 영역에 속하는지 검사합니다.
 */
static inline bool _point_in_canvas(struct paint *context, int x, int y) {
    return POINT_IN_AREA(POINT(context->canvas_x, context->canvas_y),
                         SIZE(context->canvas_width, context->canvas_height),
                         x, y);
}

/**
 * 두 점으로 이루어진 범위 내의 도형들을 새로 그립니다.
 */
static inline void _redraw_areap(struct paint *context, int x0, int y0, int x1, int y1) {
    ENSURE_POINTS_ORDERED(x0, y0, x1, y1);
    
    /**
     * 영역검사를 합니다.
     */
    if (x0 < context->canvas_x0 && x1 < context->canvas_x0) {
        /* 왼쪽으로 완전히 사라짐 */
        goto finish;
    }
    else if (x0 > context->canvas_x1 && x1 > context->canvas_x1) {
        /* 오른쪽으로 완전히 사라짐 */
        goto finish;
    }
    else if (y0 < context->canvas_y0 && y1 < context->canvas_y0) {
        /* 위쪽으로 완전히 사라짐 */
        goto finish;
    }
    else if (y0 > context->canvas_y1 && y1 > context->canvas_y1) {
        /* 아래쪽으로 완전히 사라짐 */
        goto finish;
    }
    
    /**
     * 영역을 지우고 (배경색으로)
     */
    disp_draw_rectp_fill(x0, y0, x1, y1, context->canvas_color);
    
    /**
     * 새로 그리고
     */
    struct shape *cur;
    list_for_each_entry(cur, &context->shapes, list) {
        disp_draw_2d_shape(cur);
    }
    
    /**
     * 그 변화를 commit합니다.
     * commit되는 변화는 캔버스 영역 이하이어야 합니다!!!!!! 안그러면 UI영역 침범임!!
     */
    disp_commit_partialp(MAX(x0, context->canvas_x0),
                         MAX(y0, context->canvas_y0),
                         MIN(x1, context->canvas_x1),
                         MIN(y1, context->canvas_y1));
    
finish:
    disp_cancel();
}

/**
 * 도형을 평행이동하고 새로 그립니다.
 */
static inline void _move_shape_and_redraw(struct paint *context, struct shape *shape, int delta_x, int delta_y) {
    
    SHAPE_EXPORT_AREA_TO_TWO_POINTS(shape, before_x0, before_y0, before_x1, before_y1);
    shape_move(shape, delta_x, delta_y);
    SHAPE_EXPORT_AREA_TO_TWO_POINTS(shape, after_x0, after_y0, after_x1, after_y1);
    
    _redraw_areap(context,
                  MIN(before_x0, after_x0),
                  MIN(before_y0, after_y0),
                  MAX(before_x1, after_x1),
                  MAX(before_y1, after_y1));
    
    return;
}

/**
 * 도형의 크기를 바꾸고 새로 그립니다.
 */
static inline void _transform_shape_and_redraw(struct paint *context, struct shape *shape, int delta_width, int delta_height) {
    
    SHAPE_EXPORT_AREA_TO_TWO_POINTS(shape, before_x0, before_y0, before_x1, before_y1);
    shape_transform(shape, delta_width, delta_height);
    SHAPE_EXPORT_AREA_TO_TWO_POINTS(shape, after_x0, after_y0, after_x1, after_y1);
    
    _redraw_areap(context,
                  MIN(before_x0, after_x0),
                  MIN(before_y0, after_y0),
                  MAX(before_x1, after_x1),
                  MAX(before_y1, after_y1));
    
    return;
}

/**
 * 지정된 좌표에 존재하는 도형을 선택합니다.
 */
static inline struct shape *_pick_shape(struct paint *context, int x, int y) {
    struct shape *cur;
    
    list_for_each_entry_reverse(cur, &context->shapes, list) {
        if (shape_point_in_shape_area(cur, x, y)) {
            return cur;
        }
    }
    
    return NULL;
}

/**
 * paint 구조체의 shape 연결리스트에 파라미터로 받은 도형을 추가해줍니다.
 */
static inline void _add_shape(struct paint *context, struct shape *shape) {
    shapes_list_add(&context->shapes, shape);
}

/**
 * ui의 배경색을 칠하고 그 위에 윤곽과 글씨를 씁니다.
 */
static inline void _draw_ui_background(unsigned short back_color, unsigned short ui_color) {
    register int offset = 0;
    
    disp_set_direct(true);
    
    do {
        disp_draw_point(offset % DP_WIDTH,
                        offset / DP_WIDTH,
                        GET_BIT8(ui_image, offset) ? back_color : ui_color);
    } while (++offset < DP_MEM_SIZE);
    
    disp_set_direct(false);
}

/**
 * 캔버스를 그립니다.
 */
static inline void _draw_ui_canvas(unsigned short canvas_color) {
    disp_set_direct(true);
    
    disp_draw_rect_fill(X(UI_CANVAS_LOCATION),
                        Y(UI_CANVAS_LOCATION),
                        WIDTH(UI_CANVAS_SIZE),
                        HEIGHT(UI_CANVAS_SIZE),
                        canvas_color);
    
    disp_set_direct(false);
}

/**
 * 버튼들을 그려줍니다.
 */
static inline void _draw_ui_buttons(void) {
    disp_set_direct(true);
    
    for (int i = 0; i < UI_NUMBER_OF_BUTTONS; ++i) {
        if (ui_buttons[i].type & BT_COLORABLE) {
            /**
             * 색칠 가능한 버튼들을 그려줍니다.
             */
            disp_draw_rect_fill(ui_buttons[i].x, ui_buttons[i].y, ui_buttons[i].width, ui_buttons[i].height, ui_buttons[i].color);
        }
    }
    
    disp_set_direct(false);
}

/**
 * 전체 ui를 그립니다.
 */
static inline void _draw_ui(void) {
    _draw_ui_background(UI_DEFAULT_BACK_COLOR, UI_DEFAULT_TEXT_COLOR);
    
    usleep(300000);
    
    _draw_ui_canvas(UI_DEFAULT_CANVAS_COLOR);
    
    usleep(300000);
    
    _draw_ui_buttons();
}

/**
 * 버튼에 선택되었음을 알리는 시각 효과를 추가합니다.
 */
static inline void _mark_button(const struct button *btn) {
    ASSERTDO(btn != NULL, return);
    
    if (!(btn->type & (BT_MARKABLE))) {
        /**
         * 마크 불가능한 버튼이면 아무 일도 하지 않습니다!
         */
        return;
    }
    
    for (int i = 0; i < UI_NUMBER_OF_BUTTONS; ++i) {
        /**
         * 모든 버튼들을 순회하면서, 파라미터로 들어온 버튼과 같은 그룹에 속하는 버튼이 있으면,
         * 마크를 지워줍니다!
         */
        if (UI_GROUP(btn->id) == UI_GROUP(ui_buttons[i].id)) {
            disp_set_direct(true);
            
            disp_draw_rect(ui_buttons[i].x - 2, ui_buttons[i].y - 2, ui_buttons[i].width + 4, ui_buttons[i].height + 4, UI_DEFAULT_BACK_COLOR);
            
            disp_set_direct(false);
        }
    }
    
    /**
     * 이제 파라미터로 들어온 버튼을 마크해줍니다!
     */
    disp_set_direct(true);
    
    disp_draw_rect(btn->x - 2, btn->y - 2, btn->width + 4, btn->height + 4, UI_DEFAULT_TEXT_COLOR);
    
    disp_set_direct(false);
}

/**
 * shape를 싹 다 해방시켜주고(free~) 캔버스를 기본색으로 칠합니다.
 */
static inline void _clear_canvas(struct paint *context) {
    struct shape *cur;
    struct shape *save;
    list_for_each_entry_safe(cur, save, &context->shapes, list) {
        shape_delete(cur);
    }
    
    _draw_ui_canvas(UI_DEFAULT_CANVAS_COLOR);
}

/**
 * 버튼이 눌렸을 때 적절한 행동을 합니다.
 */
static inline void _on_button_clicked(struct paint *context, const struct button *btn) {
    ASSERTDO(btn != NULL, return);
    
    switch (btn->id) {
            
        case UI_BTN_LINE: {
            if (context->fill) {
                print_info("_on_button_clicked(): cannot draw line with fill mode.\n");
                return;
            }
            context->draw_mode = MODE_LINE;
            print_info("_on_button_clicked(): set draw mode to line.\n");
            break;
        }
            
        case UI_BTN_RECT: {
            context->draw_mode = MODE_RECT;
            print_info("_on_button_clicked(): set draw mode to rect.\n");
            break;
        }
            
        case UI_BTN_OVAL: {
            context->draw_mode = MODE_OVAL;
            print_info("_on_button_clicked(): set draw mode to oval.\n");
            break;
        }
            
        case UI_BTN_FDRAW: {
            if (context->fill) {
                print_info("_on_button_clicked(): cannot do free draw with fill mode.\n");
                return;
            }
            context->draw_mode = MODE_FDRAW;
            print_info("_on_button_clicked(): set draw mode to free draw.\n");
            break;
        }
            
        case UI_BTN_SELECT: {
            context->draw_mode = MODE_SELECT;
            print_info("_on_button_clicked(): set draw mode to select.\n");
            break;
        }
            
        case UI_BTN_ERASE: {
            context->draw_mode = MODE_ERASE;
            print_info("_on_button_clicked(): set draw mode to erase.\n");
            break;
        }
            
        case UI_BTN_CLEAR: {
            _clear_canvas(context);
            print_info("_on_button_clicked(): clear canvas and remove all shapes.\n");
            break;
        }
            
        case UI_BTN_PEN: {
            context->fill = false;
            print_info("_on_button_clicked(): set fill mode to pen(non-fill).\n");
            break;
        }
            
        case UI_BTN_FILL: {
            if (context->draw_mode == MODE_LINE || context->draw_mode == MODE_FDRAW) {
                print_info("_on_button_clicked(): Cannot draw line or do free draw with fill mode.\n");
                return;
            }
            context->fill = true;
            print_info("_on_button_clicked(): Set fill mode to fill.\n");
            break;
        }
            
        case UI_BTN_C0: {
            context->draw_color = UI_PALETTE_C0_COLOR;
            print_info("_on_button_clicked(): Set draw color to c0.\n");
            break;
        }
            
        case UI_BTN_C1: {
            context->draw_color = UI_PALETTE_C1_COLOR;
            print_info("_on_button_clicked(): Set draw color to c1.\n");
            break;
        }
            
        case UI_BTN_C2: {
            context->draw_color = UI_PALETTE_C2_COLOR;
            print_info("_on_button_clicked(): Set draw color to c2.\n");
            break;
        }
            
        case UI_BTN_C3: {
            context->draw_color = UI_PALETTE_C3_COLOR;
            print_info("_on_button_clicked(): Set draw color to c3.\n");
            break;
        }
            
        case UI_BTN_C4: {
            context->draw_color = UI_PALETTE_C4_COLOR;
            print_info("_on_button_clicked(): Set draw color to c4.\n");
            break;
        }
            
        case UI_BTN_C5: {
            context->draw_color = UI_PALETTE_C5_COLOR;
            print_info("_on_button_clicked(): Set draw color to c5.\n");
            break;
        }
            
        case UI_BTN_C6: {
            context->draw_color = UI_PALETTE_C6_COLOR;
            print_info("_on_button_clicked(): Set draw color to c6.\n");
            break;
        }
            
        case UI_BTN_C7: {
            context->draw_color = UI_PALETTE_C7_COLOR;
            print_info("_on_button_clicked(): Set draw color to c7.\n");
            break;
        }
            
        default: {
            /**
             * 여기에 걸리면 그건 컴파일시점에 뭐가 틀린겁니다,,,,!!!
             */
            print_error("_on_button_clicked(): UNKNOWN BUTTON ID: %d\n", btn->id);
            return;
        }
    }
    
    _mark_button(btn);
}

/**
 * 캔버스로 터치 입력이 들어왔을 때 적절한 행동을 합니다.
 */
static inline void _on_canvas_touched(struct paint *context, int x, int y) {
    
    struct shape *shape;
    int x0;
    int y0;
    int x1;
    int y1;
    
    switch (context->touch_state) {
            
        /**
         * 첫 번째 경우:
         * 터치가 막 시작되었을 때.
         */
        case TOUCH_STATE_BEGIN: {
            
            switch (context->draw_mode) {
                case MODE_LINE: {
                    
                    shape = shape_create(ST_LINEP, x, y, x, y, context->draw_color);
                    _add_shape(context, shape);
                    
                    disp_set_direct(true);
                    disp_draw_2d_shape(shape);
                    disp_set_direct(false);
                    
                    print_info("_on_canvas_touched(): new line created at (%d, %d).\n", x, y);
                    
                    return;
                }
                case MODE_RECT: {
                    shape = shape_create(context->fill ? ST_RECTP_FILL : ST_RECTP, x, y, x, y, context->draw_color);
                    _add_shape(context, shape);
                    
                    disp_set_direct(true);
                    disp_draw_2d_shape(shape);
                    disp_set_direct(false);
                    
                    print_info("_on_canvas_touched(): new rect created at (%d, %d).\n", x, y);
                    
                    return;
                }
                case MODE_OVAL: {
                    shape = shape_create(context->fill ? ST_OVALP_FILL : ST_OVALP, x, y, x, y, context->draw_color);
                    _add_shape(context, shape);
                    
                    disp_set_direct(true);
                    disp_draw_2d_shape(shape);
                    disp_set_direct(false);
                    
                    print_info("_on_canvas_touched(): new oval created at (%d, %d).\n", x, y);
                    
                    return;
                }
                case MODE_FDRAW: {
                    shape = shape_create(ST_FREEP, x, y, x, y, context->draw_color);
                    points_add(&shape->fdraw_points, x, y);
                    _add_shape(context, shape);
                    
                    disp_set_direct(true);
                    disp_draw_2d_shape(shape);
                    disp_set_direct(false);
                    
                    print_info("_on_canvas_touched(): new free draw created at (%d, %d).\n", x, y);
                    
                    return;
                }
                case MODE_SELECT: {
                    context->selected_shape = _pick_shape(context, x, y);
                    if (context->selected_shape) {
                        print_info("_on_canvas_touched(): selected object at (%d, %d).\n", x, y);
                    }
                    else {
                        print_info("_on_canvas_touched(): no object at (%d, %d).\n", x, y);
                    }
                    
                    return;
                }
                case MODE_ERASE: {
                    shape = _pick_shape(context, x, y);
                    ASSERTDO(shape != NULL, return);
                    
                    SHAPE_EXPORT_AREA_TO_TWO_POINTS_REUSE(shape, x0, y0, x1, y1);
                    shape_delete(shape);
                    
                    _redraw_areap(context, x0, y0, x1, y1);
                    
                    print_info("_on_canvas_touched(): erased object at (%d, %d).\n", x, y);
                    
                    return;
                }
                    
                default: {
                    /* 임파서블!! */
                    print_error("_on_canvas_touched(): UNKNOWN DRAW MODE: %d.\n", context->draw_mode);
                    return;
                }
            }
            
            /* 사실 여기에 도달할 일이 없다. */
            return;
        }
            
        /**
         * 두 번째 경우:
         * 터치를 유지하며 드래그중일 때
         */
        case TOUCH_STATE_DRAG: {
            
            switch (context->draw_mode) {
                case MODE_LINE: {
                    shape = shapes_list_peek_last(&context->shapes);
                    
                    _transform_shape_and_redraw(context, shape, x - context->last_x, y - context->last_y);
                    
                    return;
                }
                case MODE_RECT: {
                    shape = shapes_list_peek_last(&context->shapes);
                    
                    _transform_shape_and_redraw(context, shape, x - context->last_x, y - context->last_y);
                    
                    return;
                }
                case MODE_OVAL: {
                    shape = shapes_list_peek_last(&context->shapes);
                    
                    _transform_shape_and_redraw(context, shape, x - context->last_x, y - context->last_y);
                    
                    return;
                }
                case MODE_FDRAW: {
                    shape = shapes_list_peek_last(&context->shapes);
                    
                    shape_add_point(shape, x, y);
                    
                    disp_draw_linep(context->last_x, context->last_y, x, y, context->draw_color);
                    _redraw_areap(context, context->last_x, context->last_y, x, y);
                    
                    return;
                }
                case MODE_SELECT: {
                    shape = context->selected_shape;
                    ASSERTDO(shape != NULL, return);
                    
                    _move_shape_and_redraw(context, shape, x - context->last_x, y - context->last_y);
                    
                    return;
                }
                case MODE_ERASE: {
                    /* 해당없어요~ */
                    return;
                }
                    
                default: {
                    /* 임파서블!! */
                    print_error("_on_canvas_touched(): UNKNOWN DRAW MODE: %d.\n", context->draw_mode);
                    return;
                }
            }
            
            return;
        }
            
        /**
         * 세 번째 경우:
         * 터치가 종료되었을 때.
         * 이것이 마지막 터치이다.
         */
        case TOUCH_STATE_DONE: {
            
            /**
             * 필요없는 수정사항은 취소하기!
             */
            disp_cancel();
            
            switch (context->draw_mode) {
                case MODE_LINE: {
                    print_info("_on_canvas_touched(): line drawing done.\n");
                    return;
                }
                case MODE_RECT: {
                    print_info("_on_canvas_touched(): rect drawing done.\n");
                    return;
                }
                case MODE_OVAL: {
                    print_info("_on_canvas_touched(): oval drawing done.\n");
                    return;
                }
                case MODE_FDRAW: {
                    print_info("_on_canvas_touched(): free drawing done.\n");
                    return;
                }
                case MODE_SELECT: {
                    print_info("_on_canvas_touched(): select moving done.\n");
                    return;
                }
                case MODE_ERASE: {
                    /**
                     * 해당없어요~~
                     */
                    return;
                }
                    
                default: {
                    /* 임파서블!! */
                    print_error("_on_canvas_touched(): UNKNOWN DRAW MODE: %d.\n", context->draw_mode);
                    return;
                }
            }
            
            return;
        }
            
        /**
         * 여기는 위의 세 가지 경우가 아닌 경우인데,
         * 불가능하다!!!!!!!!!!!!!!!!!!!
         */
        default: {
            /* 이거는 있을쑤가 업따!!! */
            print_error("_on_canvas_touched(): UNKNOWN TOUCH STATE.\n");
            return;
        }
            
    } /* end of switch */
    
}

/*************************  [ 이 소스파일에서만 쓰이는 인라인함수들 (끝)] *************************/


struct paint *paint_create(void) {
    print_info("paint_create()\n");
    
    PAINT_ALLOC(new_paint);
    
    _init(new_paint);
    
    print_info("paint_create(): initialized.\n");
    
    _draw_ui();
    
    _on_button_clicked(new_paint, ui_find_button_by_id(UI_BTN_LINE));
    _on_button_clicked(new_paint, ui_find_button_by_id(UI_BTN_C7));
    _on_button_clicked(new_paint, ui_find_button_by_id(UI_BTN_PEN));
    
    print_info("paint_create(): ready.\n");
    
    return new_paint;
}

void paint_touch_start(struct paint *context, int x, int y) {
    NULL_CHECK("paint_touch_start()", context);
    print_trace("paint_touch_start(): touch start at (%d, %d)\n", x, y);
    
    context->touch_state = TOUCH_STATE_BEGIN;
    
    /**
     * 터치가 시작될 때, 이 터치가 캔버스(그림이 그려지는 부분) 안으로부터 시작된 터치인지 확인하는 flag를
     * 업데이트해줍니다!
     */
    context->touch_started_from_canvas = _point_in_canvas(context, x, y);
    if (context->touch_started_from_canvas) {
        /**
         * 터치가 캔버스 안에 속한다면,
         * 캔버스에서 새로운 무언가를 합니다.
         */
        _on_canvas_touched(context, x, y);
    }
    else {
        /**
         * 터치가 캔버스 밖이라면,
         * UI 버튼을 처리합니다.
         * 이건 터치가 시작될 때에만 처리해주면 됩니다!
         */
        const struct button *btn = ui_find_button_by_coordinate(x, y);
        ASSERTDO(btn != NULL, return);
        
        _on_button_clicked(context, btn);
    }
    
    context->last_x = x;
    context->last_y = y;
}

void paint_touch_drag(struct paint *context, int x, int y) {
    NULL_CHECK("paint_touch_drag()", context);
    print_trace("paint_touch_drag(): touch drag at (%d, %d)\n", x, y);
    
    context->touch_state = TOUCH_STATE_DRAG;
    
    if (!context->touch_started_from_canvas) {
        /**
         * 이 프로그램에서, 캔버스 밖에서 발생한 터치를 시작점으로 하는 드래그는
         * 필요가 없습니다. 따라서 무시합니다!
         */
        print_trace("paint_touch_drag(): ignore drag.\n");
        return;
    }
    
    /**
     * 캔버스에서 해야 할 일은 이 친구에게~
     */
    _on_canvas_touched(context, x, y);
    
    context->last_x = x;
    context->last_y = y;
}

void paint_touch_end(struct paint *context, int x, int y) {
    NULL_CHECK("paint_touch_end()", context);
    print_trace("paint_touch_end(): touch end at (%d, %d)\n", x, y);
    
    context->touch_state = TOUCH_STATE_DONE;
    
    if (!context->touch_started_from_canvas) {
        /**
         * 여기서도! 캔버스 밖에서 발생한 터치는 끝날때에도 무시합니다!
         */
        print_info("paint_touch_end(): touch from out of canvas finished.\n");
        return;
    }
    
    /**
     * 캔버스에서 해야 할 일은 이 친구에게~
     */
    _on_canvas_touched(context, x, y);
    
    context->last_x = x;
    context->last_y = y;
}

void paint_test(struct paint *context) {
    
    /**
     * 도형 생성
     */
    
    struct shape *line = shape_create(ST_LINEP, 90, 200, 290, 90, COLOR(255, 0, 0));
    shapes_list_add(&context->shapes, line);
    
    struct shape *rect = shape_create(ST_RECT_FILL, 140, 110, 50, 50, COLOR(0, 255, 0));
    shapes_list_add(&context->shapes, rect);
    
    struct shape *fdraw = shape_create(ST_FREEP, 0, 0, 0, 0, COLOR(0, 0, 255));
    shape_add_point(fdraw, 100, 100);
    shape_add_point(fdraw, 102, 106);
    shape_add_point(fdraw, 106, 112);
    shape_add_point(fdraw, 112, 119);
    shape_add_point(fdraw, 120, 125);
    shape_add_point(fdraw, 130, 131);
    shape_add_point(fdraw, 142, 137);
    shape_add_point(fdraw, 156, 142);
    shape_add_point(fdraw, 172, 150);
    shapes_list_add(&context->shapes, fdraw);
    
    _redraw_areap(context, 0, 0, 319, 239);
    
    /**
     * 이동, 크기변환 테스트
     */
    int init_x = 160;
    int init_y = 120;
    
    struct shape *selected = _pick_shape(context, init_x, init_y);
    
    for (int i = 0; i < 50; ++i) {
        usleep(20000);
        
        _move_shape_and_redraw(context, selected, 2, 1);
    }
    
    for (int i = 0; i < 100; ++i) {
        usleep(20000);
        
        _transform_shape_and_redraw(context, rect, -1, -1);
    }
    
    for (int i = 0; i < 200; ++i) {
        usleep(20000);
        
        _transform_shape_and_redraw(context, rect, 1, 1);
    }
    
    for (int i = 0; i < 30; ++i) {
        usleep(20000);
        
        _transform_shape_and_redraw(context, rect, -2, -1);
    }
    
    for (int i = 0; i < 120; ++i) {
        usleep(20000);
        
        _transform_shape_and_redraw(context, line, 0, -1);
    }
    
}

