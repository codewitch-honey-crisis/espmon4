
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <memory.h>
#include "panel.h"
#include <gfx.hpp>
#include <uix.hpp>
#include "serial.hpp"
#define BUNGEE_IMPLEMENTATION
#include "assets/bungee.h"

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
static void uix_on_flush(const rect16& bounds,const void *bitmap, void* state) {
    //printf("flush (%d, %d)-(%d, %d)\n",bounds.x1, bounds.y1, bounds.x2, bounds.y2);
    panel_lcd_flush(bounds.x1, bounds.y1, bounds.x2, bounds.y2,
                              (void *)bitmap);
#if LCD_SYNC_TRANSFER > 0
    disp.flush_complete();
#endif
}
#if defined(TOUCH_BUS) || defined(BUTTON)
static bool pressed = false;
#endif
const_buffer_stream text_font_stm(bungee,sizeof(bungee));


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

template<typename ControlSurfaceType>
class vvert_label : public canvas_control<ControlSurfaceType> {
    using base_type = canvas_control<ControlSurfaceType>;
public:
    using type = vvert_label;
    using control_surface_type = ControlSurfaceType;
private:
    canvas_text_info m_label_text;
    canvas_path m_label_text_path;
    rectf m_label_text_bounds;
    bool m_label_text_dirty;
    vector_pixel m_color;
    uix_pixel m_background_color;
    void build_label_path_untransformed() {
        const float target_width = this->dimensions().height;
        float fsize = this->dimensions().width;
        if(m_label_text_path.initialized()) {
            m_label_text_path.clear();
        } else {
            m_label_text_path.initialize();
        }
        do {
            m_label_text_path.clear();
            m_label_text.font_size = fsize;
            m_label_text_path.text({0.f,0.f},m_label_text);
            m_label_text_bounds = m_label_text_path.bounds(true);
            --fsize;
            
        } while(fsize>0.f && m_label_text_bounds.width()>=target_width);
    }
public:
    vvert_label() : base_type() ,m_label_text_dirty(true) {
        m_label_text.ttf_font = &text_font_stm;
        m_label_text.text_sz("Label");
        m_label_text.encoding = &text_encoding::utf8;
        m_label_text.ttf_font_face = 0;
        m_color = vector_pixel(255,255,255,255);
    }
    virtual ~vvert_label() {

    }
    text_handle text() const {
        return m_label_text.text;
    }
    void text(text_handle text, size_t text_byte_count) {
        m_label_text.text=text;
        m_label_text.text_byte_count = text_byte_count;
        m_label_text_dirty = true;
        this->invalidate();
    }
    void text(const char* sz) {
        m_label_text.text_sz(sz);
        m_label_text_dirty = true;
        this->invalidate();
    }
    rgba_pixel<32> color() const {
        rgba_pixel<32> result;
        convert(m_color,&result);
        return result;
    }
    void color(rgba_pixel<32> value) {
        convert(value,&m_color);
        this->invalidate();
    }
    gfx::rgba_pixel<32> background_color() const {
        return m_background_color;
    }
    void background_color(gfx::rgba_pixel<32> value) {
        m_background_color = value;
        this->invalidate();
    }
    
protected:
    virtual void on_before_paint() override {
        if(m_label_text_dirty) {
            build_label_path_untransformed();
            m_label_text_dirty = false;
        }
    }
    virtual void on_paint(control_surface_type& destination, const gfx::srect16& clip) {
        if(m_background_color.opacity()!=0) {
            gfx::draw::filled_rectangle(destination,destination.bounds(),m_background_color);
        }
        base_type::on_paint(destination,clip);
    }
    virtual void on_paint(canvas& destination, const srect16& clip) override {
        canvas_style si = destination.style();
        si.fill_paint_type = paint_type::solid;
        si.stroke_paint_type = paint_type::none;
        si.fill_color = m_color;
        destination.style(si);
        // save the current transform
        matrix old = destination.transform();
        matrix m = old.rotate(math::deg2rad(-90));
        
        m=m.translate(-m_label_text_bounds.width()-((destination.dimensions().height-m_label_text_bounds.width())*0.5f),m_label_text_bounds.height());
        destination.transform(m);
        destination.path(m_label_text_path);
        destination.render();
        destination.clear_path();
        // restore the old transform
        destination.transform(old);
    }
};
using vert_label_t = vvert_label<screen_t::control_surface_type>;

