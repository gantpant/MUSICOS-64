#include <cstdint>
#include <cstddef>
#include <limine.h>
#include "cat_bitmap.hpp"
#include "cat_photo.hpp"
#include "cat_photo2.hpp"
#include "cat_photo3.hpp"

// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

namespace {

__attribute__((used, section(".limine_requests")))
volatile std::uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

}

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

namespace {

__attribute__((used, section(".limine_requests")))
volatile limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

}

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .cpp file, as seen fit.

namespace {

__attribute__((used, section(".limine_requests_start")))
volatile std::uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
volatile std::uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

}

// Halt and catch fire function, defined below.
namespace {

void hcf();

}


// The following stubs are required by the Itanium C++ ABI (the one we use,
// regardless of the "Itanium" nomenclature).
// Like the memory functions in memory.cpp, these stubs can be moved to a different .cpp file,
// but should not be removed, unless you know what you are doing.
extern "C" {
    int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
    void __cxa_pure_virtual() { hcf(); }
    void __cxa_deleted_virtual() { hcf(); }
    void *__dso_handle;
    // Not thread safe: replace before statics can be initialised concurrently.
    int __cxa_guard_acquire(std::uint64_t *guard) { return *reinterpret_cast<std::uint8_t *>(guard) == 0; }
    void __cxa_guard_release(std::uint64_t *guard) { *reinterpret_cast<std::uint8_t *>(guard) = 1; }
}

// Extern declarations for global constructors array.
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();

// The same for .ctors, which Clang 10 and older emit for x86 and which runs backwards.
extern void (*__ctors_start[])();
extern void (*__ctors_end[])();

