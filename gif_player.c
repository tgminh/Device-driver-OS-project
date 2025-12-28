#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// ==========================================
// CẤU HÌNH CHẾ ĐỘ (BẬT/TẮT DÒNG DƯỚI ĐÂY)
// ==========================================
//#define SIMULATION_MODE  // Comment dòng này lại khi compile cho BeagleBone

#ifdef SIMULATION_MODE
    #include <time.h>
    #if defined(_WIN32)
        #include <windows.h>
        #define usleep(x) Sleep((x)/1000)
        #define CLEAR_SCREEN "cls"
    #else
        #include <unistd.h>
        #define CLEAR_SCREEN "clear"
    #endif
#else
    // Chế độ Mạch thật (BeagleBone)
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
    #include <linux/types.h>
    #define DEVICE_PATH "/dev/ssd1306_oled"
    #define SSD1306_IOC_MAGIC 'k'
    #define SSD1306_IOC_UPDATE_FRAME _IOW(SSD1306_IOC_MAGIC, 1, unsigned char *)
#endif

// --- STB IMAGE ---
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#include "stb_image.h"

#define OLED_W 128
#define OLED_H 64
#define BUFFER_SIZE (OLED_W * OLED_H / 8)

// --- HÀM ĐỌC TOÀN BỘ FILE VÀO BỘ NHỚ (Mới thêm) ---
unsigned char *read_file_to_buffer(const char *filename, int *size) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char *buf = (unsigned char *)malloc(*size);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    fread(buf, 1, *size, f);
    fclose(f);
    return buf;
}
#ifdef SIMULATION_MODE
// --- HÀM MÔ PHỎNG HIỂN THỊ ---
void simulate_display(uint8_t *buffer) {
    system(CLEAR_SCREEN); 
    for(int k=0; k<OLED_W+2; k++) printf("-");
    printf("\n");

    for (int y = 0; y < OLED_H; y++) {
        printf("|"); 
        for (int x = 0; x < OLED_W; x++) {
            int page = y / 8;
            int bit_offset = y % 8;
            int index = x + (page * OLED_W);
            
            uint8_t byte = buffer[index];
            int bit_val = (byte >> bit_offset) & 1;

            if (bit_val) printf("#"); 
            else printf(" ");
        }
        printf("|\n"); 
    }
    for(int k=0; k<OLED_W+2; k++) printf("-");
    printf("\n");
}
#endif
// --- HÀM CONVERT LOGIC ---
void convert_frame_to_oled_format(unsigned char *pixels, int img_w, int img_h, int channels, uint8_t *buffer, int threshold) {
    memset(buffer, 0, BUFFER_SIZE);
    for (int y = 0; y < img_h; y++) {
        for (int x = 0; x < img_w; x++) {
            if (x >= OLED_W || y >= OLED_H) continue;
            int pixel_index = (y * img_w + x) * channels;
            unsigned char r = pixels[pixel_index];
            unsigned char g = pixels[pixel_index + 1];
            unsigned char b = pixels[pixel_index + 2];
            int brightness = (r + g + b) / 3;

            if (brightness > threshold) {
                int page = y / 8;
                int bit = y % 8;
                int buffer_index = x + (page * OLED_W);
                buffer[buffer_index] |= (1 << bit);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <filename.gif>\n", argv[0]);
        return 1;
    }

    const char *gif_filename = argv[1];
    int width, height, frames, channels;
    int *delays = NULL;
    int file_size = 0;

    // 1. Đọc file GIF từ đĩa vào RAM (Raw Data)
    printf("Reading file: %s ...\n", gif_filename);
    unsigned char *file_data = read_file_to_buffer(gif_filename, &file_size);
    if (!file_data) {
        printf("Error: Cannot open file or file empty.\n");
        return -1;
    }

    // 2. Parse GIF từ RAM
    printf("Parsing GIF...\n");
    unsigned char *image_data = stbi_load_gif_from_memory(
        file_data, file_size, &delays, &width, &height, &frames, &channels, 4
    );

    // Giải phóng buffer file thô vì stb_image đã copy dữ liệu ra rồi
    free(file_data); 

    if (!image_data) {
        printf("Error decoding GIF: %s\n", stbi_failure_reason());
        return -1;
    }

    // INIT DEVICE
    #ifdef SIMULATION_MODE
        printf("=== SIMULATION MODE (%dx%d, %d frames) ===\n", width, height, frames);
        printf("Press Enter to start...");
        getchar();
    #else
        int fd = open(DEVICE_PATH, O_RDWR);
        if (fd < 0) { perror("Cannot open OLED driver"); return -1; }
    #endif

    uint8_t *oled_buffer = (uint8_t *)malloc(BUFFER_SIZE);
    
    // LOOP
    while (1) {
        unsigned char *current_frame_ptr = image_data;
        int frame_stride = width * height * 4;

        for (int i = 0; i < frames; i++) {
            convert_frame_to_oled_format(current_frame_ptr, width, height, 4, oled_buffer, 128);
            printf("Sending frame %d...\n", i);

            #ifdef SIMULATION_MODE
                simulate_display(oled_buffer); 
            #else
                ioctl(fd, SSD1306_IOC_UPDATE_FRAME, oled_buffer); 
            #endif

            // Delay
            int delay_ms = delays[i];
            #ifdef SIMULATION_MODE
                if (delay_ms < 50) delay_ms = 50; 
            #endif
            usleep(delay_ms * 1000);

            current_frame_ptr += frame_stride;
        }
    }

    free(oled_buffer);
    stbi_image_free(image_data);
    if(delays) stbi_image_free(delays);
    #ifndef SIMULATION_MODE
        close(fd);
    #endif

    return 0;
}