using label_t = vlabel<screen_t::control_surface_type>;

template<typename ControlSurfaceType>
class bar : public control<ControlSurfaceType> {
    using base_type = control<ControlSurfaceType>;
public:
    using type = bar;
    using control_surface_type = ControlSurfaceType;
private:
    rgba_pixel<32> m_color;
    rgba_pixel<32> m_back_color;
    bool m_is_gradient;
    float m_value;
public:
    bar() : base_type(), m_is_gradient(false), m_value(0) {
        static constexpr const rgb_pixel<24> px(0,255,0);
        static constexpr const rgb_pixel<24> black(0,0,0);
        convert(px,&m_color);
        rgba_pixel<32> px2;
        convert(black,&px2);
        m_back_color = m_color.blend(px2,.125f);
    }
    
    virtual ~bar() {

    }
    float value() const {
        return m_value;
    }
    void value(float value) {
        value = math::clamp(0.f,value,1.f);
        if(value!=m_value) {
            m_value = value;
            this->invalidate();
        }
    }
    bool is_gradient() const {
        return m_is_gradient;
    }
    void is_gradient(bool value) {
        m_is_gradient= value;
        this->invalidate();
    }
    rgba_pixel<32> color() const {
        return m_color;
    }
    void color(rgba_pixel<32> value) {
        m_color=value;
        this->invalidate();
    }
    rgba_pixel<32> back_color() const {
        return m_back_color;
    }
    void back_color(rgba_pixel<32> value) {
        m_back_color  = value;
        this->invalidate();
    }
protected:
    virtual void on_paint(control_surface_type& destination, const srect16& clip) {
        typename control_surface_type::pixel_type scr_bg;
        destination.point({0,0},&scr_bg);
        uint16_t x_end = roundf(m_value*destination.dimensions().width-1);
        uint16_t y_end = destination.dimensions().height-1;
        if(m_is_gradient) {
            y_end=destination.dimensions().height*.6666;
            // two reference points for the ends of the graph
            hsva_pixel<32> px = gfx::color<gfx::hsva_pixel<32>>::red;
            hsva_pixel<32> px2 = gfx::color<gfx::hsva_pixel<32>>::green;
            auto h1 = px.channel<channel_name::H>();
            auto h2 = px2.channel<channel_name::H>();
            // adjust so we don't overshoot
            h2 -= 64;
            // the actual range we're drawing
            auto range = abs(h2 - h1) + 1;
            // the width of each gradient segment
            int w = (int)ceilf(destination.dimensions().width / 
                                (float)range) + 1;                
            // the step of each segment - default 1
            int s = 1;
            // if the gradient is larger than the control
            if (destination.dimensions().width < range) {
                // change the segment to width 1
                w = 1;
                // and make its step larger
                s = range / (float)destination.dimensions().width;  
            } 
            int x = 0;
            // c is the current color offset
            // it increases by s (step)
            int c = 0;
            // for each color in the range
            for (auto j = 0; j < range; ++j) {
                // adjust the H value (inverted and offset)
                px.channel<channel_name::H>(range - c - 1 + h1);
                // if we're drawing the filled part
                // it's fully opaque
                // otherwise it's semi-transparent
                int sw = w;
                int diff=0;
                if (m_value==0||x> x_end) {
                    px.channel<channel_name::A>(95);
                    if((x-w)<=x_end) {
                        sw = x_end-x+1;
                        diff = w-sw;
                    }
                } else {
                    px.channel<channel_name::A>(255);
                }
                // create the rect for our segment
                srect16 r(x, y_end+1, x + sw , destination.dimensions().height-1);
                
                // black out the area underneath so alpha blending
                // works correctly
                draw::filled_rectangle(destination, 
                                    r, 
                                    scr_bg
                                    );
                // draw the segment
                draw::filled_rectangle(destination, 
                                    r, 
                                    px 
                                    );
                if(diff>0) {
                    r=srect16(x+sw,y_end+1,x+w,destination.dimensions().height-1);
                    draw::filled_rectangle(destination, 
                                    r, 
                                    scr_bg
                                    );
                    // draw the segment
                    draw::filled_rectangle(destination, 
                                    r, 
                                    px 
                                    );
                }
                // increment
                x += w;
                c += s;
            }
        } 
        if(m_value>0) {
            draw::filled_rectangle(destination,srect16(0,0,x_end,y_end),m_color);
            draw::filled_rectangle(destination,srect16(x_end+1,0,destination.dimensions().width-1,y_end),m_back_color);
        } else {
            draw::filled_rectangle(destination,srect16(0,0,destination.dimensions().width-1,y_end),m_back_color);
        }
    }
};
using bar_t = bar<screen_t::control_surface_type>;