// Halt and catch fire function.
namespace {

void hcf() {
    for (;;) {
#if defined (__x86_64__)
        asm ("hlt");
#elif defined (__aarch64__) || defined (__riscv)
        asm ("wfi");
#elif defined (__loongarch64)
        asm ("idle 0");
#endif
    }
}

// Scale an 8-bit colour channel value to the size the framebuffer gives the
// channel and move it into place within a pixel.
std::uint32_t fb_channel(std::uint8_t value, std::uint8_t mask_size, std::uint8_t mask_shift) {
    std::uint64_t max = (std::uint64_t{1} << mask_size) - 1;
    return static_cast<std::uint32_t>((value * max / 255) << mask_shift);
}
void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
uint8_t text_red = 255;
uint8_t text_green = 255;
uint8_t text_blue = 255;

void draw_pixel(limine_framebuffer* fb,
                uint64_t x, uint64_t y,
                uint8_t red, uint8_t green, uint8_t blue) {
    if (fb == nullptr || fb->address == nullptr) {
        return;
    }

    if (x >= fb->width || y >= fb->height || fb->bpp != 32) {
        return;
    }

    volatile uint32_t* pixels =
        static_cast<volatile uint32_t*>(fb->address);

    uint32_t color =
        (static_cast<uint32_t>(red) << fb->red_mask_shift) |
        (static_cast<uint32_t>(green) << fb->green_mask_shift) |
        (static_cast<uint32_t>(blue) << fb->blue_mask_shift);

    pixels[y * (fb->pitch / 4) + x] = color;
} 


void draw_photo(
    limine_framebuffer* fb,
    const uint8_t* photo,
    uint64_t photo_width,
    uint64_t photo_height,
    uint64_t screen_x,
    uint64_t screen_y
) {
    if (fb == nullptr || fb->address == nullptr)
        return;

    if (fb->bpp != 32)
        return;

    if (screen_x >= fb->width || screen_y >= fb->height)
        return;

    for (uint64_t y = 0; y < photo_height; y++) {
        for (uint64_t x = 0; x < photo_width; x++) {

            uint64_t px = screen_x + x;
            uint64_t py = screen_y + y;

            if (px >= fb->width || py >= fb->height)
                continue;

            uint64_t index = (y * photo_width + x) * 3;

            uint8_t red   = photo[index];
            uint8_t green = photo[index + 1];
            uint8_t blue  = photo[index + 2];

            draw_pixel(fb, px, py, red, green, blue);
        }
    }
}


void draw_cat(
    limine_framebuffer* fb,
    uint64_t x,
    uint64_t y,
    uint32_t scale,
    uint8_t red,
    uint8_t green,
    uint8_t blue
) {
    if (fb == nullptr || fb->address == nullptr ||
        fb->bpp != 32 || scale == 0) {
        return;
    }

    for (uint32_t py = 0; py < CAT_BITMAP_HEIGHT; py++) {
        for (uint32_t px = 0; px < CAT_BITMAP_WIDTH; px++) {

            uint32_t index = py * CAT_BITMAP_WIDTH + px;

            if ((CAT_BITMAP[index / 8] & (1u << (index % 8))) == 0) {
                continue;
            }

            for (uint32_t sy = 0; sy < scale; sy++) {
                uint64_t screen_y =
                    y + static_cast<uint64_t>(py) * scale + sy;

                if (screen_y >= fb->height) continue;

                for (uint32_t sx = 0; sx < scale; sx++) {
                    uint64_t screen_x =
                        x + static_cast<uint64_t>(px) * scale + sx;

                    if (screen_x >= fb->width) continue;

                    draw_pixel(fb, screen_x, screen_y, red, green, blue);
                }
            }
        }
    }
}
const char *get_ukrain(char symbol, int row){
    switch (symbol) {
        case ' ': {static const char *g[]={
            "00000","00000","00000","00000","00000","00000","00000"
        }; return g[row];}
        case 'F': {static const char *g[]={
            "00100","01010","10001","11111","10001","10001","10001"
        }; return g[row];}
        case ',': {static const char *g[]={
            "11111","10000","10000","11110","10001","10001","11110"
        }; return g[row];}
        case 'D': {static const char *g[]={
            "11110","10001","10001","11110","10001","10001","11110"
        }; return g[row];}
        case 'U': {static const char *g[]={
            "11111","10000","10000","10000","10000","10000","10000"
        }; return g[row];}
        case 'Q': {static const char *g[]={
            "10101","11001","10101","10011","10001","10001","10001"
        }; return g[row];}
        case 'W': {static const char *g[]={
            "10010","10010","10010","10010","10010","11111","00011"
        }; return g[row];}
        case 'E': {static const char *g[]={
            "10001", "10001", "01010", "00100","00100", "01000", "10000"
        }; return g[row];}
        case 'R': {static const char *g[]={
            "10001","10010","10100","11000","10100","10010","10001"
        }; return g[row];}
        case 'T': {static const char *g[]={
            "11111", "10000", "10000", "11110","10000", "10000", "11111"
        }; return g[row];}
        case 'Y': {static const char *g[]={
            "10001", "10001", "10001", "11111","10001", "10001", "10001"
        }; return g[row];}
        case 'I': {static const char *g[]={
            "10101","10101","10101","10101","10101","10101","11111"
        }; return g[row];}
        case 'O': {static const char *g[]={
            "10101","10101","10101","10101","10101","11111","00001"
        }; return g[row];}
        case 'P': {static const char *g[]={
            "11110","00001","00001","11110","00001","00001","11111"
        }; return g[row];}
        case '[': {static const char *g[]={
            "10001", "10001", "01010", "00100","01010", "10001", "10001"
        }; return g[row];}
        case ']': {static const char *g[]={
            "10001","00100","00100","00100","00100","00100","00100"
        }; return g[row];}
        case 'A': {static const char *g[]={
            "00100","01110","10001","10001","10001","01110","00100"
        }; return g[row];}
        case 'S': {static const char *g[]={
            "00100","00000","00100","00100","00100","00100","00100"
        }; return g[row];}
        case 'G': {static const char *g[]={
            "11111","10001","10001","10001","10001","10001","10001"
        }; return g[row];}
        case 'H': {static const char *g[]={
            "11110", "10001", "10001", "11110","10000", "10000", "10000"
        }; return g[row];}
        case 'J': {static const char *g[]={
            "01110", "10001", "10001", "10001","10001", "10001", "01110"
        }; return g[row];}
        case 'K': {static const char *g[]={
            "00111","00101","00101","00101","00101","00101","11101"
        }; return g[row];}
        case 'L': {static const char *g[]={
            "10001","11111","01010","01010","01010","01010","01110"
        }; return g[row];}
        case '"': {static const char *g[]={
            "01111","10000","10000","01111","10000","10000","01111"
        }; return g[row];}
        case 'Z': {static const char *g[]={
            "01111","10001","10001","01111","00101","01001","10001"
        }; return g[row];}
        case 'X': {static const char *g[]={
            "10001","10001","10001","01111","00001","00001","00001"
        }; return g[row];}
        case 'C': {static const char *g[]={
            "01111","10000","10000","10000","10000","10000","01111"
        }; return g[row];}
        case 'V': {static const char *g[]={
            "10001","11011","10101","10101","10001","10001","10001"
        }; return g[row];}
        case 'B': {static const char *g[]={
            "10001","10001","10011","10101","11001","10001","10001"
        }; return g[row];}
        case 'N': {static const char *g[]={
            "11111","00100","00100","00100","00100","00100","00100"
        }; return g[row];}
        case 'M': {static const char *g[]={
            "10000","10000","10000","11110","10001","10001","11110"
        }; return g[row];}
        case '.': {static const char *g[]={
            "10010","10101","10101","11101","10101","10101","10010"
        }; return g[row];}
        default: return "00000";
    }
}

const char *get_char_row(char symbol, int row){
    switch (symbol){
        case ' ': {static const char *g[]={"00000","00000","00000","00000","00000","00000","00000"}; return g[row]; }
        case 'A': {static const char *g[]={"00100","01010","10001","11111","10001","10001","10001"}; return g[row]; }
        case 'B': {static const char *g[]={"11110","10001","10001","11110","10001","10001","11110"}; return g[row]; }
        case 'C': {static const char *g[]={"01111","10000","10000","10000","10000","10000","01111"}; return g[row]; }
        case 'D': {
    static const char *g[] = {
        "11110", "10001", "10001", "10001",
        "10001", "10001", "11110"
    };
    return g[row];
}
case 'E': {
    static const char *g[] = {
        "11111", "10000", "10000", "11110",
        "10000", "10000", "11111"
    };
    return g[row];
}
case 'F': {
    static const char *g[] = {
        "11111", "10000", "10000", "11110",
        "10000", "10000", "10000"
    };
    return g[row];
}
case 'G': {
    static const char *g[] = {
        "01111", "10000", "10000", "10111",
        "10001", "10001", "01111"
    };
    return g[row];
}
case 'H': {
    static const char *g[] = {
        "10001", "10001", "10001", "11111",
        "10001", "10001", "10001"
    };
    return g[row];
}
case 'I': {
    static const char *g[] = {
        "11111", "00100", "00100", "00100",
        "00100", "00100", "11111"
    };
    return g[row];
}
case 'J': {
    static const char *g[] = {
        "00111", "00010", "00010", "00010",
        "10010", "10010", "01100"
    };
    return g[row];
}
case 'K': {
    static const char *g[] = {
        "10001", "10010", "10100", "11000",
        "10100", "10010", "10001"
    };
    return g[row];
}
case 'L': {
    static const char *g[] = {
        "10000", "10000", "10000", "10000",
        "10000", "10000", "11111"
    };
    return g[row];
}
case 'M': {
    static const char *g[] = {
        "10001", "11011", "10101", "10101",
        "10001", "10001", "10001"
    };
    return g[row];
}
case 'N': {
    static const char *g[] = {
        "10001", "11001", "10101", "10011",
        "10001", "10001", "10001"
    };
    return g[row];
}
case 'O': {
    static const char *g[] = {
        "01110", "10001", "10001", "10001",
        "10001", "10001", "01110"
    };
    return g[row];
}
case 'P': {
    static const char *g[] = {
        "11110", "10001", "10001", "11110",
        "10000", "10000", "10000"
    };
    return g[row];
}
case 'Q': {
    static const char *g[] = {
        "01110", "10001", "10001", "10001",
        "10101", "10010", "01101"
    };
    return g[row];
}
case 'R': {
    static const char *g[] = {
        "11110", "10001", "10001", "11110",
        "10100", "10010", "10001"
    };
    return g[row];
}
case 'S': {
    static const char *g[] = {
        "01111", "10000", "10000", "01110",
        "00001", "00001", "11110"
    };
    return g[row];
}
case 'T': {
    static const char *g[] = {
        "11111", "00100", "00100", "00100",
        "00100", "00100", "00100"
    };
    return g[row];
}
case 'U': {
    static const char *g[] = {
        "10001", "10001", "10001", "10001",
        "10001", "10001", "01110"
    };
    return g[row];
}
case 'V': {
    static const char *g[] = {
        "10001", "10001", "10001", "10001",
        "10001", "01010", "00100"
    };
    return g[row];
}
case 'W': {
    static const char *g[] = {
        "10001", "10001", "10001", "10101",
        "10101", "10101", "01010"
    };
    return g[row];
}
case 'X': {
    static const char *g[] = {
        "10001", "10001", "01010", "00100",
        "01010", "10001", "10001"
    };
    return g[row];
}
case 'Y': {
    static const char *g[] = {
        "10001", "10001", "01010", "00100",
        "00100", "00100", "00100"
    };
    return g[row];
}
case 'Z': {
    static const char *g[] = {
        "11111", "00001", "00010", "00100",
        "01000", "10000", "11111"
    };
    return g[row];
}
case '0': {
    static const char *g[] = {
        "01110", "10001", "10011", "10101",
        "11001", "10001", "01110"
    };
    return g[row];
}
case '1': {
    static const char *g[] = {
        "00100", "01100", "00100", "00100",
        "00100", "00100", "01110"
    };
    return g[row];
}
case '2': {
    static const char *g[] = {
        "01110", "10001", "00001", "00010",
        "00100", "01000", "11111"
    };
    return g[row];
}
case '3': {
    static const char *g[] = {
        "11110", "00001", "00001", "01110",
        "00001", "00001", "11110"
    };
    return g[row];
}
case '4': {
    static const char *g[] = {
        "00010", "00110", "01010", "10010",
        "11111", "00010", "00010"
    };
    return g[row];
}
case '5': {
    static const char *g[] = {
        "11111", "10000", "10000", "11110",
        "00001", "00001", "11110"
    };
    return g[row];
}
case '6': {
    static const char *g[] = {
        "01110", "10000", "10000", "11110",
        "10001", "10001", "01110"
    };
    return g[row];
}
case '7': {
    static const char *g[] = {
        "11111", "00001", "00010", "00100",
        "01000", "01000", "01000"
    };
    return g[row];
}
case '8': {
    static const char *g[] = {
        "01110", "10001", "10001", "01110",
        "10001", "10001", "01110"
    };
    return g[row];
}
case '9': {
    static const char *g[] = {
        "01110", "10001", "10001", "01111",
        "00001", "00001", "01110"
    };
    return g[row];
}
case ':': { static const char *g[]= {"00000","00100","00000","00000","00100","00000","00000"}; return g[row];}
case '_': { static const char *g[]= {"00000","00000","00000","00000","00000","00000","11111"}; return g[row];}
case '+': { static const char *g[]= {"00100","00100","11111","00100","00100","00100","00100"}; return g[row];}
case '.': { static const char *g[]= {"00000","00000","00000","00000","00000","00000","00100"}; return g[row];}
case '@': { static const char *g[]= {"10111","10100","10100","11111","00101","00101","11101"}; return g[row];}
case '/': { static const char *g[]= {"00001","00010","00010","00100","00100","01000","01000"}; return g[row];}
        default: return "00000";
    }
    
}
uint64_t cursor_x = 0;
uint64_t cursor_y = 0;
uint8_t background_red = 48;
uint8_t background_green = 48;
uint8_t background_blue = 48;
char command_buffer[128];
uint64_t input_len = 0;
const char* os_version = "0.2.4\n";
char saved_text[256];
uint64_t saved_text_len = 0;
char name_per[16][33];
char value[16][129];
char first_text[256];
char second_text[256];
char third_text[256];
char four_text[256];
char five_text[256];
int first_text_len = 0;
int second_text_len = 0;
int third_text_len = 0;
int four_text_len = 0;
int five_text_len = 0;
bool crestuki_noluki = false;
char cres[2][2];
volatile uint64_t timer_ticks = 0;
uint64_t r_1 = 2;
uint64_t r_2 = 2;
uint64_t r_3 = 2;
uint64_t r_4 = 2;
uint64_t r_5 = 2;
uint64_t r_6 = 2;
uint64_t r_7 = 2;
uint64_t r_8 = 2;
uint64_t r_9 = 2;
uint64_t r_10 = 2;


void draw_rect(limine_framebuffer* fb,
               uint64_t x, uint64_t y,
               uint64_t width, uint64_t height,
               uint8_t red, uint8_t green, uint8_t blue) {
    for (uint64_t py = 0; py < height; py++) {
        for (uint64_t px = 0; px < width; px++) {
            draw_pixel(fb, x + px, y + py, red, green, blue);
        }
    }
}

void draw_char(limine_framebuffer* fb, uint64_t x, uint64_t y, char symbol, uint64_t scale) {
    for (int row = 0; row <7; row++) {
        const char* pixels = get_char_row(symbol, row);
        for (int col = 0;col <5; col++) {
            if (pixels[col] == '1') {
                for (uint64_t sy = 0; sy < scale; sy++) {
                    for (uint64_t sx =0; sx< scale; sx++) {
                        draw_pixel(fb,x+col * scale +sx,y+row * scale +sy,text_red,text_green,text_blue);        
                    }
                }
            }
        }
    }
}
void draw_char_ukr(limine_framebuffer* fb, uint64_t x, uint64_t y, char symbol, uint64_t scale) {
    for (int row = 0; row <7; row++) {
        const char* pixels = get_ukrain(symbol, row);
        for (int col = 0;col <5; col++) {
            if (pixels[col] == '1') {
                for (uint64_t sy = 0; sy < scale; sy++) {
                    for (uint64_t sx =0; sx< scale; sx++) {
                        draw_pixel(fb,x+col * scale +sx,y+row * scale +sy,text_red,text_green,text_blue);        
                    }
                }
            }
        }
    }
}

void draw_text(limine_framebuffer* fb, const char* text, uint64_t scale) {
    int let = 0;
    uint64_t x_start = cursor_x;
    for (int i = 0; text[i] !='\0'; i++) {
        let += 1;
    }
    for (int i =0; i < let; i++) {
        if (text[i] == '\n') {
            cursor_y += 7 * scale + 1 * scale;
            cursor_x = x_start;
        }
        if (text[i] == '\0') {
            cursor_y += 7 * scale + 1 * scale;
            cursor_x = x_start;
        }
        if (cursor_x + 5 * scale > fb->width) {
            cursor_y = cursor_y + 7 * scale + 1 * scale;
            cursor_x = x_start;
        }
        if (text[i] != '\n') {
            draw_char(fb, cursor_x, cursor_y,text[i],scale);
            cursor_x += 5 * scale + 1 * scale;
        }
    }
    
}
void draw_text_location(limine_framebuffer* fb, const char* text, uint64_t scale, uint64_t cursor_x, uint64_t cursor_y) {
    int let = 0;
    uint64_t x_start = cursor_x;
    for (int i = 0; text[i] !='\0'; i++) {
        let += 1;
    }
    for (int i =0; i < let; i++) {
        if (text[i] == '\n') {
            cursor_x = x_start;
        }
        if (text[i] == '\0') {
            cursor_x = x_start;
        }
        if (cursor_x + 5 * scale > fb->width) {
            cursor_x = x_start;
        }
        if (text[i] != '\n') {
            draw_char(fb, cursor_x, cursor_y,text[i],scale);
            cursor_x += 5 * scale + 1 * scale;
        }
    }
    
}
void draw_text_ukr(limine_framebuffer* fb, const char* text, uint64_t scale) {
    int let = 0;
    uint64_t x_start = cursor_x;
    for (int i = 0; text[i] !='\0'; i++) {
        let += 1;
    }
    for (int i =0; i < let; i++) {
        if (text[i] == '\n') {
            cursor_y += 7 * scale + 1 * scale;
            cursor_x = x_start;
        }
        if (text[i] == '\0') {
            cursor_y += 7 * scale + 1 * scale;
            cursor_x = x_start;
        }
        if (cursor_x + 5 * scale > fb->width) {
            cursor_y = cursor_y + 7 * scale + 1 * scale;
            cursor_x = x_start;
        }
        if (text[i] != '\n') {
            draw_char_ukr(fb, cursor_x, cursor_y,text[i],scale);
            cursor_x += 5 * scale + 1 * scale;
        }
    }
    
}
void clear_screen(limine_framebuffer* fb, int ter) {
    draw_rect(fb,0,0,fb->width,fb->height,background_red,background_green,background_blue);
    cursor_x = 0;
    cursor_y = 0;
    if (ter > 0) {
        cursor_x = 0;
        cursor_y = 0;
    }
}
uint8_t inb(uint16_t port) {
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}
uint8_t get_second() {
    outb(0x70, 0x00);
    uint8_t time = inb(0x71);
    return time;
}
uint8_t get_minute() {
    outb(0x70, 0x02);
    uint8_t time = inb(0x71);
    return time;
}
uint8_t get_hour() {
    outb(0x70, 0x04);
    uint8_t time = inb(0x71);
    return time;
}
uint8_t get_day() {
    outb(0x70, 0x07);
    uint8_t time = inb(0x71);
    return time;
}
uint8_t get_month() {
    outb(0x70, 0x08);
    uint8_t time = inb(0x71);
    return time;
}
uint8_t get_year() {
    outb(0x70, 0x09);
    uint8_t time = inb(0x71);
    return time;
}
uint8_t bcd_to_decimal(uint8_t value) {
    uint8_t hight = value >> 4;
    uint8_t low = value & 0x0F;
    uint8_t result = hight * 10 + low;
    return result;
}
// час торби
uint8_t second = bcd_to_decimal(get_second());
uint8_t minute = bcd_to_decimal(get_minute());
uint8_t hour = bcd_to_decimal(get_hour());
uint8_t day = bcd_to_decimal(get_day());
uint8_t month = bcd_to_decimal(get_month());
uint8_t year = bcd_to_decimal(get_year());

bool keyboard_ready() {
    return (inb(0x64) & 1) != 0;
}
uint8_t keyboard_read() {
    return inb(0x60);
}

bool shift_pressed = false;

char scancode_to_char(uint8_t code) {
    switch (code) {
        case 0x1E: return shift_pressed ? 'A' : 'A';
        case 0x30: return shift_pressed ? 'B' : 'B';
        case 0x2E: return shift_pressed ? 'C' : 'C';
        case 0x20: return 'D';
        case 0x12: return 'E';
        case 0x21: return 'F';
        case 0x22: return 'G';
        case 0x23: return 'H';
        case 0x17: return 'I';
        case 0x24: return 'J';
        case 0x25: return 'K';
        case 0x26: return 'L';
        case 0x32: return 'M';
        case 0x31: return 'N';
        case 0x18: return 'O';
        case 0x19: return 'P';
        case 0x10: return 'Q';
        case 0x13: return 'R';
        case 0x1F: return 'S';
        case 0x14: return 'T';
        case 0x16: return 'U';
        case 0x2F: return 'V';
        case 0x11: return 'W';
        case 0x2D: return 'X';
        case 0x15: return 'Y';
        case 0x2C: return 'Z';

        case 0x0B: return '0';
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';

        case 0x39: return ' ';
        default: return '\0';
    }
}
void reset_shell(limine_framebuffer* fb) {
    input_len = 0;
    for (int i = 0; i < sizeof command_buffer; i++) {
        command_buffer[i] = '\0';
    }
    clear_screen(fb, 1);
}
void command_buffer_clear() {
    for (int i = 0; i < sizeof command_buffer; i++) {
        command_buffer[i] = '\0';
    }
}

void echo_text(limine_framebuffer* fb) {
    int scale =2;
    int let = 5;
    for (int i = 5; command_buffer[i] !='\0'; i++) {
        let += 1;
    }
    for (int i = 5; i < let; i++) {
        char ch = command_buffer[i];
        draw_char(fb,cursor_x,cursor_y, ch, 2);
        cursor_x += 5 * scale + 1 * scale;
    }
    cursor_y += 7 * scale + 1 * scale;
    cursor_x = 0;
}
void load_text(limine_framebuffer* fb) {
    if (saved_text_len > 0) {
        draw_text(fb, saved_text, 2);
        draw_text(fb, "\n", 2); 
        cursor_x = 0;
    }
    else {
        draw_text(fb, "NO SAVED TEXT\n", 2);
    }
}
void loading_text(limine_framebuffer* fb, int i) {
    if (i == 1) {
        if (first_text_len > 0) {
            draw_text(fb, first_text, 2);
        }   
    }
    if (i == 2) {
        if (second_text_len > 0) {
            draw_text(fb, second_text, 2);
        }
    }
    if (i == 3) {
        if (third_text_len > 0) {
            draw_text(fb, third_text, 2);
        }
    }
    if (i == 4) {
        if (four_text_len > 0) {
            draw_text(fb, four_text, 2);
        }
    }
    if (i == 5) {
        if (five_text_len > 0) {
            draw_text(fb, five_text, 2);
        }
    }

}
void number_to_string(uint64_t number, char* buffer) {

    int digits = 0;
    uint64_t n_n = number;
    if (n_n == 0) {
        digits = 1;
    }
    while (n_n > 0) {
        n_n /= 10;
        digits += 1;
    }
    for (int i = 0; i != digits; i++) {
        char gg = number % 10;
        gg += '0';
        number /= 10;
        buffer[i] = gg;
    }
    

    
    int right = digits - 1;
    for (int left = 0; left < right; left++, right--) {
        char timeout;
        timeout = buffer[right];
        buffer[right] = buffer[left];
        buffer[left] = timeout;
    }
    buffer[digits] = '\0';
}
int asdf =0;

void user_value(int asd, const char* second) {
    int a = 0;
    for (int i =0; i < 128; i++) {
        if (second[i] == '\0') {
            break;
        }
        value[asd][i] = second[i];
        a +=1;
    }
    value[asd][a] = '\0';
}

void user_per(limine_framebuffer fb, const char* first, const char* second){
    int asd = -1;
    int abc = -1;
    for (int i = 0; i <16; i++) {
        if (name_per[i][0] == '\0') {
            asd = i;
            break;
        }
    }
    if (asd == -1) {
        return;
    }
    for (int i = 0; i < 32; i++) {
        if (first[i] == '\0') {
            break;
        }
        else {
            name_per[asd][i] = first[i];
        }
        abc = i;
    }
    name_per[asd][abc + 1] = '\0';
    user_value(asd, second) ;
}

uint64_t step(uint64_t number, uint64_t step) {
    uint64_t number_old = number;
    for (int i = 0; i < step - 1; i++) {
        number *= number_old; 
    }
    return number;
}

uint64_t random(uint64_t min, uint64_t max) {
    uint64_t text_blue_t = text_blue;
    uint64_t text_green_t = text_green;
    uint64_t text_red_t = text_red;
    uint64_t saved_text_len_t = saved_text_len;
    uint64_t first_text_len_t = first_text_len;
    uint64_t second_text_len_t = second_text_len;
    uint64_t third_text_len_t = third_text_len;
    uint64_t four_text_len_t = four_text_len;
    uint64_t five_text_len_t = five_text_len;
    uint64_t cursor_x_t = cursor_x;
    uint64_t cursor_y_t = cursor_y;

    if (text_blue_t <= 0) {
        text_blue_t = 0;
        text_blue_t += 2;
    }
    if (text_green_t <= 0) {
        text_green_t = 0;
        text_green_t += 2;
    }
    if (text_red_t <= 0) {
        text_red_t  = 0;
        text_red_t += 2;
    }
    if (saved_text_len_t <= 0) {
        saved_text_len_t = 0;
        saved_text_len_t += 2;
    }
    if (first_text_len_t <= 0) {
        first_text_len_t = 0;
        first_text_len_t += 2;
    }
    if (second_text_len_t <=0) {
        second_text_len_t = 0;
        second_text_len_t +=2;
    }
    if (third_text_len_t <= 0) {
        third_text_len_t = 0;
        third_text_len_t +=2;
    }
    if (four_text_len_t <= 0) {
        four_text_len_t = 0;
        four_text_len_t +=2;
    }
    if (five_text_len_t <= 0) {
        five_text_len_t =0;
        five_text_len_t += 2;
    }
    if (cursor_x_t <= 0) {
        cursor_x_t =0;
        cursor_x += 2;
    }
    if (cursor_y_t <= 0) {
        cursor_y_t = 0;
        cursor_y_t +=2;
    }
    uint64_t number = text_blue_t * text_green_t * text_red_t * saved_text_len_t * first_text_len_t * second_text_len_t * third_text_len_t * four_text_len_t * five_text_len_t * r_1 * r_2 * r_3 * r_4 * r_5 *r_6 * r_7 * r_8 * r_9 * r_10; 
    number = step(number, 2);
    for (uint64_t i = number; i > max; i++) {
        i /= 10;
        number = i;
    }
    return number;
}


void execute_command(limine_framebuffer* fb){
    uint64_t scale = 2;
    if (command_buffer[0] == 'H' && command_buffer[1] == 'E' && command_buffer[2] == 'L' && command_buffer[3] == 'P' && command_buffer[4] == '\0') {
        draw_text(fb, "AVAILABLE COMMANDS\n", 2);
        draw_text(fb, "HELP SHOW COMMANDS\n",2);
        draw_text(fb, "ABOUT ABOUT OS\n",2);
        draw_text(fb, "CLEAR CLEAR SCREEN\n", 2);
        draw_text(fb, "REBOOT REBOOT\n", 2);
        draw_text(fb, "VERSION VERSION OS\n", 2);
        draw_text(fb, "ECHO ECHO TEXT\n", 2);
        draw_text(fb, "COLOR RED RED TEXT\n", 2);
        draw_text(fb, "COLOR GREEN GREEN TEXT\n", 2);
        draw_text(fb, "COLOR BLUE BLUE TEXT\n", 2);
        draw_text(fb, "COLOR RESET WHITE TEXT\n", 2);
        draw_text(fb, "COLOR CUSTOM CUSTOM COLOR FOR TEXT\n", 2);
        draw_text(fb, "SAVE SAVE TEXT\n", 2);
        draw_text(fb, "LOAD  LOAD TEXT\n", 2);
        draw_text(fb, "DELETE DELETE TEXT\n", 2);
        draw_text(fb, "APPEND ADD TEXT TO TEXT\n", 2);
        draw_text(fb, "LIST INFORMATION PRO TEXT\n",2);
    } else if (command_buffer[0] == 'A' && command_buffer[1] == 'B' && command_buffer[2] == 'O' && command_buffer[3] == 'U' && command_buffer[4] == 'T' && command_buffer[5] == '\0') {
        draw_text(fb, "MUSICOS64\n", 2);
        draw_text(fb, "A CUSTOM OPERATING SYSTEM\n", 2);
        draw_text(fb, "MUSICO64 IS A C++ KERNEL\n",2);
        draw_text(fb, "ARCHITECTURE: X86_64\n",2);
        
    } else if (command_buffer[0] == 'C' && command_buffer[1] == 'L' && command_buffer[2] == 'E' && command_buffer[3] == 'A' && command_buffer[4] == 'R' && command_buffer[5] == '\0') {
        clear_screen(fb,1);
        
    } else if (command_buffer[0] == 'R' && command_buffer[1] == 'E' && command_buffer[2] == 'B' && command_buffer[3] == 'O' && command_buffer[4] == 'O' && command_buffer[5] == 'T' && command_buffer[6] == '\0') {
        reset_shell(fb);
        
    } else if (command_buffer[0] == 'V' && command_buffer[1] == 'E' && command_buffer[2] == 'R' && command_buffer[3] == 'S' && command_buffer[4] == 'I' && command_buffer[5] == 'O' && command_buffer[6] == 'N' && command_buffer[7] == '\0') {
        draw_text(fb, os_version, 2);
        
    } else if (command_buffer[0] == 'E' && command_buffer[1] == 'C' && command_buffer[2] == 'H' && command_buffer[3] == 'O' && command_buffer[4] == ' ') {
        echo_text(fb);
        
    } else if (command_buffer[0] == 'C' && command_buffer[1] == 'O' && command_buffer[2] == 'L' && command_buffer[3] == 'O' && command_buffer[4] == 'R' && command_buffer[5]== ' ' && command_buffer[6] == 'R' && command_buffer[7] == 'E' && command_buffer[8] == 'D' && command_buffer[9] == '\0') {
        text_red = 255;
        text_green = 0;
        text_blue = 0;
        
    } else if (command_buffer[0] == 'C' && command_buffer[1] == 'O' && command_buffer[2] == 'L' && command_buffer[3] == 'O' && command_buffer[4] == 'R' && command_buffer[5]== ' ' && command_buffer[6] == 'G' && command_buffer[7] == 'R' && command_buffer[8] == 'E' && command_buffer[9] == 'E' && command_buffer[10] == 'N' && command_buffer[11] == '\0') {
        text_red = 0;
        text_green = 255;
        text_blue = 0;
        
    } else if (command_buffer[0] == 'C' && command_buffer[1] == 'O' && command_buffer[2] == 'L' && command_buffer[3] == 'O' && command_buffer[4] == 'R' && command_buffer[5]== ' ' && command_buffer[6] == 'B' && command_buffer[7] == 'L' && command_buffer[8] == 'U' && command_buffer[9] == 'E' && command_buffer[10] == '\0') {
        text_red = 0;
        text_green = 0;
        text_blue = 255;
        
    } else if (command_buffer[0] == 'C' && command_buffer[1] == 'O' && command_buffer[2] == 'L' && command_buffer[3] == 'O' && command_buffer[4] == 'R' && command_buffer[5]== ' ' && command_buffer[6] == 'R' && command_buffer[7] == 'E' && command_buffer[8] == 'S' && command_buffer[9] == 'E' && command_buffer[10] == 'T' && command_buffer[11] == '\0') {
        text_red = 255;
        text_green = 255;
        text_blue = 255;
        
    } else if (command_buffer[0] == 'C' && command_buffer[1] == 'O' && command_buffer[2] == 'L' && command_buffer[3] == 'O' && command_buffer[4] == 'R' && command_buffer[5] == ' ' && command_buffer[6] == 'C' && command_buffer[7] == 'U' && command_buffer[8] == 'S' && command_buffer[9] == 'T' && command_buffer[10] == 'O' && command_buffer[11] == 'M' && command_buffer[12] == ' ' && command_buffer[24] =='\0') {
        if (command_buffer[15] == 'N') {
            if (command_buffer[14] == 'N') {
                char no_num_red = command_buffer[13];
                uint8_t red = no_num_red - '0';
                text_red = red;
            }
            else {
                char no_num_red = command_buffer[13] + command_buffer[14];
                uint8_t red = no_num_red - '0';
                text_red = red; 
            }
        }
        else {
            char no_num_red = command_buffer[13] + command_buffer[14] + command_buffer[15];
            uint8_t red = no_num_red - '0';
            text_red = red;
        }
        if (command_buffer[19] == 'N') {
            if (command_buffer[18] == 'N'){
                char no_num_green = command_buffer[17];
                uint8_t green = no_num_green - '0';
                text_green = green;
            }
            else {
                char no_num_green = command_buffer[17] + command_buffer[18];
                uint8_t green = no_num_green - '0';
                text_green = green;
            }
        } 
        else {
            char no_num_green = command_buffer[17] + command_buffer[18] + command_buffer[19];
            uint8_t green = no_num_green - '0';
            text_green = green;
        }
        if (command_buffer[23] == 'N') {
            if (command_buffer[22] == 'N') {
                char no_num_blue = command_buffer[21];
                uint8_t blue = no_num_blue - '0';
                text_blue = blue;
            }
            else {
                char no_num_blue = command_buffer[21] + command_buffer[22];
                uint8_t blue = no_num_blue - '0';
                text_blue = blue;
            }
        }
        else {
            char no_num_blue = command_buffer[21] + command_buffer[22] + command_buffer[23];
            uint8_t blue = no_num_blue - '0';
            text_blue = blue;
        }
        
    }
    else if (command_buffer[0] == 'S' && command_buffer[1] == 'A' && command_buffer[2] == 'V' && command_buffer[3] == 'E' && command_buffer[4] == ' ') {
        int in = 0;
        saved_text_len = 0;
        for (int i = 5; command_buffer[i] != '\0'; i++) {
            saved_text_len += 1;
        }
        if (saved_text_len > 255) {
            saved_text_len = 255;
        }
        for (int i = 5; in < saved_text_len; i++) {
            saved_text[in] = command_buffer[i];
            in += 1;
        }
        saved_text[saved_text_len] = '\0';
        
    }
    else if (command_buffer[0] == 'L' && command_buffer[1] == 'O' && command_buffer[2] == 'A' && command_buffer[3] == 'D' && command_buffer[4] == '\0') {
        load_text(fb);
        
    } else if (command_buffer[0] == 'D' && command_buffer[1] == 'E' && command_buffer[2] == 'L' && command_buffer[3] == 'E' && command_buffer[4] == 'T' && command_buffer[5] =='E' && command_buffer[6] == '\0') {
        for (int i =0; i <255; i++) {
            saved_text[i] = '\0';
            
        }
    } else if (command_buffer[0] == 'A' && command_buffer[1] == 'P' && command_buffer[2] == 'P' && command_buffer[3] == 'E' && command_buffer[4] == 'N' && command_buffer[5] == 'D' && command_buffer[6] == ' ') {
        int in = saved_text_len;
        for (int i = 7; command_buffer[i] != '\0'; i++) {
            saved_text_len += 1;
        }
        if (saved_text_len > 255) {
            saved_text_len = 255;
        }
        for (int i = 7; in < saved_text_len; i++) {
            saved_text[in] = command_buffer[i];
            in += 1;
        }
        saved_text[saved_text_len] = '\0';
        
    } else if (command_buffer[0] == 'L' && command_buffer[1] == 'I' && command_buffer[2] == 'S' && command_buffer[3] == 'T' && command_buffer[4] == '\0') {
        char buffer[21];
        uint64_t free_space = 255 - saved_text_len;
        uint64_t free_space_first = 255 - first_text_len;
        uint64_t free_space_second = 255 - second_text_len;
        uint64_t free_space_third = 255 - third_text_len;
        uint64_t free_space_four = 255 - four_text_len;
        uint64_t free_space_five = 255 - five_text_len;
        
        draw_text(fb, "STORAGE INFORMATION\n", 2);
        if (saved_text_len > 0 || first_text_len > 0 || second_text_len > 0 || third_text_len > 0 || four_text_len > 0 || five_text_len > 0)  {
            draw_text(fb, "STATUS: DATA SAVED\n", 2);
        }
        else {
            draw_text(fb, "STATUS: DATA NOT SAVED\n", 2);
        }
        number_to_string(saved_text_len, buffer);
        draw_text(fb, "TEXT LENGTH ON NULL DISK: ", 2);
        draw_text(fb, buffer,2);
        cursor_y += 7 *scale + 1 * scale;
        cursor_x = 0;
        for (int i = 0; i < 21; i++) {
            buffer[i] = '\0';
        }
        number_to_string(first_text_len, buffer);
        draw_text(fb, "TEXT LENGHT ON FIRST DISK ", 2);
        draw_text(fb, buffer,2);
        cursor_y += 7 *scale + 1 * scale;
        cursor_x = 0;
        for (int i = 0; i != 21; i++) {
            buffer[i] = '\0';
        }
        number_to_string(second_text_len, buffer);
        draw_text(fb, "TEXT LENGHT ON SECOND DISK ", 2);
        draw_text(fb, buffer, 2);
        cursor_y += 7 *scale + 1 * scale;
        cursor_x = 0;
        for (int i = 0; i != 21; i++) {
            buffer[i] = '\0';
        }
        number_to_string(third_text_len, buffer);
        draw_text(fb, "TEXT LENGHT IN THIRD DISK ", 2);
        draw_text(fb, buffer, 2);
        cursor_y += 7 *scale + 1 * scale;
        cursor_x = 0;
        for (int i = 0; i != 21; i++) {
            buffer[i] = '\0';
        }
        number_to_string(four_text_len, buffer);
        draw_text(fb, "TEXT LENGHT IN FOUR DISK ", 2);
        draw_text(fb, buffer, 2);
        cursor_y += 7 *scale + 1 * scale;
        cursor_x = 0;
        for (int i = 0; i != 21; i++) {
            buffer[i] = '\0';
        }
        number_to_string(five_text_len, buffer);
        draw_text(fb, "TEXT LENGHT IN FIVE DISK ", 2);
        draw_text(fb, buffer, 2);
        cursor_y += 7 *scale + 1 * scale;
        cursor_x = 0;
        for (int i = 0; i != 21; i++) {
            buffer[i] = '\0';
        }

        number_to_string(free_space, buffer);
        draw_text(fb, "FREE SPACE ON NULL DISK: ", 2);
        draw_text(fb, buffer,2);
        cursor_y += 7 *scale + 1 * scale;
        cursor_x = 0;
        for (int i = 0; i != 21; i++) {
            buffer[i] = '\0';
        }
        number_to_string(free_space_first, buffer);
        draw_text(fb, "FREE SPACE ON FIRST DISK: ", 2);
        draw_text(fb, buffer, 2);
        cursor_y += 7 *scale + 1 * scale;
        cursor_x = 0;
        for (int i = 0; i != 21; i++) {
            buffer[i] = '\0';
        }
        number_to_string(free_space_second, buffer);
        draw_text(fb, "FREE SPACE ON SECOND DISK ", 2);
        draw_text(fb, buffer, 2);
        cursor_y += 7 *scale + 1 * scale;
        cursor_x = 0;
        for (int i = 0; i != 21; i++) {
            buffer[i] = '\0';
        }
        number_to_string(free_space_third, buffer);
        draw_text(fb, "FREE SPACE ON THIRD DISK ", 2);
        draw_text(fb, buffer, 2);
        cursor_y += 7 *scale + 1 * scale;
        cursor_x = 0;
        for (int i = 0; i != 21; i++) {
            buffer[i] = '\0';
        }
        number_to_string(free_space_four, buffer);
        draw_text(fb, "FREE SPACE ON FOUR DISK ", 2);
        draw_text(fb, buffer, 2);
        cursor_y += 7 *scale + 1 * scale;
        cursor_x = 0;
        for (int i = 0; i != 21; i++) {
            buffer[i] = '\0';
        }
        number_to_string(free_space_five, buffer);
        draw_text(fb, "FREE SPACE ON FIVE DISK ", 2);
        draw_text(fb, buffer, 2);
        cursor_y += 7 *scale + 1 * scale;
        cursor_x = 0;
        for (int i = 0; i != 21; i++) {
            buffer[i] = '\0';
        }
        draw_text(fb, "CAPACITY: 1530 BYTES\n", 2);
        
    } else if (command_buffer[0]== 'Z' && command_buffer[1] == 'I' && command_buffer[2] == 'G') {
        draw_cat(fb, 100, 50, 2, 0, 0, 0);
        draw_photo(fb,cat_photo,PHOTO_WIDTH,PHOTO_HEIGHT,380,0);
        draw_photo(fb,cat_photo2,PHOTO2_WIDTH,PHOTO2_HEIGHT,760,0);
        draw_text_ukr(fb, "LF Z WT PHJ,BD", 2);
        draw_photo(fb, cat_photo3, 240, 320, 980, 400);
    }else if (command_buffer[0] == 'D' && command_buffer[1]== 'S' && command_buffer[2] == 'A' && command_buffer[3] == 'V' && command_buffer[4]== 'E' && command_buffer[5] == ' ' && command_buffer[7]== ' ') {
        if (command_buffer[6] == '1') {
            for (int i =0; i<255; i++) {
                first_text[i] = '\0';
            }
            first_text_len = 0;
            for (int i = 8; command_buffer[i] != '\0'; i++) {
                first_text[first_text_len] = command_buffer[i];
                first_text_len += 1;
            } 
        }
        else if (command_buffer[6] == '2') {
            for (int i =0; i < 255; i++) {
                second_text[i] = '\0';
            }
            second_text_len = 0;
            for (int i = 8; command_buffer[i] != '\0'; i++) {
                second_text[second_text_len] = command_buffer[i];
                second_text_len += 1;
            } 
        }
        else if (command_buffer[6] == '3') {
            for (int i = 0; i < 255; i++) {
                third_text[i] = '\0';
            }
            third_text_len = 0;
            for (int i = 8; command_buffer[i] != '\0'; i++) {
                third_text[third_text_len] = command_buffer[i];
                third_text_len += 1;
            } 
        }
        else if (command_buffer[6] == '4') {
            for (int i = 0; i < 255; i++) {
                four_text[i] = '\0';
            }
            four_text_len = 0;
            for (int i = 8; command_buffer[i] != '\0'; i++) {
                four_text[four_text_len] = command_buffer[i];
                four_text_len += 1;
            } 
        }
        else if (command_buffer[6] == '5') {
            for (int i = 0; i < 255; i++) {
                five_text[i] = '\0';
            }
            five_text_len = 0;
            for (int i = 8; command_buffer[i] != '\0'; i++) {
                five_text[five_text_len] = command_buffer[i];
                five_text_len += 1;
            } 
        }
        if (first_text_len > 255) {
            saved_text_len = 255;
        }
        else if (second_text_len > 255) {
            second_text_len = 255;
        }
        else if (third_text_len > 255) {
            second_text_len = 255;
        }
        else if (four_text_len > 255) {
            second_text_len = 255;
        }
        else if (five_text_len > 255) {
            second_text_len = 255;
        }
        first_text[first_text_len] = '\0';
        second_text[second_text_len] = '\0';
        third_text[third_text_len] = '\0';
        four_text[four_text_len] = '\0';
        five_text[five_text_len] = '\0';
    } else if (command_buffer[0] == 'D' && command_buffer[1] == 'L' && command_buffer[2] == 'O' && command_buffer[3] == 'A' && command_buffer[4] == 'D' && command_buffer[5] == ' ' && command_buffer[7] == '\0') {
        if (command_buffer[6] == '1') {
            if (first_text[0] != '\0') {
                loading_text(fb, 1);
            }
            else {
                draw_text(fb, "NO TEXT SAVED", 2);
            }
        }
        else if (command_buffer[6] == '2'){
            if (second_text[0] != '\0') {
                loading_text(fb, 2);
            }
            else {
                draw_text(fb, "NO TEXT SAVED", 2);
            }
        }
        else if (command_buffer[6] == '3'){
            if (third_text[0] != '\0') {
                loading_text(fb, 3);
            }
            else {
                draw_text(fb, "NO TEXT SAVED", 2);
            }
        }
        else if (command_buffer[6] == '4'){
            if (four_text[0] != '\0') {
                loading_text(fb, 4);
            }
            else {
                draw_text(fb, "NO TEXT SAVED", 2);
            }
        }
        else if (command_buffer[6] == '5'){
            if (five_text[0] != '\0') {
                loading_text(fb, 5);
            }
            else {
                draw_text(fb, "NO TEXT SAVED", 2);
            }
        }
        else {
            draw_text(fb, "NO", 2);
        }
        cursor_x = 0;
        cursor_y += 7 *scale + 1 *scale;
    } else if (command_buffer[0] == 'T' && command_buffer[1] == 'E' && command_buffer[2] == 'S' && command_buffer[3] == 'T' && command_buffer[4] == '\0') {
        char buffer[21];
        uint64_t test = random(0, 100);
        number_to_string(test, buffer);
        draw_text(fb, buffer,2);
        cursor_x = 0;
        cursor_y += 7 * scale + 1 * scale;
    }
    //не трогать це непрацюючий команда калькулятора
    else if (command_buffer[0] == 'A' && command_buffer[1] == 'D' && command_buffer[2] == 'D' && command_buffer[3] == ' ') {
        char buffer[21];
        uint64_t old_number = 0;
        uint64_t number = 0;
        uint64_t rind =0;
        uint64_t old_number_2 =0;
        uint64_t number_2 = 0;
        int aber = 0;
        for (int i = 4; command_buffer[i] != ' '; i++) {
            if (command_buffer[i] == '1') {
                rind =1;
                old_number = number;
                number = old_number *10 +rind;
            }
            if (command_buffer[i] == '2') {
                rind =2;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '3') {
                rind =3;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '4') {
                rind =4;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '5') {
                rind =5;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '6') {
                rind =6;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '7') {
                rind =7;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '8') {
                rind =8;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '9') {
                rind =9;
                old_number = number;
                number = old_number *10 + rind;
            }
            aber = i;
        }
        for (int i = aber + 1; command_buffer[i] != '\0'; i++) {
            if (command_buffer[i] == '1') {
                rind =1;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 +rind;
            }
            if (command_buffer[i] == '2') {
                rind =2;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '3') {
                rind =3;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '4') {
                rind =4;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '5') {
                rind =5;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '6') {
                rind =6;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '7') {
                rind =7;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '8') {
                rind =8;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '9') {
                rind =9;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
        }
        for (int i = 0; i <21; i++) {
            buffer[i] = '\0';
        }
        uint64_t result = number + number_2;
        number_to_string(result, buffer);
        draw_text(fb, buffer, 2);
        cursor_y += 7 * scale + 1 *scale;
        cursor_x = 0;
    }
    else if (command_buffer[0] == 'G' && command_buffer[1] == 'A' && command_buffer[2] == 'M' && command_buffer[3] == 'E' && command_buffer[4] == ' ' && command_buffer[5] == '1' && command_buffer[6] == '\0'){

    }
    else if (command_buffer[0] == 'W' && command_buffer[1] == 'E' && command_buffer[2] == 'B' && command_buffer[3] == ' ' && command_buffer[4] == 'I' && command_buffer[5] == 'N' && command_buffer[6] == 'F' && command_buffer[7] == 'O' && command_buffer[8] == '\0'){
        draw_text(fb, "WEB SITE MINI LETER GANTPANT.GITHUB.IO/MUSICOS-64/", 2);   
    } 
    else if (command_buffer[0] == 'C' && command_buffer[1] == 'A' && command_buffer[2] == 'S' && command_buffer[3] == 'I' && command_buffer[4] == 'N' && command_buffer[5] == 'O' && command_buffer[6] == '\0') {
        
    }
    else if (command_buffer[0] == 'S' && command_buffer[1] == 'U' &&command_buffer[2] == 'B' && command_buffer[3] == 'T' && command_buffer[4] == 'R' && command_buffer[5] == 'A' && command_buffer[6] == 'C' &&command_buffer[7] == 'T') {
        char buffer[21];
        uint64_t old_number = 0;
        uint64_t number = 0;
        uint64_t rind =0;
        uint64_t old_number_2 =0;
        uint64_t number_2 = 0;
        int aber = 0;
        for (int i = 9; command_buffer[i] != ' '; i++) {
            if (command_buffer[i] == '1') {
                rind =1;
                old_number = number;
                number = old_number *10 +rind;
            }
            if (command_buffer[i] == '2') {
                rind =2;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '3') {
                rind =3;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '4') {
                rind =4;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '5') {
                rind =5;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '6') {
                rind =6;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '7') {
                rind =7;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '8') {
                rind =8;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '9') {
                rind =9;
                old_number = number;
                number = old_number *10 + rind;
            }
            aber = i;
        }
        for (int i = aber + 1; command_buffer[i] != '\0'; i++) {
            if (command_buffer[i] == '1') {
                rind =1;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 +rind;
            }
            if (command_buffer[i] == '2') {
                rind =2;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '3') {
                rind =3;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '4') {
                rind =4;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '5') {
                rind =5;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '6') {
                rind =6;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '7') {
                rind =7;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '8') {
                rind =8;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '9') {
                rind =9;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
        }
        for (int i = 0; i <21; i++) {
            buffer[i] = '\0';
        }
        uint64_t result = number - number_2;
        number_to_string(result, buffer);
        draw_text(fb, buffer, 2);
        cursor_y += 7 * scale + 1 *scale;
        cursor_x = 0;
    }
    // multiplication
    else if (command_buffer[0] == 'M' && command_buffer[1] == 'U' && command_buffer[2] == 'L' && command_buffer[3] == 'T' && command_buffer[4] == 'I' && command_buffer[5] == 'P' && command_buffer[6] == 'L' && command_buffer[7] == 'I' && command_buffer[8] == 'C' && command_buffer[9] == 'A' && command_buffer[10] == 'T' && command_buffer[11] == 'I' &&command_buffer[12] == 'O' && command_buffer[13] == 'N') {
        char buffer[21];
        uint64_t old_number = 0;
        uint64_t number = 0;
        uint64_t rind =0;
        uint64_t old_number_2 =0;
        uint64_t number_2 = 0;
        int aber = 0;
        for (int i = 15; command_buffer[i] != ' '; i++) {
            if (command_buffer[i] == '1') {
                rind =1;
                old_number = number;
                number = old_number *10 +rind;
            }
            if (command_buffer[i] == '2') {
                rind =2;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '3') {
                rind =3;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '4') {
                rind =4;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '5') {
                rind =5;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '6') {
                rind =6;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '7') {
                rind =7;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '8') {
                rind =8;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '9') {
                rind =9;
                old_number = number;
                number = old_number *10 + rind;
            }
            aber = i;
        }
        for (int i = aber + 1; command_buffer[i] != '\0'; i++) {
            if (command_buffer[i] == '1') {
                rind =1;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 +rind;
            }
            if (command_buffer[i] == '2') {
                rind =2;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '3') {
                rind =3;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '4') {
                rind =4;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '5') {
                rind =5;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '6') {
                rind =6;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '7') {
                rind =7;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '8') {
                rind =8;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '9') {
                rind =9;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
        }
        for (int i = 0; i <21; i++) {
            buffer[i] = '\0';
        }
        uint64_t result = number * number_2;
        number_to_string(result, buffer);
        draw_text(fb, buffer, 2);
        cursor_y += 7 * scale + 1 *scale;
        cursor_x = 0;
    }
    else if (command_buffer[0] == 'D' && command_buffer[1] == 'I' && command_buffer[2] == 'V' && command_buffer[3] == 'I' && command_buffer[4] == 'S' && command_buffer[5] == 'I' && command_buffer[6] == 'O' && command_buffer[7] == 'N') {
        char buffer[21];
        uint64_t old_number = 0;
        uint64_t number = 0;
        uint64_t rind =0;
        uint64_t old_number_2 =0;
        uint64_t number_2 = 0;
        int aber = 0;
        for (int i = 9; command_buffer[i] != ' '; i++) {
            if (command_buffer[i] == '1') {
                rind =1;
                old_number = number;
                number = old_number *10 +rind;
            }
            if (command_buffer[i] == '2') {
                rind =2;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '3') {
                rind =3;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '4') {
                rind =4;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '5') {
                rind =5;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '6') {
                rind =6;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '7') {
                rind =7;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '8') {
                rind =8;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '9') {
                rind =9;
                old_number = number;
                number = old_number *10 + rind;
            }
            aber = i;
        }
        for (int i = aber + 1; command_buffer[i] != '\0'; i++) {
            if (command_buffer[i] == '1') {
                rind =1;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 +rind;
            }
            if (command_buffer[i] == '2') {
                rind =2;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '3') {
                rind =3;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '4') {
                rind =4;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '5') {
                rind =5;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '6') {
                rind =6;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '7') {
                rind =7;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '8') {
                rind =8;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '9') {
                rind =9;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
        }
        for (int i = 0; i <21; i++) {
            buffer[i] = '\0';
        }
        uint64_t result = number / number_2;
        number_to_string(result, buffer);
        draw_text(fb, buffer, 2);
        cursor_y += 7 * scale + 1 *scale;
        cursor_x = 0;
    }
    else if (command_buffer[0] == 'S' && command_buffer[1] == 'T' && command_buffer[2] == 'E' && command_buffer[3] == 'P') {
        char buffer[21];
        uint64_t old_number = 0;
        uint64_t number = 0;
        uint64_t rind =0;
        uint64_t old_number_2 =0;
        uint64_t number_2 = 0;
        int aber = 0;
        for (int i = 5; command_buffer[i] != ' '; i++) {
            if (command_buffer[i] == '1') {
                rind =1;
                old_number = number;
                number = old_number *10 +rind;
            }
            if (command_buffer[i] == '2') {
                rind =2;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '3') {
                rind =3;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '4') {
                rind =4;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '5') {
                rind =5;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '6') {
                rind =6;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '7') {
                rind =7;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '8') {
                rind =8;
                old_number = number;
                number = old_number *10 + rind;
            }
            if (command_buffer[i] == '9') {
                rind =9;
                old_number = number;
                number = old_number *10 + rind;
            }
            aber = i;
        }
        for (int i = aber + 1; command_buffer[i] != '\0'; i++) {
            if (command_buffer[i] == '1') {
                rind =1;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 +rind;
            }
            if (command_buffer[i] == '2') {
                rind =2;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '3') {
                rind =3;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '4') {
                rind =4;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '5') {
                rind =5;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '6') {
                rind =6;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '7') {
                rind =7;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '8') {
                rind =8;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
            if (command_buffer[i] == '9') {
                rind =9;
                old_number_2 = number_2;
                number_2 = old_number_2 *10 + rind;
            }
        }
        for (int i = 0; i <21; i++) {
            buffer[i] = '\0';
        }
        uint64_t result = step(number, number_2);
        number_to_string(result, buffer);
        draw_text(fb, buffer, 2);
        cursor_y += 7 * scale + 1 *scale;
        cursor_x = 0;
    }
    //зроби казино просто нада
    else if (command_buffer[0] != '\0') {
        draw_text(fb, "UNKNOWN COMMAND\n", 2);
        
    } 
    else {
        draw_text(fb, "NO COMMAND DETECTED\n", 2);
    }
    command_buffer_clear();
}


void keyboard_poll(limine_framebuffer* fb) {
    uint64_t scale = 2;
    if (!keyboard_ready()) return;

    uint8_t code = keyboard_read();

    // Shift натиснуто
    if (code == 0x2A || code == 0x36) {
        shift_pressed = true;
        return;
    }

    // Shift відпущено
    if (code == 0xAA || code == 0xB6) {
        shift_pressed = false;
        return;
    }

    // Ігноруємо коди відпускання інших клавіш
    if (code & 0x80) return;

    // Enter
    if (code == 0x1C) {
        cursor_x = 0;
        cursor_y += 7 * scale;
        execute_command(fb);
        input_len = 0;
        return;
    }

    // Backspace
    if (code == 0x0E) {
        if (cursor_x >= 5*scale && input_len > 0) {
            command_buffer[input_len - 1] = '\0';
            input_len -= 1;
            cursor_x -= 5 *scale + 1 *scale;
            draw_rect(fb, cursor_x, cursor_y, 5 *scale, 7*scale,
                      background_red, background_green, background_blue);
        }
        return;
    }


    char ch = scancode_to_char(code);

    if (ch != '\0') {
        command_buffer[input_len] = ch;
        input_len += 1;        
        draw_char(fb, cursor_x, cursor_y, ch, 2);
        cursor_x += 5 * scale + 1 * scale;

        if (cursor_x + 20 > fb->width) {
            cursor_x = 0;
            cursor_y += 7 * scale + 1 * scale;
        }
    }
    r_10 +=1;
    r_9 += 1;
    r_8 += 1;
    r_7 += 1;
    r_6 += 1;
    r_5 += 1;
    r_4 += 1;
    r_3 += 1;
    r_2 += 1;
    r_1 += 1;
}



void kernel_init(limine_framebuffer* fb) {
    if (fb == nullptr) {
        return;
    }
    if (fb->bpp != 32) {
        return;
    }
    uint64_t width = fb->width;
    uint64_t height = fb->height;
    auto address = fb->address;
    if (address == nullptr) {
        return;
    }
    volatile uint32_t* fb_ptr = static_cast<volatile uint32_t*>(address);
    int x = 0;
    int y = 0;
    if (x < width && y < height) {
        fb_ptr[y * (fb->pitch / 4) + x] = 0x00FF0000;
    }

    draw_rect(fb,0,0,fb->width,fb->height,background_red,background_green,background_blue);
    
    
    draw_text(fb, "MUSICOOS\n64",4);

    clear_screen(fb, 1);

}



// Build a pixel from 8-bit red, green and blue values following the channel
// layout of the framebuffer.
std::uint32_t fb_pixel(limine_framebuffer *fb, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return fb_channel(red, fb->red_mask_size, fb->red_mask_shift)
         | fb_channel(green, fb->green_mask_size, fb->green_mask_shift)
         | fb_channel(blue, fb->blue_mask_size, fb->blue_mask_shift);
}

// Print a nice pattern to a framebuffer as an example.
void fb_pattern(limine_framebuffer *fb) {
    volatile std::uint32_t *fb_ptr = static_cast<volatile std::uint32_t *>(fb->address);
    for (std::size_t y = 0; y < fb->height; y++) {
        for (std::size_t x = 0; x < fb->width; x++) {
            std::uint8_t nX = x * 255 / fb->width;
            std::uint8_t nY = y * 255 / fb->height;
            fb_ptr[y * (fb->pitch / 4) + x] = fb_pixel(fb, 0, nY, nX);
        }
    }
}

}

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
extern "C" void kmain() {
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
        
    }

    // Call global constructors.
    for (void (**ctor)() = __init_array_start; ctor < __init_array_end;) {
        (*ctor++)();
    }
    for (void (**ctor)() = __ctors_end; ctor > __ctors_start;) {
        (*--ctor)();
    }
    

    // Ensure we got a framebuffer.
    
    
    // Print the pattern to every framebuffer.
    for (std::uint64_t i = 0; i < framebuffer_request.response->framebuffer_count; i++) {
        limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[i];

        // Ensure the framebuffer has 32-bit RGB pixels, the only kind we handle.
        if (framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB || framebuffer->bpp != 32) {
            hcf();
        }

        kernel_init(framebuffer);
    }

    // We're done, just hang...
    if (framebuffer_request.response != nullptr &&
    framebuffer_request.response->framebuffer_count > 0) {
    limine_framebuffer* fb =
        framebuffer_request.response->framebuffers[0];

    while (true) {
        char buffer[12];
        second = bcd_to_decimal(get_second());
        minute = bcd_to_decimal(get_minute());
        hour = bcd_to_decimal(get_hour());
        day = bcd_to_decimal(get_day());
        month = bcd_to_decimal(get_month());
        year = bcd_to_decimal(get_year());
        draw_rect(fb, 500, 0, 800, 28, background_red,background_green,background_blue);
        number_to_string(second, buffer);
        draw_text_location(fb, buffer, 4, 500, 0);
        number_to_string(minute, buffer);
        draw_text_location(fb, buffer, 4, 644,0);
        number_to_string(hour, buffer);
        draw_text_location(fb, buffer, 4, 788, 0);
        number_to_string(day, buffer);
        draw_text_location(fb, buffer, 4,932, 0);
        number_to_string(month, buffer);
        draw_text_location(fb, buffer, 4, 1076,0);
        number_to_string(year, buffer);
        draw_text_location(fb, buffer, 4, 1220,0);
        keyboard_poll(fb);
    }
}

hcf();
}

