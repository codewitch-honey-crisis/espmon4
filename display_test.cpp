
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <memory.h>
#include "panel.h"
#include <gfx.hpp>
#include <uix.hpp>
//#define BUNGEE_IMPLEMENTATION
//#include "assets/bungee.h"

using namespace gfx;
using namespace uix;

static uix::display disp;
#if LCD_SYNC_TRANSFER == 0
// indicates the LCD DMA transfer is complete
void panel_lcd_flush_complete(void) {
    disp.flush_complete();
}
#endif
// flush a bitmap to the display
static void uix_on_flush(const rect16& bounds,
                             const void *bitmap, void* state) {
    //printf("flush (%d, %d)-(%d, %d)\n",bounds.x1, bounds.y1, bounds.x2, bounds.y2);
    panel_lcd_flush(bounds.x1, bounds.y1, bounds.x2, bounds.y2,
                              (void *)bitmap);
#if LCD_SYNC_TRANSFER > 0
    disp.flush_complete();
#endif
}

//const_buffer_stream text_font_stm(bungee,sizeof(bungee));


#if LCD_COLOR_SPACE == LCD_COLOR_GSC
#define PIXEL gsc_pixel
#elif (LCD_COLOR_SPACE == LCD_COLOR_RGB || LCD_COLOR_SPACE == LCD_COLOR_BGR) 
#define PIXEL rgb_pixel
#endif
#if LCD_BIT_DEPTH==18
using pixel_t = pixel<
    channel_traits<channel_name::nop,2>,
    channel_traits<channel_name::R,6>,
    channel_traits<channel_name::nop,2>,
    channel_traits<channel_name::G,6>,
    channel_traits<channel_name::nop,2>,
    channel_traits<channel_name::B,6>
>;
#else 
using pixel_t = PIXEL<LCD_BIT_DEPTH>;
#endif
using screen_t = screen_ex<bitmap<pixel_t>,LCD_X_ALIGN,LCD_Y_ALIGN>;

using color_t = color<screen_t::pixel_type>;
using uix_color_t = color<uix_pixel>;
using vcolor_t = color<vector_pixel>;

using painter_t = painter<screen_t::control_surface_type>;
static screen_t main_screen;

static painter_t main_painter;

static void main_painter_on_paint(screen_t::control_surface_type& destination,const srect16& clip, void* state) {
    pixel_t px_start = color_t::black;
    pixel_t px_r = color_t::red;
    pixel_t px_g = color_t::green;
    pixel_t px_b = color_t::blue;
    int y2red = destination.dimensions().height/3;
    int y2green = y2red + destination.dimensions().height/3;
    int y2blue = destination.bounds().y2;
    for(int x = 0;x<destination.dimensions().width;++x) {
        float b = (((float)x)/destination.bounds().x2);
        pixel_t px = px_r.blend(px_start,b);
        draw::line(destination,srect16(x,0,x,y2red),px);
        px = px_g.blend(px_start,b);
        draw::line(destination,srect16(x,y2red+1,x,y2green),px);
        px = px_b.blend(px_start,b);
        draw::line(destination,srect16(x,y2green+1,x,y2blue),px);
    }
}

void loop();
static void loop_task(void* arg) {
    TickType_t wdt_ts = xTaskGetTickCount();
    while(1) {
        TickType_t ticks = xTaskGetTickCount();
        if(ticks>wdt_ts+pdMS_TO_TICKS(200)) {
            wdt_ts = ticks;
            vTaskDelay(5);
        }
        loop();
    }
}
static void refresh_display() {
    while(disp.dirty()) {
        disp.update();
    }
}
extern "C" void app_main() {
#ifdef POWER
    panel_power_init();
#endif
#ifdef EXPANDER_BUS
    panel_expander_init();
#endif
    panel_lcd_init();
    disp.buffer_size(LCD_TRANSFER_SIZE);
    disp.buffer1((uint8_t*)panel_lcd_transfer_buffer());
#if LCD_SYNC_TRANSFER == 0
    disp.buffer2((uint8_t*)panel_lcd_transfer_buffer2());
#endif
    disp.on_flush_callback(uix_on_flush);
    disp.on_touch_callback(nullptr);
    main_screen.dimensions({LCD_WIDTH,LCD_HEIGHT});
    main_screen.background_color(gfx::color<typename screen_t::pixel_type>::black);
    main_painter.bounds(main_screen.bounds());
    main_painter.on_paint_callback(main_painter_on_paint);
    main_screen.register_control(main_painter);
    disp.active_screen(main_screen);
    refresh_display();
    TaskHandle_t loop_handle;
    xTaskCreate(loop_task,"loop_task",4096,nullptr,20,&loop_handle);
}

void loop() {
    
}