#if LCD_HEIGHT>128
template<typename ControlSurfaceType>
class vgraph : public control<ControlSurfaceType> {
    using base_type = control<ControlSurfaceType>;
    using buffer_t = data::circular_buffer<uint8_t,100>;
public:
    using type = vgraph;
    using control_surface_type = ControlSurfaceType;
private:
    struct data_line {
        rgba_pixel<32> color;
        buffer_t buffer;
        data_line* next;
    };
    data_line* m_first;
    void clear_lines() {
        data_line*entry=m_first;
        while(entry!=nullptr) {
            data_line* n = entry->next;
            delete entry;
            entry = n;
        }
        m_first = nullptr;
    }
public:
    vgraph() : base_type(), m_first(nullptr) {
    }
    virtual ~vgraph() {
        clear_lines();
    }
    void remove_lines() {
        clear_lines();
        this->invalidate();
    }
    size_t add_line(rgba_pixel<32> color) {
        data_line* n;
        if(m_first==nullptr) {
            n = new data_line();
            if(n==nullptr) {
                return 0; // out of memory
            }
            n->color = color;
            n->next = nullptr;
            m_first = n;
            return 1;
        }
        size_t result = 0;
        data_line*entry=m_first;
        while(entry!=nullptr) {
            n = entry->next;
            if(n==nullptr) {
                n = new data_line();
                if(n==nullptr) {
                    return 0; // out of memory
                }
                n->color =color;
                n->next = nullptr;
                entry->next = n;
                break;
            }
            entry = n;
            ++result;
        }
        this->invalidate();
        return result+1;
    }
    bool set_line(size_t index, rgba_pixel<32> color) {
        if(m_first==nullptr) {
            return false;
        }
        data_line*entry=m_first;
        while(entry!=nullptr && index-->0) {
            entry = entry->next;
            if(entry==nullptr) {
                return false;
            }
        }
        entry->color = color;
        this->invalidate();
        return true;
    }
    bool add_data(size_t line_index,float value) {
        uint8_t v = math::clamp(0.f,value,1.f)*255;
        size_t i = 0;
        for(data_line* entry = m_first;entry!=nullptr;entry=entry->next) {
            if(i==line_index) {
                if(entry->buffer.size()==entry->buffer.capacity) {
                    uint8_t tmp;
                    entry->buffer.get(&tmp);
                }
                entry->buffer.put(v);
                this->invalidate();
                return true;
            }
            ++i;
        }
        return false;
    }
    void clear_data() {
        for(data_line* entry = m_first;entry!=nullptr;entry=entry->next) {
            entry->buffer.clear();
        }
        this->invalidate();
    }
protected:
    void on_paint(control_surface_type& destination, const srect16& clip) {
        srect16 b = (srect16)destination.bounds();
        auto px = gfx::color<typename control_surface_type::pixel_type>::gray;
        draw::rectangle(destination,b,px);
        b.inflate_inplace(-1,-1);
        const float tenth_x = ((float)b.width())/10.f;
        for(float x = b.x1;x<=b.x2;x+=tenth_x) {
            destination.fill(rect16(x,b.y1,x,b.y2),px);
        }
        const float tenth_y = ((float)b.height())/10.f;
        for(float y = b.y1;y<=b.y2;y+=tenth_y) {
            destination.fill(rect16(b.x1,y,b.x2,y),px);
        }
        for(data_line* entry = m_first;entry!=nullptr;entry=entry->next) {
            if(entry->buffer.size()) {
                uint8_t v = *entry->buffer.peek(0);
                float fv = v/255.f;
                float y = (1.0-fv)*(tenth_y*10);
                pointf pt(b.x1,y);
                for(int i = 1;i<entry->buffer.size();++i) {
                    v = *entry->buffer.peek(i);
                    fv = v/255.f;
                    pointf pt2=pt;
                    pt2.x+=(tenth_x*.1f);
                    y = (1.f-fv)*(tenth_y*10);
                    pt2.y =y;
                    draw::filled_rectangle(destination,srect16(floorf(pt.x),floorf(pt.y),ceilf(pt2.x),ceilf(pt2.y)),entry->color);
                    draw::filled_rectangle(destination,srect16(floorf(pt.x-1),floorf(pt.y-1),ceilf(pt2.x-1),ceilf(pt2.y-1)),entry->color);
                    pt=pt2;
                }
            }
        }
    }
};
using graph_t = vgraph<screen_t::control_surface_type>;
#endif

