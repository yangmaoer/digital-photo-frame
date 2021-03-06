/********************************************************************************
 ** Copyright (C) 2015 <ericrock@foxmail.com>
 ** File name         : ebook.c
 ** Author            : wuweidong
 ** Description       :
 ** Todo              :
 ********************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "encode.h"
#include "bitmap.h"
#include "display.h"
#include "input.h"
#include "config.h"

struct txt_info txt;
static struct page page_entry;
static struct page *cur_page;

int show_one_page(struct page *p)
{
    int startx, starty;
    struct encode_ops *ecd_ops;
    struct bitmap_ops *bmp_ops;
    struct display_ops *dsp_ops;
    unsigned int code;
    unsigned char *bitmap = NULL;
    unsigned char *cur_buf = p->buf;
    int len;

    DBG("show page: %d\n", p->id);

    ecd_ops = txt.ecd_ops;
    bmp_ops = txt.bmp_ops;
    dsp_ops = txt.dsp_ops;

#if defined( __x86_64__) || defined(__i386__)
    startx = dsp_ops->xres / 2;
    starty = 0;
#else               // ARM
    startx = starty = 0;
#endif
    DBG("start(startx=%d, starty=%d) xres=%d yres=%d txt.length=%d\n", startx, starty, dsp_ops->xres, dsp_ops->yres, txt.length);
    dsp_ops->clear_screen(0);

    struct bitmap_info bif;
    struct cell_frame *cf = &(bif.cf);
    struct font_frame *ff = &(bif.ff);
    cf->height = cf->width = txt.font_size;
    cf->xmin = startx;
    cf->xmax = cf->xmin + cf->width;
    cf->ymin = starty;
    cf->ymax = cf->ymin + cf->height;
    while (cur_buf < txt.end) {
        if ((len = ecd_ops->get_char_code(cur_buf, &code)) == -1) {
            DBG("Fail to get_char_code\n");
            return (cur_buf - p->buf);
        }                
        if (bmp_ops->get_char_bitmap(code, &bitmap, &bif) == -1) {
            DBG("Fail to get_char_bitmap\n");
            return (cur_buf - p->buf);
        }

        cur_buf += len;
        // DBG("code:%x len=%d\n", code, len);
        // handle enter
        // 1. windows enter: 0d 0a = \r \n
        // 2. unix enter: 0a = \n
        if ( code == 0x0d0a || code == '\n') {
            cf->xmin = startx;
            cf->ymin = cf->ymin + cf->height;
            continue;
        }
        // handle tab
        if ( code == '\t') {
            cf->xmin += 4 * cf->width;
            continue;
        }

        // handle space
        if ( code == ' ') {
            cf->xmin += 1 * cf->width;
            continue;
        }

        // meet x edge;
        if ((cf->xmin + cf->width) > dsp_ops->xres) {
            // DBG("line is full\n");
            // reset cell frame
            cf->xmin = startx;
            cf->xmax = cf->xmin + cf->width;
            cf->ymin += cf->height;
            cf->ymax = cf->ymin + cf->height;
            // reset font frame
            ff->xmin = cf->xmin;
            ff->xmax = ff->xmin + ff->width;
            ff->ymin = ff->ymin + cf->height;
            ff->ymax = ff->ymin + ff->height;
        }
        if ((cf->ymin + cf->height) >= dsp_ops->yres ) {
            // DBG("page is full\n");
            return (cur_buf - p->buf - len);
        }

#if 0
        DBG("xmin:%d\n", ff->xmin);
        DBG("xmax:%d\n", ff->xmax);
        DBG("ymin:%d\n", ff->ymin);
        DBG("ymax:%d\n", ff->ymax);
        DBG("width:%d\n", ff->width);
        DBG("height:%d\n", ff->height);
#endif
        int i, j, k;
        unsigned char byte;
        int color = 0;
        int offset = cf->height - (ff->ymin - cf->ymin) * 2 - ff->height;
        switch (bmp_ops->bpp) {
        case 1:
            color = 0xffffffff;
            for (i = 0; i < ff->height; i++) {
                for (k = 0; k < ff->width/8; k++) {
                    byte = bitmap[i*ff->width/8 + k];
                    for (j = 7; j >= 0; j--) {
                        if (byte & (1<<j)) {
                            dsp_ops->draw_pixel(ff->xmin + (8*k) + (7-j), ff->ymin + i, color);
                        }
                    }
                }
            }
            break;
        case 8:
            color = 0xffffffff;
            for (i = 0; i < ff->height; i++) {
                for (j = 0; j < ff->width; j++) {
                    // DBG("(%d, %d)%2x\n", ff->xmin + j , ff->ymin + i + offset, bitmap[i * ff->width + j]);
                    if (bitmap[i * ff->width + j] != 0) {
                        dsp_ops->draw_pixel(ff->xmin + j, ff->ymin + i + offset, color);
                    }
                }
            }
            break;
        default:
            break;
        }
        cf->xmin += cf->width;
    }
    return (cur_buf - p->buf);
}

void page_list(void)
{
    struct list_head *list;

    INFO("page_list:\n");
    list_for_each(list, &(page_entry.list)) {
        struct page *p = list_entry(list, struct page, list);
        INFO("%d\n", p->id);
    }
}

int show_next_page(void) 
{   
    int len = 0;
    struct list_head *cur_list = &(cur_page->list);
    struct page *next_page = list_entry(cur_list->next, struct page, list);

    if (next_page->buf == NULL) {
        len = show_one_page(cur_page);
    } else {
        len = show_one_page(next_page);
        cur_page = next_page;
        cur_list = &(cur_page->list);
        next_page = list_entry(cur_list->next, struct page, list);
    }
    // end of novel
    if ((cur_page->buf + len - txt.start) >= txt.length) {
        DBG("end of novel\n");
        return 1;
    }

    if (next_page->buf == (cur_page->buf + len) && next_page->id == (cur_page->id + 1)) {
        return 0;
    } else {
        struct page *new_page = malloc(sizeof(struct page));
        new_page->buf = cur_page->buf + len;
        new_page->id = cur_page->id + 1;
        DBG("add new page: %d\n", new_page->id);
        list_add_tail(&(new_page->list), &(page_entry.list));
    }
    return 0;
}

int show_prev_page(void)
{
    struct list_head *cur_list = &(cur_page->list);
    struct page *prev_page = list_entry(cur_list->prev, struct page, list);
    if (prev_page->buf == NULL) {
        show_one_page(cur_page);
    } else {
        show_one_page(prev_page);
        cur_page = prev_page;
    }
    return 0;
}

int open_txt(int argc, char **argv)
{
    struct stat stat_buf;
    char *txt_file = argv[1];
    char *ttc_file = argv[2];
    char *font_size = argv[3];

    strcpy(txt.ttc_path, ttc_file);
    if ((txt.fd = open(txt_file, O_RDONLY)) == -1) {
        ERR("fail to open %s\n", txt_file);
        return -1;
    }

    if (fstat(txt.fd, &stat_buf) == -1) {
        ERR("fail to fstat %d\n", txt.fd);
        return -1;
    }

    txt.start = (unsigned char *)mmap(NULL, stat_buf.st_size, PROT_READ, MAP_SHARED, txt.fd, 0);
    if (txt.start == MAP_FAILED) {
        ERR("fail to mmap %d\n", txt.fd);
        return -1;
    }
    txt.length = stat_buf.st_size;
    txt.end = txt.start + txt.length;

    DBG("txt:\n");
#ifdef DEBUG
    unsigned char *buf;
    int i = 0;
    buf = txt.start;
    for (i=0; i<32; i++) {
        DBG("%02x ", (0xff & buf[i]));
        if (!((i+1)%16) ) {
            DBG("\n");
        }
    }
#endif
    txt.font_size = atoi(font_size);
    return 0;
}

void close_txt()
{
    munmap((void*)txt.start, txt.length);
    close(txt.fd);
}

void txt_head_remove(void)
{
    unsigned char *head;
    if (txt.ecd_ops->type == ENCODE_UTF8) {
        head = txt.start;
        if (head[0] == 0xef && head[1] == 0xbb && head[2] == 0xbf) {
            txt.start = txt.start + 3;
            txt.length -= 3;
        }
    }

    if (txt.ecd_ops->type == ENCODE_UTF16BE) {
        head = txt.start;
        if (head[0]==0xfe && head[1]==0xff) {
            txt.start = txt.start + 2;
            txt.length -= 2;
        }
    }

    if (txt.ecd_ops->type == ENCODE_UTF16LE) {
        head = txt.start;
        if (head[0]==0xff && head[1]==0xfe) {
            txt.start = txt.start + 2;
            txt.length -= 2;
        }
    }
}

void print_usage(int argc, char **argv)
{
    ERR("Usage:%s txt_file font_file font_size\n", argv[0]);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        print_usage(argc, argv);
        return -1;
    }
    if (open_txt(argc, argv) == -1) {
        ERR("fail to open txt file %s\n", argv[1]);
        return -1;
    }

    // encode module
    if (encode_init() == -1) {
        ERR("fail to init encode module\n");
        goto err_encode_init;
    }

    encode_list();

    if (encode_select(&txt) == -1) {
        ERR("fail to select encode type\n");
        goto err_encode_init;
    }

    txt_head_remove();

    // bitmap module
    if (bitmap_init() == -1) {
        ERR("fail to init bitmap module\n");
        goto err_bitmap_init;
    }

    bitmap_list();

    if (bitmap_select(&txt) == -1) {
        ERR("fail to select bitmap type\n");
        goto err_bitmap_init;
    }

    // display module
    if (display_init() == -1) {
        ERR("fail to init display module\n");
        goto err_display_init;
    }

    display_list();

    if (display_select(&txt) == -1) {
        ERR("fail to select display type\n");
        goto err_display_init;
    }

    // head page, not have any data
    page_entry.buf = NULL;
    page_entry.id = -1;
    INIT_LIST_HEAD(&(page_entry.list));

    // show first page
    cur_page = malloc(sizeof(struct page));
    cur_page->id = 1;
    cur_page->buf = txt.start;
    list_add(&(cur_page->list), &(page_entry.list));
    show_next_page();

    INFO("\n\nusage: +[next page], -[previous page] enter[exit ebook]\n");

    // input module
    struct input_event event;
    int q = 0;
    int max_fd = -1;

    if (input_init(&txt) == -1){
        ERR("fail to init input module\n");
        goto err_input_init;
    }
    if ((max_fd = input_ops_init()) == -1) {
        ERR("fail to init input ops\n");
        goto err_input_init;
    }
    while (1) { 
        event.type = INPUT_TYPE_UNKNOWN;
        event.value = INPUT_VALUE_UNKNOWN;

        if (input_get_event(&event) == 0) {
            switch (event.value) {
            case INPUT_VALUE_DOWN:
                show_next_page();
                break;
            case INPUT_VALUE_UP:
                show_prev_page();
                break;
            case INPUT_VALUE_EXIT:
                q = 1;
                break;
            default:
                break;
            }
            if (q == 1) {
                break;
            }
        }
    }

    err_input_init:
    display_exit();
    err_display_init:
    bitmap_exit();
    err_bitmap_init:
    encode_exit();
    err_encode_init:
    close_txt();
    return 0;
}