static screen_t main_screen;
static vert_label_t value1_label;
static vert_label_t value2_label;

static label_t top_value1_label;
static label_t top_value2_label;

static bar_t top_value1_bar;
static bar_t top_value2_bar;

static label_t bottom_value1_label;
static label_t bottom_value2_label;

static bar_t bottom_value1_bar;
static bar_t bottom_value2_bar;

static int8_t screen_index = -1;

#if LCD_HEIGHT > 128
static graph_t history_graph;
#endif

static char top_label_text[12]={0};
static char bottom_label_text[12]={0};

static uint16_t top_value1_max=1;
static uint16_t top_value2_max=1;
static char top_value1_text[12]={0};
static char top_value1_suffix[4]={0};
static char top_value2_text[12]={0};
static char top_value2_suffix[4]={0};
static uint16_t bottom_value1_max=1;
static uint16_t bottom_value2_max=1;
static char bottom_value1_text[12]={0};
static char bottom_value1_suffix[4]={0};
static char bottom_value2_text[12]={0};
static char bottom_value2_suffix[4]={0};
static label_t disconnected_label;

#if defined(TOUCH_BUS) || defined(BUTTON)
static void update_input() {
    if(disconnected_label.visible()) {
        return;
    }
#ifdef TOUCH_BUS
    panel_touch_update();
    uint16_t x,y,s;
    size_t count = 1;
    panel_touch_read_raw(&count,&x,&y,&s);
    if(count>0) {
        pressed = true;
    } else {
        if(pressed) {
            screen_index++;
            serial_write(0,screen_index);
        }
        pressed = false;
    }
#endif
#ifdef BUTTON
    if(panel_button_read_all()) {
        pressed = true;
    } else {
        if(pressed) {
            screen_index++;
            serial_write(0,screen_index);
        }
        pressed = false;
    }
#endif
}
#endif

static void loop();
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
#ifdef TOUCH_BUS
    panel_touch_init();
#endif
#ifdef BUTTON
    panel_button_init();
#endif
#ifdef LCD_BCKL_PWM
    panel_lcd_backlight(64);
#endif
    serial_init();
    disp.buffer_size(LCD_TRANSFER_SIZE);
    disp.buffer1((uint8_t*)panel_lcd_transfer_buffer());
#if LCD_SYNC_TRANSFER == 0
    disp.buffer2((uint8_t*)panel_lcd_transfer_buffer2());
#endif
    disp.on_flush_callback(uix_on_flush);

#if LCD_HEIGHT > 128
    static const int section_height_divisor = 4;
#else
    static const int section_height_divisor = 2;
#endif
    main_screen.dimensions({LCD_WIDTH,LCD_HEIGHT});
    main_screen.background_color(gfx::color<typename screen_t::pixel_type>::black);
    value1_label.bounds(srect16(0,0,(main_screen.dimensions().width)/10-1,main_screen.dimensions().height/section_height_divisor).inflate(-2,-4));
    value1_label.text("---");
    value1_label.background_color(uix_color_t::black);
    value1_label.color(uix_color_t::white);
    main_screen.register_control(value1_label);
    srect16 b = value1_label.bounds();
    top_value1_label.bounds(srect16(b.x2+2,b.y1,b.x2+1+(main_screen.dimensions().width/5),b.height()/2+b.y1));
    top_value1_label.text("---");
    top_value1_label.font(text_font_stm);
    top_value1_label.color(uix_color_t::white);
    strcpy(top_value1_text,"---");
    main_screen.register_control(top_value1_label);
    b = top_value1_label.bounds();
    top_value2_label.bounds(srect16(b.x1,b.y2+1,b.x2,b.y2+b.height()));
    top_value2_label.text("---");
    top_value2_label.font(text_font_stm);
    top_value2_label.color(uix_color_t::white);
    strcpy(top_value2_text,"---");
    main_screen.register_control(top_value2_label);

    b=top_value1_label.bounds();
    b.x1 = b.x2+4;
    b.x2 = main_screen.dimensions().width-1;
    b.y2-=2;
    top_value1_bar.bounds(b);
    top_value1_bar.value(0);
    main_screen.register_control(top_value1_bar);

    b=top_value2_label.bounds();
    b.x1 = b.x2+4;
    b.x2 = main_screen.dimensions().width-1;
    b.y2-=2;
    top_value2_bar.bounds(b);
    auto px = uix_color_t::white;
    top_value2_bar.color(px);
    top_value2_bar.is_gradient(true);
    top_value2_bar.value(0);
    main_screen.register_control(top_value2_bar);
    

    value2_label.bounds(value1_label.bounds().offset(0,main_screen.dimensions().height/section_height_divisor+3));
    value2_label.color(uix_color_t::white);
    value2_label.background_color(uix_color_t::black);
    value2_label.text("---");
    main_screen.register_control(value2_label);
    b = value2_label.bounds();
    bottom_value1_label.bounds(srect16(b.x2+2,b.y1,b.x2+1+(main_screen.dimensions().width/5),b.height()/2+b.y1));
    bottom_value1_label.text("---");
    bottom_value1_label.font(text_font_stm);
    bottom_value1_label.color(uix_color_t::white);
    strcpy(bottom_value1_text,"---");
    main_screen.register_control(bottom_value1_label);
    b = bottom_value1_label.bounds();
    bottom_value2_label.bounds(srect16(b.x1,b.y2+1,b.x2,b.y2+b.height()));
    bottom_value2_label.text("---"); // \xC2\xB0
    bottom_value2_label.color(uix_color_t::white);
    bottom_value2_label.font(text_font_stm);
    strcpy(bottom_value2_text,"---");
    main_screen.register_control(bottom_value2_label);
    
    b=bottom_value1_label.bounds();
    b.x1 = b.x2+4;
    b.x2 = main_screen.dimensions().width-1;
    b.y2-=2;
    bottom_value1_bar.bounds(b);
    bottom_value1_bar.value(0);
    bottom_value1_bar.color(uix_color_t::white);
    main_screen.register_control(bottom_value1_bar);

    b=bottom_value2_label.bounds();
    b.x1 = b.x2+4;
    b.x2 = main_screen.dimensions().width-1;
    b.y2-=2;
    bottom_value2_bar.bounds(b);
    px = uix_color_t::white;
    bottom_value2_bar.color(px);
    bottom_value2_bar.is_gradient(true);
    bottom_value2_bar.value(0);
    main_screen.register_control(bottom_value2_bar);
    
#if LCD_HEIGHT>128
    b = main_screen.bounds();
    b.y1=main_screen.dimensions().height/2+1;
    history_graph.bounds(b);
    history_graph.add_line(top_value1_bar.color());
    history_graph.add_line(top_value2_bar.color());
    history_graph.add_line(bottom_value1_bar.color());
    history_graph.add_line(bottom_value2_bar.color());
    main_screen.register_control(history_graph);
#endif
    disconnected_label.bounds(srect16(0,0,main_screen.dimensions().width/2,main_screen.dimensions().width/8).center(main_screen.bounds()));
    rgba_pixel<32> bg = uix_color_t::black;
    //bg.opacity_inplace(.6f);
    disconnected_label.font(text_font_stm);
    disconnected_label.color(uix_color_t::white);
    disconnected_label.background_color(bg);
    disconnected_label.text("[ disconnected ]");
    disconnected_label.text_justify(uix_justify::center);
    main_screen.register_control(disconnected_label);

    disp.active_screen(main_screen);
    refresh_display();
    TaskHandle_t loop_handle;
    xTaskCreate(loop_task,"loop_task",4096,nullptr,20,&loop_handle);
}
static uix_pixel to_color(const uint8_t* col_array) {
#if LCD_BIT_DEPTH > 1
    return uix_pixel(col_array[0],col_array[1],col_array[2],col_array[3]);
#else
    return uix_pixel(255,255,255,255);
#endif
}
static void loop() {
    struct to_avg {
        float g0,g1,g2,g3;
    };
    static to_avg values[5];
    static TickType_t ts = 0;
    static int ts_count = 0;
    static int index =0;
    if(xTaskGetTickCount()>=ts+pdMS_TO_TICKS(100)) {
        ts=xTaskGetTickCount();
        ++ts_count;
        serial_write(screen_index==-1?0:1,screen_index==-1?0:screen_index);
    }
    float v;
    
    to_avg& value=values[index];
    response_t resp; 
    int cmd = serial_read_packet(&resp);
    while(cmd!=-1) {
        ts_count = 0;
        if(disconnected_label.visible()) {
            disconnected_label.visible(false);
            refresh_display();
            screen_index = -1;
        }
        if(cmd==0) { // new screen
            response_screen_t& scr = resp.screen;
            screen_index = scr.index;
#if LCD_BIT_DEPTH == 1
            scr.flags &= 0xF0; // turn off gradients for monochrome dispalys
#endif
            top_value1_max = scr.top_max1;
            strcpy(top_value1_suffix,scr.top_suffix1);
            top_value2_max = scr.top_max2;
            strcpy(top_value2_suffix,scr.top_suffix2);
            bottom_value1_max = scr.bottom_max1;
            strcpy(bottom_value1_suffix,scr.bottom_suffix1);
            bottom_value2_max = scr.bottom_max2;
            strcpy(bottom_value2_suffix,scr.bottom_suffix2);
            screen_index = scr.index;
            strcpy(top_label_text,scr.top_label);
            value1_label.text(top_label_text);
            value1_label.color(to_color(scr.top_label_color));
            top_value1_bar.color(to_color(scr.top_color1));
            top_value1_bar.back_color(top_value1_bar.color().opacity(.25));
            top_value1_bar.is_gradient((scr.flags&(1<<0)));
            top_value2_bar.color(to_color(scr.top_color2));
            top_value2_bar.back_color(top_value2_bar.color().opacity(.25));
            top_value2_bar.is_gradient((scr.flags&(1<<1)));
            strcpy(bottom_label_text,scr.bottom_label);
            value2_label.text(bottom_label_text);
            value2_label.color(to_color(scr.bottom_label_color));
            bottom_value1_bar.color(to_color(scr.bottom_color1));
            bottom_value1_bar.back_color(bottom_value1_bar.color().opacity(.25));
            top_value1_bar.is_gradient((scr.flags&(1<<2)));
            bottom_value2_bar.color(to_color(scr.bottom_color2));
            bottom_value2_bar.back_color(bottom_value2_bar.color().opacity(.25));
            bottom_value2_bar.is_gradient((scr.flags&(1<<3)));
#if LCD_HEIGHT > 128
            history_graph.clear_data();
            history_graph.set_line(0,to_color(scr.top_color1));
            history_graph.set_line(1,to_color(scr.top_color2));
            history_graph.set_line(2,to_color(scr.bottom_color1));
            history_graph.set_line(3,to_color(scr.bottom_color2));
#endif
            refresh_display();
            cmd = serial_read_packet(&resp);
        }
        if(cmd==1) { // screen data
            response_data_t& data = resp.data;
            v=((float)data.top_value1)/top_value1_max;
            value.g0=v;
            itoa(data.top_value1,top_value1_text,10);
            strcat(top_value1_text,top_value1_suffix);
            top_value1_label.text(top_value1_text);
            refresh_display();
            top_value1_bar.value(v);
            refresh_display();
            v=((float)data.top_value2)/top_value2_max;
            value.g1=v;
            itoa(data.top_value2,top_value2_text,10);
            strcat(top_value2_text,top_value2_suffix);
            top_value2_label.text(top_value2_text);
            refresh_display();
            top_value2_bar.value(v);
            refresh_display();
            v=((float)data.bottom_value1)/bottom_value1_max;
            value.g2=v;
            itoa(data.bottom_value1,bottom_value1_text,10);
            strcat(bottom_value1_text,bottom_value1_suffix);
            bottom_value1_label.text(bottom_value1_text);
            refresh_display();
            bottom_value1_bar.value(v);
            refresh_display();
            v=((float)data.bottom_value2)/bottom_value2_max;
            value.g3=v;
            itoa(data.bottom_value2,bottom_value2_text,10);
            strcat(bottom_value2_text,bottom_value2_suffix);
            bottom_value2_label.text(bottom_value2_text);
            refresh_display();
            bottom_value2_bar.value(v);
            refresh_display();
            ++index;
            cmd = serial_read_packet(&resp);
        }
    }
#if LCD_HEIGHT>128
    if(index>=5 && !disconnected_label.visible()) {
        index = 0;
        to_avg total;
        memset(&total,0,sizeof(total));
        for(int i = 0;i<5;++i) {
            total.g0+=values[i].g0;
            total.g1+=values[i].g1;
            total.g2+=values[i].g2;
            total.g3+=values[i].g3;
    
        }
        total.g0/=5.f;
        total.g1/=5.f;
        total.g2/=5.f;
        total.g3/=5.f;
        history_graph.add_data(0,total.g0);
        history_graph.add_data(1,total.g1);
        history_graph.add_data(2,total.g2);
        history_graph.add_data(3,total.g3);
        refresh_display();
    } 
#endif
    if(ts_count>=10 && !disconnected_label.visible()) { // 1 second
        ts_count = 0;
        index = 0;
        top_value1_label.text("---");
        refresh_display();
        top_value1_bar.value(0);
        refresh_display();
        top_value2_label.text("---");
        refresh_display();
        top_value2_bar.value(0);
        refresh_display();
        bottom_value1_label.text("---");
        refresh_display();
        bottom_value1_bar.value(0);
        refresh_display();
        bottom_value2_label.text("---");
        refresh_display();
        bottom_value2_bar.value(0);
        refresh_display();
#if LCD_HEIGHT>128
        history_graph.clear_data();
        refresh_display();
#endif
        disconnected_label.visible(true);
        refresh_display();
    }
#if defined(TOUCH_BUS) || defined(BUTTON)
    update_input();
#endif
}
