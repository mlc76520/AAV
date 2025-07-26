/*
 * Dual SSD1309 OLED Audio Visualizer for Raspberry Pi
 * Optimized C++ implementation using bcm2835 library
 * 
 * Compile: g++ -o visualizer visualizer.cpp -lbcm2835 -lpthread -lasound -lfftw3f -lm -O3 -march=native -lfreetype
 */

#include <bcm2835.h>
#include <alsa/asoundlib.h>
#include <fftw3.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <thread>
#include <atomic>
#include <chrono>
#include <array>
#include <mutex>
#include <signal.h>
#include <ft2build.h>
#include <mpd/client.h>
#include <string>
#include <sstream>
#include <iomanip>
#include FT_FREETYPE_H

// Forward declarations
class Display;
class AudioProcessor;
struct ControlState;

// GPIO Configuration
namespace GPIO {
    constexpr uint8_t LEFT_CS = 8;
    constexpr uint8_t LEFT_DC = 25;
    constexpr uint8_t LEFT_RST = 24;
    constexpr uint8_t RIGHT_CS = 7;
    constexpr uint8_t RIGHT_DC = 23;
    constexpr uint8_t RIGHT_RST = 22;
    constexpr uint8_t ROT1_CLK = 17;
    constexpr uint8_t ROT1_DT = 5;
    constexpr uint8_t ROT1_SW = 27;
    constexpr uint8_t ROT2_CLK = 6;
    constexpr uint8_t ROT2_DT = 9;
    constexpr uint8_t ROT2_SW = 26;
    constexpr uint8_t POWER_LED = 16;
    constexpr uint8_t POWER_SW = 13;
}

// Improved MPD Client that respects sleep state
class MPDClient {
private:
    struct mpd_connection* conn;
    std::thread mpd_thread;
    std::atomic<bool> thread_running;
    std::atomic<bool> shutdown_requested;
    std::atomic<bool> is_sleeping;  // Track sleep state
    std::mutex data_mutex;
    std::mutex conn_mutex;
    
    // Current song info
    std::string track_number;
    std::string title;
    std::string artist;
    std::string year;
    std::string formatted_text;
    
    // Connection parameters
    std::string host;
    int port;
    static constexpr int RECONNECT_DELAY_SEC = 5;
    static constexpr int SLEEP_CHECK_INTERVAL_MS = 500;  // Check sleep state every 500ms
    
    void updateFormattedText() {
        std::stringstream ss;
        
        if (!track_number.empty()) {
            ss << std::setfill('0') << std::setw(2) << track_number << ". ";
        }
        
        if (!title.empty()) {
            ss << title;
        } else {
            ss << "Unknown Title";
        }
        
        if (!artist.empty()) {
            ss << " - " << artist;
        }
        
        if (!year.empty()) {
            ss << " (" << year << ")";
        }
        
        formatted_text = ss.str();
    }
    
    bool connectMPD() {
        std::lock_guard<std::mutex> lock(conn_mutex);
        
        if (conn) {
            mpd_connection_free(conn);
            conn = nullptr;
        }
        
        if (shutdown_requested || is_sleeping) return false;
        
        conn = mpd_connection_new(host.c_str(), port, 2000);
        
        if (!conn || mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
            if (conn) {
                printf("MPD connection error: %s\n", mpd_connection_get_error_message(conn));
                mpd_connection_free(conn);
                conn = nullptr;
            }
            return false;
        }
        
        printf("Connected to MPD at %s:%d\n", host.c_str(), port);
        return true;
    }
    
    void disconnectMPD() {
        std::lock_guard<std::mutex> lock(conn_mutex);
        if (conn) {
            mpd_connection_free(conn);
            conn = nullptr;
            printf("Disconnected from MPD\n");
        }
    }
    
    void updateCurrentSong() {
        std::lock_guard<std::mutex> lock(conn_mutex);
        if (!conn || shutdown_requested || is_sleeping) return;
        
        struct mpd_song* song = mpd_run_current_song(conn);
        
        if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
            printf("MPD error getting current song: %s\n", mpd_connection_get_error_message(conn));
            mpd_connection_free(conn);
            conn = nullptr;
            return;
        }
        
        std::lock_guard<std::mutex> data_lock(data_mutex);
        
        if (song) {
            // Get track number
            unsigned track = mpd_song_get_pos(song) + 1;
            track_number = std::to_string(track);
            
            // Get title
            const char* tag = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
            title = tag ? tag : "";
            
            // Get artist
            tag = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
            artist = tag ? tag : "";
            
            // Get year/date
            tag = mpd_song_get_tag(song, MPD_TAG_DATE, 0);
            if (tag && strlen(tag) >= 4) {
                year = std::string(tag).substr(0, 4);
            } else {
                year = "";
            }
            
            mpd_song_free(song);
        } else {
            track_number = "";
            title = "No song playing";
            artist = "";
            year = "";
        }
        
        updateFormattedText();
    }
    
    void mpdThreadFunc() {
        printf("MPD thread started\n");
        
        while (thread_running && !shutdown_requested) {
            // Check if we're sleeping
            if (is_sleeping) {
                // Disconnect while sleeping to save resources
                if (conn) {
                    printf("MPD entering sleep mode - disconnecting\n");
                    disconnectMPD();
                }
                
                // Wait while sleeping, checking periodically
                while (is_sleeping && !shutdown_requested) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_CHECK_INTERVAL_MS));
                }
                
                if (!shutdown_requested) {
                    printf("MPD waking up - reconnecting\n");
                    // Continue to normal operation
                }
            }
            
            if (shutdown_requested) break;
            
            // Normal operation when not sleeping
            if (!conn) {
                if (!connectMPD()) {
                    // Sleep with interruption check
                    for (int i = 0; i < RECONNECT_DELAY_SEC * 10 && !shutdown_requested && !is_sleeping; i++) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    continue;
                }
                updateCurrentSong();
            }
            
            // Use idle with timeout
            {
                std::lock_guard<std::mutex> lock(conn_mutex);
                if (!conn || shutdown_requested || is_sleeping) continue;
                
                if (!mpd_send_idle_mask(conn, MPD_IDLE_PLAYER)) {
                    printf("MPD failed to send idle\n");
                    mpd_connection_free(conn);
                    conn = nullptr;
                    continue;
                }
            }
            
            // Wait for idle result with timeout
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            
            fd_set fds;
            FD_ZERO(&fds);
            
            int fd;
            {
                std::lock_guard<std::mutex> lock(conn_mutex);
                if (!conn) continue;
                fd = mpd_connection_get_fd(conn);
                if (fd < 0) {
                    mpd_connection_free(conn);
                    conn = nullptr;
                    continue;
                }
            }
            
            FD_SET(fd, &fds);
            
            int ret = select(fd + 1, &fds, NULL, NULL, &tv);
            
            if (shutdown_requested || is_sleeping) {
                // Cancel idle if we need to sleep or shutdown
                std::lock_guard<std::mutex> lock(conn_mutex);
                if (conn) {
                    mpd_send_noidle(conn);
                    mpd_response_finish(conn);
                }
                continue;
            }
            
            if (ret > 0) {
                std::lock_guard<std::mutex> lock(conn_mutex);
                if (!conn) continue;
                
                enum mpd_idle idle_result = mpd_recv_idle(conn, false);
                
                if (idle_result == 0 && mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
                    printf("MPD idle error: %s\n", mpd_connection_get_error_message(conn));
                    mpd_connection_free(conn);
                    conn = nullptr;
                    continue;
                }
                
                if (idle_result & MPD_IDLE_PLAYER) {
                    lock.~lock_guard();
                    updateCurrentSong();
                }
            } else if (ret == 0) {
                // Timeout - send noidle to keep connection alive
                std::lock_guard<std::mutex> lock(conn_mutex);
                if (conn && !is_sleeping) {
                    mpd_send_noidle(conn);
                    mpd_response_finish(conn);
                }
            }
        }
        
        disconnectMPD();
        printf("MPD thread stopped\n");
    }
    
public:
    MPDClient(const std::string& mpd_host = "localhost", int mpd_port = 6600) 
        : conn(nullptr), thread_running(false), shutdown_requested(false),
          is_sleeping(false), host(mpd_host), port(mpd_port) {
        
        // Initialize with default text
        std::lock_guard<std::mutex> lock(data_mutex);
        title = "Waiting for MPD...";
        artist = "";
        year = "";
        track_number = "";
        updateFormattedText();
    }
    
    ~MPDClient() {
        stop();
    }
    
    bool start() {
        if (thread_running) return true;
        
        printf("Starting MPD client...\n");
        thread_running = true;
        shutdown_requested = false;
        mpd_thread = std::thread(&MPDClient::mpdThreadFunc, this);
        return true;
    }
    
    void stop() {
        printf("Stopping MPD client...\n");
        
        if (!thread_running) return;
        
        shutdown_requested = true;
        thread_running = false;
        
        // Wake from sleep if necessary
        setSleepState(false);
        
        // Try to break out of idle
        {
            std::lock_guard<std::mutex> lock(conn_mutex);
            if (conn) {
                struct mpd_connection* cancel_conn = mpd_connection_new(host.c_str(), port, 500);
                if (cancel_conn) {
                    mpd_run_noidle(cancel_conn);
                    mpd_connection_free(cancel_conn);
                }
            }
        }
        
        if (mpd_thread.joinable()) {
            printf("Waiting for MPD thread to finish...\n");
            mpd_thread.join();
        }
        
        disconnectMPD();
        printf("MPD client stopped\n");
    }
    
    // Set sleep state
    void setSleepState(bool sleeping) {
        bool was_sleeping = is_sleeping.exchange(sleeping);
        if (was_sleeping != sleeping) {
            printf("MPD sleep state changed to: %s\n", sleeping ? "sleeping" : "awake");
        }
    }
    
    // Simple getters
    std::string getFormattedText() {
        std::lock_guard<std::mutex> lock(data_mutex);
        return formatted_text;
    }
    
    std::string getTitle() {
        std::lock_guard<std::mutex> lock(data_mutex);
        return title;
    }
    
    std::string getArtist() {
        std::lock_guard<std::mutex> lock(data_mutex);
        return artist;
    }
    
    std::string getYear() {
        std::lock_guard<std::mutex> lock(data_mutex);
        return year;
    }
    
    std::string getTrackNumber() {
        std::lock_guard<std::mutex> lock(data_mutex);
        return track_number;
    }
    
    bool isConnected() {
        std::lock_guard<std::mutex> lock(conn_mutex);
        return conn != nullptr;
    }
};

// Font manager class
class FontManager {
private:
    FT_Library library;
    FT_Face face_regular;
    FT_Face face_small;
    FT_Face face_large;
    bool initialized;
    
public:
    FontManager() : library(nullptr), face_regular(nullptr), face_small(nullptr), 
                   face_large(nullptr), initialized(false) {}
    
    ~FontManager() {
        if (initialized) {
            if (face_large) FT_Done_Face(face_large);
            if (face_small) FT_Done_Face(face_small);
            if (face_regular) FT_Done_Face(face_regular);
            if (library) FT_Done_FreeType(library);
        }
    }
    
    bool init(const char* font_path) {
        FT_Error error = FT_Init_FreeType(&library);
        if (error) {
            printf("FreeType init error: %d\n", error);
            return false;
        }
        
        // Load regular size (10px)
        error = FT_New_Face(library, font_path, 0, &face_regular);
        if (error) {
            printf("Font loading error: %d (check path: %s)\n", error, font_path);
            FT_Done_FreeType(library);
            library = nullptr;
            return false;
        }
        FT_Set_Pixel_Sizes(face_regular, 0, 10);
        
        // Load small size (8px)
        error = FT_New_Face(library, font_path, 0, &face_small);
        if (!error) {
            FT_Set_Pixel_Sizes(face_small, 0, 8);
        }
        
        // Load large size (14px)
        error = FT_New_Face(library, font_path, 0, &face_large);
        if (!error) {
            FT_Set_Pixel_Sizes(face_large, 0, 14);
        }
        
        initialized = true;
        printf("Font loaded: %s (8px, 10px, 14px)\n", font_path);
        return true;
    }
    
    enum FontSize { SMALL, REGULAR, LARGE };
    
    bool renderText(const char* text, uint8_t* buffer, int buf_width, int buf_height, 
                   int x, int y, FontSize size = REGULAR, bool invert = false) {
        if (!initialized || !text) return false;
        
        FT_Face face = face_regular;
        switch (size) {
            case SMALL: face = face_small ? face_small : face_regular; break;
            case LARGE: face = face_large ? face_large : face_regular; break;
            default: break;
        }
        
        int cursor_x = x;
        int baseline_y = y;
        
        while (*text) {
            FT_Error error = FT_Load_Char(face, *text, FT_LOAD_RENDER);
            if (error) {
                text++;
                continue;
            }
            
            FT_GlyphSlot slot = face->glyph;
            FT_Bitmap* bitmap = &slot->bitmap;
            
            int start_x = cursor_x + slot->bitmap_left;
            int start_y = baseline_y - slot->bitmap_top;
            
            for (unsigned int row = 0; row < bitmap->rows; row++) {
                for (unsigned int col = 0; col < bitmap->width; col++) {
                    int px = start_x + col;
                    int py = start_y + row;
                    
                    if (px >= 0 && px < buf_width && py >= 0 && py < buf_height) {
                        uint8_t gray = bitmap->buffer[row * bitmap->pitch + col];
                        bool pixel_on = gray > 128;
                        if (invert) pixel_on = !pixel_on;
                        
                        if (pixel_on) {
                            int page = py / 8;
                            int bit = py % 8;
                            buffer[page * buf_width + px] |= (1 << bit);
                        }
                    }
                }
            }
            
            cursor_x += slot->advance.x >> 6;
            text++;
        }
        
        return true;
    }
    
    int getTextWidth(const char* text, FontSize size = REGULAR) {
        if (!initialized || !text) return 0;
        
        FT_Face face = face_regular;
        switch (size) {
            case SMALL: face = face_small ? face_small : face_regular; break;
            case LARGE: face = face_large ? face_large : face_regular; break;
            default: break;
        }
        
        int width = 0;
        while (*text) {
            FT_Error error = FT_Load_Char(face, *text, FT_LOAD_DEFAULT);
            if (!error) {
                width += face->glyph->advance.x >> 6;
            }
            text++;
        }
        return width;
    }
    
    int getFontHeight(FontSize size = REGULAR) {
        if (!initialized) return 12;
        
        FT_Face face = face_regular;
        switch (size) {
            case SMALL: face = face_small ? face_small : face_regular; break;
            case LARGE: face = face_large ? face_large : face_regular; break;
            default: break;
        }
        
        return face->size->metrics.height >> 6;
    }
    
    bool isInitialized() const { return initialized; }
};

// Display class
class Display {
private:
    uint8_t _cs, _dc, _rst;
    FontManager* font_manager;

public:
    uint8_t buffer[1024];
    
    Display(uint8_t cs, uint8_t dc, uint8_t rst) : _cs(cs), _dc(dc), _rst(rst), font_manager(nullptr) {
        memset(buffer, 0x00, sizeof(buffer));
    }
    
    bool begin() {
        bcm2835_gpio_fsel(_cs, BCM2835_GPIO_FSEL_OUTP);
        bcm2835_gpio_fsel(_dc, BCM2835_GPIO_FSEL_OUTP);
        bcm2835_gpio_fsel(_rst, BCM2835_GPIO_FSEL_OUTP);
        
        bcm2835_gpio_write(_rst, LOW);
        bcm2835_delay(10);
        bcm2835_gpio_write(_rst, HIGH);
        
        sendCommand(0xAE);
        sendCommand(0x20); sendCommand(0x00);
        sendCommand(0xB0);
        sendCommand(0xC8);
        sendCommand(0x00);
        sendCommand(0x10);
        sendCommand(0x40);
        sendCommand(0x81); sendCommand(0x7F);
        sendCommand(0xA1);
        sendCommand(0xA6);
        sendCommand(0xA8); sendCommand(0x3F);
        sendCommand(0xA4);
        sendCommand(0xD3); sendCommand(0x00);
        sendCommand(0xD5); sendCommand(0x80);
        sendCommand(0xD9); sendCommand(0xF1);
        sendCommand(0xDA); sendCommand(0x12);
        sendCommand(0xDB); sendCommand(0x40);
        sendCommand(0x8D); sendCommand(0x14);
        sendCommand(0xAF);
        return true;
    }
    
    void sendCommand(uint8_t cmd) {
        bcm2835_gpio_write(_dc, LOW);
        bcm2835_gpio_write(_cs, LOW);
        bcm2835_spi_transfer(cmd);
        bcm2835_gpio_write(_cs, HIGH);
    }
    
    void sendData(uint8_t data) {
        bcm2835_gpio_write(_dc, HIGH);
        bcm2835_gpio_write(_cs, LOW);
        bcm2835_spi_transfer(data);
        bcm2835_gpio_write(_cs, HIGH);
    }
    
    void display() {
        for (uint8_t page = 0; page < 8; page++) {
            sendCommand(0xB0 + page);
            sendCommand(0x00);
            sendCommand(0x10);
            for (uint8_t col = 0; col < 128; col++) {
                sendData(buffer[page * 128 + col]);
            }
        }
    }
    
    void clear() {
        memset(buffer, 0x00, sizeof(buffer));
    }
    
    void sleep() {
        sendCommand(0xAE); // Display off
    }
    
    void wake() {
        sendCommand(0xAF); // Display on
    }
    
    void drawPixel(uint8_t x, uint8_t y) {
        if (x >= 128 || y >= 64) return;
        buffer[(y / 8) * 128 + x] |= (1 << (y % 8));
    }
    
    void clearPixel(uint8_t x, uint8_t y) {
        if (x >= 128 || y >= 64) return;
        buffer[(y / 8) * 128 + x] &= ~(1 << (y % 8));
    }
    
    void drawLine(int x0, int y0, int x1, int y1) {
        int dx = abs(x1 - x0), dy = abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;
        
        while (true) {
            drawPixel(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
    }
    
    void drawRect(int x, int y, int w, int h, bool filled = false) {
        if (filled) {
            for (int i = y; i < y + h && i < 64; i++) {
                for (int j = x; j < x + w && j < 128; j++) {
                    drawPixel(j, i);
                }
            }
        } else {
            drawLine(x, y, x + w - 1, y);
            drawLine(x + w - 1, y, x + w - 1, y + h - 1);
            drawLine(x + w - 1, y + h - 1, x, y + h - 1);
            drawLine(x, y + h - 1, x, y);
        }
    }
    
    void drawCircle(int x0, int y0, int radius, bool filled = false) {
        if (filled) {
            // For filled circle, we'll draw horizontal lines for each row
            for (int y = -radius; y <= radius; y++) {
                int x = (int)sqrt(radius * radius - y * y);
                drawLine(x0 - x, y0 + y, x0 + x, y0 + y);
            }
        } else {
            // Bresenham's circle algorithm for outline
            int x = radius;
            int y = 0;
            int err = 0;
            
            while (x >= y) {
                // Draw 8 octants
                drawPixel(x0 + x, y0 + y);
                drawPixel(x0 + y, y0 + x);
                drawPixel(x0 - y, y0 + x);
                drawPixel(x0 - x, y0 + y);
                drawPixel(x0 - x, y0 - y);
                drawPixel(x0 - y, y0 - x);
                drawPixel(x0 + y, y0 - x);
                drawPixel(x0 + x, y0 - y);
                
                if (err <= 0) {
                    y += 1;
                    err += 2 * y + 1;
                }
                if (err > 0) {
                    x -= 1;
                    err -= 2 * x + 1;
                }
            }
        }
    }

    void setFont(FontManager* fm) {
        font_manager = fm;
    }
    
    // Text drawing using TTF fonts only
    void drawText(uint8_t x, uint8_t y, const char* text, FontManager::FontSize size = FontManager::REGULAR) {
        if (!font_manager || !font_manager->isInitialized()) {
            printf("Warning: Font manager not initialized\n");
            return;
        }
        font_manager->renderText(text, buffer, 128, 64, x, y, size);
    }
    
    // Centered text drawing
    void drawTextCentered(uint8_t y, const char* text, FontManager::FontSize size = FontManager::REGULAR) {
        if (!font_manager || !font_manager->isInitialized()) return;
        
        int text_width = font_manager->getTextWidth(text, size);
        int x = (128 - text_width) / 2;
        if (x < 0) x = 0;
        
        drawText(x, y, text, size);
    }
    
    // Text with background
    void drawTextWithBackground(uint8_t x, uint8_t y, const char* text, 
                               FontManager::FontSize size = FontManager::REGULAR, bool invert = false) {
        if (!font_manager || !font_manager->isInitialized()) return;
        
        int text_width = font_manager->getTextWidth(text, size);
        int text_height = font_manager->getFontHeight(size);
        
        // Draw background
        drawRect(x - 1, y - text_height - 1, text_width + 2, text_height + 2, !invert);
        
        // Draw text
        if (invert) {
            // For inverted text, we need to clear the text area first
            for (int i = 0; i < text_height; i++) {
                for (int j = 0; j < text_width; j++) {
                    clearPixel(x + j, y - text_height + i);
                }
            }
        }
        
        font_manager->renderText(text, buffer, 128, 64, x, y, size, invert);
    }
};

// Control State
struct ControlState {
    std::atomic<bool> running{true};
    std::atomic<int> current_viz{0};
    std::atomic<bool> is_sleeping{false};
};

// Audio Processor with sleep detection
class AudioProcessor {
private:
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int CHANNELS = 2;
    static constexpr int FRAMES_PER_BUFFER = 2048;
    static constexpr int FFT_SIZE_BASS = 8192;
    static constexpr int FFT_SIZE_MID = 2048;
    static constexpr int FFT_SIZE_TREBLE = 512;
    static constexpr float SILENCE_THRESHOLD = 0.001f;
    static constexpr int SLEEP_TIMEOUT_SEC = 10;
    
    struct FreqBand {
        int low, high;
        float correction;
    };
    
    static constexpr FreqBand FREQ_BANDS[7] = {
        {63, 120, 0.5f}, {120, 350, 1.0f}, {350, 900, 2.0f},
        {900, 2000, 3.5f}, {2000, 5000, 5.0f},
        {5000, 10000, 7.0f}, {10000, 16000, 10.0f}
    };
    
    snd_pcm_t* pcm_handle;
    std::thread audio_thread;
    std::atomic<bool> thread_running;
    std::atomic<bool> is_sleeping{false};
    
    float* circular_buffer_left;
    float* circular_buffer_right;
    std::atomic<size_t> write_pos{0};
    std::mutex buffer_mutex;
    
    fftwf_plan plan_bass, plan_mid, plan_treble;
    float *fft_in_bass, *fft_in_mid, *fft_in_treble;
    fftwf_complex *fft_out_bass, *fft_out_mid, *fft_out_treble;
    float *window_bass, *window_mid, *window_treble;
    
    std::array<float, 7> prev_left_spectrum{};
    std::array<float, 7> prev_right_spectrum{};
    
    float noise_reduction = 77.0f;
    float sensitivity = 100.0f;
    float integral_factor, gravity_factor, scale_factor;
    
    // Sleep detection
    std::chrono::steady_clock::time_point last_audio_time;
    std::atomic<float> max_amplitude{0.0f};
    
    void createHannWindow(float* window, int size) {
        for (int i = 0; i < size; i++) {
            window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (size - 1)));
        }
    }
    
    void audioThreadFunc() {
        int16_t* audio_buffer = new int16_t[FRAMES_PER_BUFFER * CHANNELS];
        
        while (thread_running) {
            // Use smaller buffer during sleep for faster wake detection
            int frames_to_read = is_sleeping ? 256 : FRAMES_PER_BUFFER;
            
            int frames = snd_pcm_readi(pcm_handle, audio_buffer, frames_to_read);
            if (frames < 0) frames = snd_pcm_recover(pcm_handle, frames, 0);
            if (frames < 0) continue;
            
            float frame_max = 0.0f;
            
            // During sleep, only calculate max amplitude (skip buffer updates)
            if (is_sleeping) {
                for (int i = 0; i < frames * CHANNELS; i++) {
                    frame_max = std::max(frame_max, std::abs(audio_buffer[i] / 32768.0f));
                }
                max_amplitude = frame_max;
                if (frame_max > SILENCE_THRESHOLD) {
                    last_audio_time = std::chrono::steady_clock::now();
                }
                continue;  // Skip buffer writes during sleep
            }
            
            // Normal processing when awake
            std::lock_guard<std::mutex> lock(buffer_mutex);
            size_t pos = write_pos;
            
            for (int i = 0; i < frames; i++) {
                circular_buffer_left[pos] = audio_buffer[i * CHANNELS] / 32768.0f;
                circular_buffer_right[pos] = (CHANNELS > 1) ? 
                    audio_buffer[i * CHANNELS + 1] / 32768.0f : circular_buffer_left[pos];
                
                frame_max = std::max(frame_max, std::abs(circular_buffer_left[pos]));
                frame_max = std::max(frame_max, std::abs(circular_buffer_right[pos]));
                
                pos = (pos + 1) % (FFT_SIZE_BASS * 2);
            }
            write_pos = pos;
            
            // Update max amplitude for sleep detection
            max_amplitude = frame_max;
            if (frame_max > SILENCE_THRESHOLD) {
                last_audio_time = std::chrono::steady_clock::now();
            }
        }
        
        delete[] audio_buffer;
    }

    void updateParameters() {
        float nr_normalized = noise_reduction / 100.0f;
        integral_factor = nr_normalized * 0.95f;
        gravity_factor = 1.0f - (nr_normalized * 0.8f);
        gravity_factor = std::max(gravity_factor, 0.2f);
        scale_factor = (sensitivity / 100.0f) * 2.2f;
    }
    
public:
    AudioProcessor() : pcm_handle(nullptr), thread_running(false) {
        int buffer_size = FFT_SIZE_BASS * 2;
        circular_buffer_left = new float[buffer_size]();
        circular_buffer_right = new float[buffer_size]();
        
        // Allocate FFT resources
        fft_in_bass = (float*)fftwf_malloc(sizeof(float) * FFT_SIZE_BASS);
        fft_out_bass = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * (FFT_SIZE_BASS/2 + 1));
        plan_bass = fftwf_plan_dft_r2c_1d(FFT_SIZE_BASS, fft_in_bass, fft_out_bass, FFTW_ESTIMATE);
        
        fft_in_mid = (float*)fftwf_malloc(sizeof(float) * FFT_SIZE_MID);
        fft_out_mid = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * (FFT_SIZE_MID/2 + 1));
        plan_mid = fftwf_plan_dft_r2c_1d(FFT_SIZE_MID, fft_in_mid, fft_out_mid, FFTW_ESTIMATE);
        
        fft_in_treble = (float*)fftwf_malloc(sizeof(float) * FFT_SIZE_TREBLE);
        fft_out_treble = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * (FFT_SIZE_TREBLE/2 + 1));
        plan_treble = fftwf_plan_dft_r2c_1d(FFT_SIZE_TREBLE, fft_in_treble, fft_out_treble, FFTW_ESTIMATE);
        
        // Create windows
        window_bass = new float[FFT_SIZE_BASS];
        window_mid = new float[FFT_SIZE_MID];
        window_treble = new float[FFT_SIZE_TREBLE];
        createHannWindow(window_bass, FFT_SIZE_BASS);
        createHannWindow(window_mid, FFT_SIZE_MID);
        createHannWindow(window_treble, FFT_SIZE_TREBLE);
        
        updateParameters();
        last_audio_time = std::chrono::steady_clock::now();
    }
    
    ~AudioProcessor() {
        stop();
        delete[] circular_buffer_left;
        delete[] circular_buffer_right;
        delete[] window_bass;
        delete[] window_mid;
        delete[] window_treble;
        
        fftwf_destroy_plan(plan_bass);
        fftwf_destroy_plan(plan_mid);
        fftwf_destroy_plan(plan_treble);
        fftwf_free(fft_in_bass);
        fftwf_free(fft_out_bass);
        fftwf_free(fft_in_mid);
        fftwf_free(fft_out_mid);
        fftwf_free(fft_in_treble);
        fftwf_free(fft_out_treble);
    }
    
    bool start() {
        int err = snd_pcm_open(&pcm_handle, "cava", SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0) {
            err = snd_pcm_open(&pcm_handle, "hw:Loopback,1", SND_PCM_STREAM_CAPTURE, 0);
        }
        if (err < 0) return false;
        
        snd_pcm_hw_params_t* hw_params;
        snd_pcm_hw_params_alloca(&hw_params);
        snd_pcm_hw_params_any(pcm_handle, hw_params);
        snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
        snd_pcm_hw_params_set_channels(pcm_handle, hw_params, CHANNELS);
        
        unsigned int rate = SAMPLE_RATE;
        snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &rate, 0);
        
        if (snd_pcm_hw_params(pcm_handle, hw_params) < 0) {
            snd_pcm_close(pcm_handle);
            return false;
        }
        
        thread_running = true;
        audio_thread = std::thread(&AudioProcessor::audioThreadFunc, this);
        return true;
    }

    void setSleepState(bool sleeping) {
    is_sleeping = sleeping;
    }
    
    void stop() {
        if (thread_running) {
            thread_running = false;
            if (audio_thread.joinable()) audio_thread.join();
        }
        if (pcm_handle) {
            snd_pcm_close(pcm_handle);
            pcm_handle = nullptr;
        }
    }
    
    bool checkForAudio() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_audio_time).count();
        return elapsed < SLEEP_TIMEOUT_SEC;
    }
    
    void getSpectrumData(std::array<int, 7>& left_out, std::array<int, 7>& right_out) {
        std::array<float, 7> left_bands{}, right_bands{};
        
        // Get buffer data
        float* temp_left = new float[FFT_SIZE_BASS];
        float* temp_right = new float[FFT_SIZE_BASS];
        
        {
            std::lock_guard<std::mutex> lock(buffer_mutex);
            size_t read_pos = (write_pos + FFT_SIZE_BASS * 2 - FFT_SIZE_BASS) % (FFT_SIZE_BASS * 2);
            for (int i = 0; i < FFT_SIZE_BASS; i++) {
                temp_left[i] = circular_buffer_left[read_pos];
                temp_right[i] = circular_buffer_right[read_pos];
                read_pos = (read_pos + 1) % (FFT_SIZE_BASS * 2);
            }
        }
        
        // Process FFTs (simplified - just using bass FFT for all bands)
        for (int i = 0; i < FFT_SIZE_BASS; i++) {
            fft_in_bass[i] = temp_left[i] * window_bass[i];
        }
        fftwf_execute(plan_bass);
        
        // Process left channel
        for (int i = 0; i < 7; i++) {
            int low_idx = FREQ_BANDS[i].low * FFT_SIZE_BASS / SAMPLE_RATE;
            int high_idx = FREQ_BANDS[i].high * FFT_SIZE_BASS / SAMPLE_RATE;
            
            float sum = 0.0f;
            for (int j = low_idx; j < high_idx && j < FFT_SIZE_BASS/2; j++) {
                float mag = sqrtf(fft_out_bass[j][0] * fft_out_bass[j][0] + 
                                 fft_out_bass[j][1] * fft_out_bass[j][1]);
                sum += mag * mag;
            }
            
            left_bands[i] = sqrtf(sum / (high_idx - low_idx)) * scale_factor * FREQ_BANDS[i].correction;
        }
        
        // Process right channel
        for (int i = 0; i < FFT_SIZE_BASS; i++) {
            fft_in_bass[i] = temp_right[i] * window_bass[i];
        }
        fftwf_execute(plan_bass);
        
        for (int i = 0; i < 7; i++) {
            int low_idx = FREQ_BANDS[i].low * FFT_SIZE_BASS / SAMPLE_RATE;
            int high_idx = FREQ_BANDS[i].high * FFT_SIZE_BASS / SAMPLE_RATE;
            
            float sum = 0.0f;
            for (int j = low_idx; j < high_idx && j < FFT_SIZE_BASS/2; j++) {
                float mag = sqrtf(fft_out_bass[j][0] * fft_out_bass[j][0] + 
                                 fft_out_bass[j][1] * fft_out_bass[j][1]);
                sum += mag * mag;
            }
            
            right_bands[i] = sqrtf(sum / (high_idx - low_idx)) * scale_factor * FREQ_BANDS[i].correction;
        }
        
        // Apply smoothing
        for (int i = 0; i < 7; i++) {
            float smoothed_left = integral_factor * prev_left_spectrum[i] + 
                                 (1.0f - integral_factor) * left_bands[i];
            float smoothed_right = integral_factor * prev_right_spectrum[i] + 
                                  (1.0f - integral_factor) * right_bands[i];
            
            if (smoothed_left < prev_left_spectrum[i]) {
                float fall = (prev_left_spectrum[i] - smoothed_left) * gravity_factor;
                prev_left_spectrum[i] -= fall;
                prev_left_spectrum[i] = std::max(prev_left_spectrum[i], smoothed_left);
            } else {
                prev_left_spectrum[i] = smoothed_left;
            }
            
            if (smoothed_right < prev_right_spectrum[i]) {
                float fall = (prev_right_spectrum[i] - smoothed_right) * gravity_factor;
                prev_right_spectrum[i] -= fall;
                prev_right_spectrum[i] = std::max(prev_right_spectrum[i], smoothed_right);
            } else {
                prev_right_spectrum[i] = smoothed_right;
            }
            
            left_out[i] = std::min(255, std::max(0, (int)prev_left_spectrum[i]));
            right_out[i] = std::min(255, std::max(0, (int)prev_right_spectrum[i]));
        }
        
        delete[] temp_left;
        delete[] temp_right;
    }
    
    void getVUMeterData(int& left_out, int& right_out) {
        std::array<int, 7> left_spectrum, right_spectrum;
        getSpectrumData(left_spectrum, right_spectrum);
        
        int left_sum = 0, right_sum = 0;
        for (int i = 0; i < 7; i++) {
            left_sum += left_spectrum[i];
            right_sum += right_spectrum[i];
        }
        
        left_out = left_sum / 7;
        right_out = right_sum / 7;
    }
    
    void getWaveformData(float* out, int samples, bool left_channel) {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        size_t read_pos = (write_pos + FFT_SIZE_BASS * 2 - samples) % (FFT_SIZE_BASS * 2);
        
        for (int i = 0; i < samples; i++) {
            out[i] = left_channel ? circular_buffer_left[read_pos] : circular_buffer_right[read_pos];
            read_pos = (read_pos + 1) % (FFT_SIZE_BASS * 2);
        }
    }
    
    void getStereoAnalysis(float& phase, float& correlation) {
        const int ANALYSIS_SAMPLES = 512;
        float left[ANALYSIS_SAMPLES], right[ANALYSIS_SAMPLES];
        
        {
            std::lock_guard<std::mutex> lock(buffer_mutex);
            size_t read_pos = (write_pos + FFT_SIZE_BASS * 2 - ANALYSIS_SAMPLES) % (FFT_SIZE_BASS * 2);
            
            for (int i = 0; i < ANALYSIS_SAMPLES; i++) {
                left[i] = circular_buffer_left[read_pos];
                right[i] = circular_buffer_right[read_pos];
                read_pos = (read_pos + 1) % (FFT_SIZE_BASS * 2);
            }
        }
        
        // Calculate phase difference
        float sum_phase = 0.0f;
        for (int i = 0; i < ANALYSIS_SAMPLES; i++) {
            if (std::abs(left[i]) > 0.01f && std::abs(right[i]) > 0.01f) {
                sum_phase += atan2f(right[i], left[i]);
            }
        }
        phase = sum_phase / ANALYSIS_SAMPLES;
        
        // Calculate correlation
        float sum_l = 0, sum_r = 0, sum_lr = 0, sum_l2 = 0, sum_r2 = 0;
        for (int i = 0; i < ANALYSIS_SAMPLES; i++) {
            sum_l += left[i];
            sum_r += right[i];
            sum_lr += left[i] * right[i];
            sum_l2 += left[i] * left[i];
            sum_r2 += right[i] * right[i];
        }
        
        float n = ANALYSIS_SAMPLES;
        float num = n * sum_lr - sum_l * sum_r;
        float den = sqrtf((n * sum_l2 - sum_l * sum_l) * (n * sum_r2 - sum_r * sum_r));
        
        correlation = (den > 0) ? num / den : 0.0f;
        correlation = std::max(-1.0f, std::min(1.0f, correlation));
    }
    
    void setSensitivity(int value) {
        sensitivity = std::max(10.0f, std::min(300.0f, (float)value));
        updateParameters();
    }

    void setNoiseReduction(int value) {
        noise_reduction = std::max(0.0f, std::min(100.0f, (float)value));
        updateParameters();
    }
    
    int getSensitivity() const { return (int)sensitivity; }
    int getNoiseReduction() const { return (int)noise_reduction; }
};

// Improved TextScroller with smooth pixel-based scrolling
class TextScroller {
private:
    std::string current_text;
    float scroll_position;  // Now uses float for sub-pixel precision
    std::chrono::steady_clock::time_point last_scroll_time;
    int pause_counter;
    int text_width_pixels;
    bool needs_scrolling;
    
    static constexpr float SCROLL_SPEED_PIXELS_PER_SECOND = 30.0f;  // Adjustable speed
    static constexpr int SCROLL_PAUSE_MS = 2000;  // Pause at start/end
    static constexpr int SCROLL_GAP_PIXELS = 1;  // Gap between text repetitions
    
    enum ScrollState {
        PAUSED_AT_START,
        SCROLLING,
        PAUSED_AT_END
    } scroll_state;
    
public:
    TextScroller() : scroll_position(0.0f), pause_counter(0), 
                     text_width_pixels(0), needs_scrolling(false),
                     scroll_state(PAUSED_AT_START) {
        last_scroll_time = std::chrono::steady_clock::now();
    }
    
    void setText(const std::string& text) {
        if (text != current_text) {
            current_text = text;
            scroll_position = 0.0f;
            scroll_state = PAUSED_AT_START;
            pause_counter = SCROLL_PAUSE_MS;
            text_width_pixels = 0;
            needs_scrolling = false;
        }
    }
    
    std::string getScrollingText(int max_width, FontManager* font_manager, 
                                FontManager::FontSize font_size = FontManager::SMALL) {
        if (!font_manager || !font_manager->isInitialized() || current_text.empty()) {
            return current_text;
        }
        
        // Calculate text width if not done yet
        if (text_width_pixels == 0) {
            text_width_pixels = font_manager->getTextWidth(current_text.c_str(), font_size);
            needs_scrolling = text_width_pixels > max_width;
            
            if (!needs_scrolling) {
                return current_text;  // Text fits, no scrolling needed
            }
        }
        
        if (!needs_scrolling) {
            return current_text;
        }
        
        // Handle scrolling timing
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_scroll_time).count();
        last_scroll_time = now;
        
        // Update based on state
        switch (scroll_state) {
            case PAUSED_AT_START:
                pause_counter -= elapsed_ms;
                if (pause_counter <= 0) {
                    scroll_state = SCROLLING;
                    pause_counter = 0;
                }
                break;
                
            case SCROLLING:
                // Smooth pixel-based scrolling
                scroll_position += (SCROLL_SPEED_PIXELS_PER_SECOND * elapsed_ms) / 1000.0f;
                
                // Check if we've scrolled the full text + gap
                if (scroll_position >= text_width_pixels + SCROLL_GAP_PIXELS) {
                    scroll_position = 0.0f;
                    scroll_state = PAUSED_AT_START;
                    pause_counter = SCROLL_PAUSE_MS;
                }
                break;
                
            case PAUSED_AT_END:
                // Not used in continuous loop, but could be added for ping-pong scrolling
                break;
        }
        
        // Create the visible portion of text
        // We need to handle partial character rendering at the edges
        return createVisibleText(max_width, font_manager, font_size);
    }
    
    void reset() {
        scroll_position = 0.0f;
        scroll_state = PAUSED_AT_START;
        pause_counter = SCROLL_PAUSE_MS;
        last_scroll_time = std::chrono::steady_clock::now();
    }
    
private:
    std::string createVisibleText(int max_width, FontManager* font_manager, 
                                 FontManager::FontSize font_size) {
        // For smooth scrolling, we need to determine which characters are visible
        // based on the current scroll position
        
        int current_pixel = 0;
        int start_char = -1;
        int end_char = -1;
        
        // Find the first visible character
        for (size_t i = 0; i < current_text.length(); i++) {
            int char_width = font_manager->getTextWidth(current_text.substr(i, 1).c_str(), font_size);
            
            if (current_pixel + char_width > scroll_position && start_char == -1) {
                start_char = i;
            }
            
            current_pixel += char_width;
            
            if (current_pixel > scroll_position + max_width) {
                end_char = i;
                break;
            }
        }
        
        if (start_char == -1) start_char = 0;
        if (end_char == -1) end_char = current_text.length();
        
        // Extract visible portion
        std::string visible = current_text.substr(start_char, end_char - start_char);
        
        // If we need more text to fill the width, add from the beginning (seamless loop)
        int visible_width = font_manager->getTextWidth(visible.c_str(), font_size);
        if (visible_width < max_width && scroll_position > 0) {
            // Add gap
            std::string gap(SCROLL_GAP_PIXELS / 6, ' ');  // Approximate space width
            visible += gap;
            
            // Add beginning of text to create seamless loop
            int remaining_width = max_width - font_manager->getTextWidth(visible.c_str(), font_size);
            for (size_t i = 0; i < current_text.length() && remaining_width > 0; i++) {
                visible += current_text[i];
                remaining_width -= font_manager->getTextWidth(current_text.substr(i, 1).c_str(), font_size);
            }
        }
        
        return visible;
    }
};

// Unified visualization class with optional MPD support
class Visualization {
protected:
    Display* left_display;
    Display* right_display;
    MPDClient* mpd_client;
    FontManager* font_manager;
    TextScroller title_scroller_left;
    TextScroller title_scroller_right;
    
    // Helper to draw title with optional smooth scrolling MPD info
    void drawTitleWithMPD(Display* display, const char* viz_name, int y_offset = 0, bool is_left = true) {
        // Always draw visualization name
        display->drawText(0, y_offset, viz_name, FontManager::SMALL);
        
        // Early return if no MPD support
        if (!mpd_client || !font_manager) {
            return;
        }
        
        // Calculate where the MPD text should start
        int viz_name_width = font_manager->getTextWidth(viz_name, FontManager::SMALL);
        int mpd_start_x = viz_name_width + 8; // 8 pixels spacing
        int available_width = 128 - mpd_start_x;
        
        // Get current MPD text and update scroller
        std::string mpd_text = mpd_client->getFormattedText();
        TextScroller& scroller = is_left ? title_scroller_left : title_scroller_right;
        scroller.setText(mpd_text);
        
        // For pixel-perfect scrolling, we need to render to a temporary area
        // and then copy only the visible portion
        if (!mpd_text.empty() && available_width > 20) {
            std::string scrolled = scroller.getScrollingText(available_width, font_manager, FontManager::SMALL);
            
            // Calculate actual pixel offset for smooth rendering
            int text_width = font_manager->getTextWidth(mpd_text.c_str(), FontManager::SMALL);
            if (text_width > available_width) {
                // Create a clipping region for smooth pixel-based scrolling
                // This ensures text appears to slide smoothly rather than jump
                renderClippedText(display, mpd_start_x, y_offset, scrolled.c_str(), 
                                 available_width, FontManager::SMALL);
            } else {
                // Text fits, just draw normally
                display->drawText(mpd_start_x, y_offset, scrolled.c_str(), FontManager::SMALL);
            }
        }
    }
    
    // Helper method for simple title drawing (backward compatibility)
    void drawTitle(Display* display, const char* viz_name, int y_offset = 0) {
        display->drawText(2, y_offset, viz_name, FontManager::SMALL);
    }
    
private:
    // Helper to render text with pixel-perfect clipping
    void renderClippedText(Display* display, int x, int y, const char* text, 
                          int clip_width, FontManager::FontSize font_size) {
        // Create temporary buffer for the text
        uint8_t temp_buffer[128 * 8] = {0};  // One row of display height
        
        // Render text to temporary buffer
        font_manager->renderText(text, temp_buffer, 128, 64, 0, y, font_size);
        
        // Copy only the visible portion to the display buffer
        int page_start = (y - 8) / 8;  // Assuming font height ~8 pixels
        int page_end = (y + 8) / 8;
        
        for (int page = page_start; page <= page_end && page < 8; page++) {
            for (int col = 0; col < clip_width && (x + col) < 128; col++) {
                // Copy pixel data from temp buffer to display buffer
                display->buffer[page * 128 + x + col] |= temp_buffer[page * 128 + col];
            }
        }
    }
    
public:
    // Constructor for basic visualization (no MPD support)
    Visualization(Display* left, Display* right) 
        : left_display(left), right_display(right), mpd_client(nullptr), font_manager(nullptr) {}
    
    // Constructor with MPD support
    Visualization(Display* left, Display* right, MPDClient* mpd, FontManager* fm) 
        : left_display(left), right_display(right), mpd_client(mpd), font_manager(fm) {}
    
    virtual ~Visualization() = default;
    virtual void render(ControlState& state, AudioProcessor& audio) = 0;
    virtual const char* getName() const = 0;
    
    // Utility method to check if MPD support is available
    bool hasMPDSupport() const { return mpd_client != nullptr && font_manager != nullptr; }
};

// VU Meter visualization
class VUMeterVisualization : public Visualization {
private:
    struct DBPosition {
        int x;
        const char* text;
    };
    
    std::array<DBPosition, 11> db_positions;
    static constexpr const char* POWER_SCALE[6] = {"0", "20", "40", "60", "80", "100"};
    
    void calculateDBPositions() {
        // dB values matching Python code
        const float db_values[11] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
        
        for (int i = 0; i < 11; i++) {
            // Convert dB to linear value
            float value = powf(10.0f, db_values[i] / 20.0f);
            float log_pos = log10f(value);
            
            // Min/max log values for -20dB to +3dB
            float min_log = log10f(powf(10.0f, -20.0f / 20.0f));
            float max_log = log10f(powf(10.0f, 3.0f / 20.0f));
            
            // Calculate x position (0-125 range as in Python)
            int x_pos = (int)((log_pos - min_log) / (max_log - min_log) * 125.0f);
            
            // Store position and text
            db_positions[i].x = x_pos;
            // Static storage for text
            static char texts[11][4];
            snprintf(texts[i], 4, "%d", abs((int)db_values[i]));
            db_positions[i].text = texts[i];
        }
    }
    
    void drawVUBackground(Display* display, bool is_left) {
        // Draw dB markings
        for (const auto& pos : db_positions) {
            display->drawText(pos.x, 5, pos.text, FontManager::SMALL);
            display->drawLine(pos.x, 7, pos.x, 9);
        }
        
        // Draw horizontal lines
        display->drawLine(108, 8, 127, 8);  // Short line at right
        display->drawLine(0, 9, 127, 9);    // Full line
        display->drawLine(0, 11, 127, 11);    // Full line
        
        // Draw edge marks
        display->drawLine(0, 6, 0, 8);
        display->drawLine(0, 11, 0, 13);
        display->drawLine(127, 6, 127, 8);
        display->drawLine(127, 11, 127, 13);
        
        // Draw power scale
        for (int i = 0; i < 6; i++) {
            int x = i * 22;
            display->drawText(x, 22, POWER_SCALE[i], FontManager::SMALL);
            display->drawLine(x, 11, x, 13);
        }
        
        // Draw +/- indicators
        display->drawText(0, 28, "-", FontManager::SMALL);
        display->drawText(124, 28, "+", FontManager::SMALL);
        
        // Draw channel label
        display->drawText(0, 64, is_left ? "LEFT" : "RIGHT", FontManager::SMALL);
        display->drawText(120, 64, "dB", FontManager::SMALL);
    }
    
    void drawVUNeedle(Display* display, float level) {
        // Convert level (0-255) to position (0-127)
        int pos = (int)((level / 255.0f) * 127.0f);
        pos = std::max(0, std::min(127, pos));
        
        // Calculate needle start and end points (matching Python algorithm)
        int start_x = 71 - (127 - pos) / 8;
        int start_y = 63;
        int end_x = pos;
        
        // Parabolic curve for needle
        int curve_height = pos * (127 - pos);
        int end_y = 20 - curve_height / 200;
        
        // Draw needle with thickness
        display->drawLine(start_x, start_y, end_x, end_y);
        display->drawLine(start_x + 1, start_y, end_x + 1, end_y);
    }
    
    void drawVUMeter(Display* display, int level, bool is_left) {
        display->clear();
        
        // Draw background elements
        drawVUBackground(display, is_left);
        
        // Draw needle
        drawVUNeedle(display, (float)level);
        
        display->display();
    }
    
public:
    VUMeterVisualization(Display* left, Display* right) 
        : Visualization(left, right) {
        calculateDBPositions();
    }
    
    void render(ControlState& state, AudioProcessor& audio) override {
        int left_vu, right_vu;
        audio.getVUMeterData(left_vu, right_vu);
        
        drawVUMeter(left_display, left_vu, true);
        drawVUMeter(right_display, right_vu, false);
    }
    
    const char* getName() const override { return "VU Meter"; }
};

class SpectrumVisualizationMPD : public Visualization {
private:
    std::array<float, 7> peak_left{};
    std::array<float, 7> peak_right{};
    static constexpr const char* FREQ_LABELS[7] = {
        "63", "160", "400", "1K", "2.5K", "6.3K", "16K"
    };
    
    void drawSpectrum(Display* display, const std::array<int, 7>& levels, 
                      std::array<float, 7>& peaks, const char* title, bool is_left) {
        display->clear();
        
        // Draw title with MPD info on same line
        drawTitleWithMPD(display, title, 5, is_left);
        
        // Now we have more vertical space for bars!
        int bar_top = 8; // Only need small offset now
        int bar_bottom = 57;
        int bar_width = 12;
        int bar_height_range = bar_bottom - bar_top;
        
        // Draw bars
        for (int i = 0; i < 7; i++) {
            int x = 1 + (i * 19);
            int height = (levels[i] * bar_height_range) / 255;
            int bar_y = bar_bottom - height;
            
            // Draw bar
            if (height > 0 && bar_y >= bar_top) {
                display->drawRect(x, std::max(bar_y, bar_top), bar_width, 
                                 std::min(height, bar_bottom - bar_top), true);
            }
            
            // Update and draw peak
            if (bar_y < peaks[i]) {
                peaks[i] = bar_y;
            }
            peaks[i] = std::min((float)(bar_bottom - 1), peaks[i] + 0.8f);
            
            if (peaks[i] < bar_bottom - 1 && peaks[i] >= bar_top) {
                display->drawLine(x, (int)peaks[i], x + bar_width - 1, (int)peaks[i]);
            }
            
            // Label
            display->drawText(x, 64, FREQ_LABELS[i], FontManager::SMALL);
        }
        
        display->display();
    }
    
public:
    SpectrumVisualizationMPD(Display* left, Display* right, MPDClient* mpd, FontManager* fm) 
        : Visualization(left, right, mpd, fm) {}
    
    void render(ControlState& state, AudioProcessor& audio) override {
        std::array<int, 7> left_spectrum, right_spectrum;
        audio.getSpectrumData(left_spectrum, right_spectrum);
        
        drawSpectrum(left_display, left_spectrum, peak_left, "SPECTRUM L", true);
        drawSpectrum(right_display, right_spectrum, peak_right, "SPECTRUM R", false);
    }
    
    const char* getName() const override { return "Spectrum Analyzer"; }
};

class EmptySpectrumVisualizationMPD : public Visualization {
private:
    static constexpr const char* FREQ_LABELS[7] = {
        "63", "160", "400", "1K", "2.5K", "6.3K", "16K"
    };
    
    void drawSpectrum(Display* display, const std::array<int, 7>& levels, const char* title, bool is_left) {
        display->clear();
        
        // Draw title with MPD info on same line
        drawTitleWithMPD(display, title, 5, is_left);
        
        int bar_top = 8;
        int bar_bottom = 57;
        int bar_width = 12;
        int bar_height_range = bar_bottom - bar_top;
        
        // Draw bars
        for (int i = 0; i < 7; i++) {
            int x = 1 + (i * 19);
            int height = (levels[i] * bar_height_range) / 255;
            int bar_y = bar_bottom - height;
            
            // Draw bar
            if (height > 0 && bar_y >= bar_top) {
                display->drawRect(x, std::max(bar_y, bar_top), bar_width, 
                                 std::min(height, bar_bottom - bar_top), false);
            }
                       
            // Label
            display->drawText(x, 64, FREQ_LABELS[i], FontManager::SMALL);
        }
        
        display->display();
    }
    
public:
    EmptySpectrumVisualizationMPD(Display* left, Display* right, MPDClient* mpd, FontManager* fm) 
        : Visualization(left, right, mpd, fm) {}
    
    void render(ControlState& state, AudioProcessor& audio) override {
        std::array<int, 7> left_spectrum, right_spectrum;
        audio.getSpectrumData(left_spectrum, right_spectrum);
        
        drawSpectrum(left_display, left_spectrum, "SPECTRUM L", true);
        drawSpectrum(right_display, right_spectrum, "SPECTRUM R", false);
    }
    
    const char* getName() const override { return "Empty Spectrum Analyzer"; }
};

class TeubSpectrumVisualizationMPD : public Visualization {
private:
    std::array<float, 7> peak_left{};
    std::array<float, 7> peak_right{};
    static constexpr const char* FREQ_LABELS[7] = {
        "63", "160", "400", "1K", "2.5K", "6.3K", "16K"
    };
    
    void drawSpectrum(Display* display, const std::array<int, 7>& levels, 
                      std::array<float, 7>& peaks, const char* title, bool is_left) {
        display->clear();
        
        // Draw title with MPD info on same line
        drawTitleWithMPD(display, title, 5, is_left);
        
        // Now we have more vertical space for bars!
        int bar_top = 12; // Only need small offset now
        int bar_bottom = 47;
        int bar_width = 8;
        int bar_height_range = bar_bottom - bar_top;
        
        // Draw bars
        for (int i = 0; i < 7; i++) {
            int x = 1 + (i * 19);
            int height = (levels[i] * bar_height_range) / 255;
            int bar_y = bar_bottom - height;
            
            // Draw bar
            if (height > 0 && bar_y >= bar_top) {
            //    display->drawRect(x, std::max(bar_y, bar_top), bar_width, 
            //                     std::min(height, bar_height_range), false);
                display->drawLine(x, std::max(bar_y, bar_top), x, bar_bottom);
                display->drawLine(x+bar_width, std::max(bar_y, bar_top), x+bar_width, bar_bottom);
                display->drawCircle(x+4, std::max(bar_y, bar_top), 5, false);
                display->drawLine(x+4, std::max(bar_y, bar_top) -3, x+4, std::max(bar_y, bar_top)-1);


            }
            
            // Update and draw peak
            if (bar_y < peaks[i]) {
                peaks[i] = bar_y;
            }
            peaks[i] = std::min((float)(bar_bottom - 1), peaks[i] + 0.8f);
            
            if (peaks[i] < bar_bottom - 1 && peaks[i] >= bar_top) {
                display->drawLine(x+4, ((int)peaks[i])-4, x+4 , ((int)peaks[i])-2);
                display->drawLine(x+3, ((int)peaks[i])-4, x+3 , ((int)peaks[i])-4);
                display->drawLine(x+5, ((int)peaks[i])-4, x+5 , ((int)peaks[i])-6);


            }
            
            // balls
            display->drawCircle(x, 52, 5, false);
            display->drawCircle(x+8, 52, 5, false);


            // Label
            display->drawText(x, 64, FREQ_LABELS[i], FontManager::SMALL);
        }
        
        display->display();
    }
    
public:
    TeubSpectrumVisualizationMPD(Display* left, Display* right, MPDClient* mpd, FontManager* fm) 
        : Visualization(left, right, mpd, fm) {}
    
    void render(ControlState& state, AudioProcessor& audio) override {
        std::array<int, 7> left_spectrum, right_spectrum;
        audio.getSpectrumData(left_spectrum, right_spectrum);
        
        drawSpectrum(left_display, left_spectrum, peak_left, "SPECTEUB L", true);
        drawSpectrum(right_display, right_spectrum, peak_right, "SPECTEUB R", false);
    }
    
    const char* getName() const override { return "Spectrum Analyzer"; }
};

// Waveform visualization with MPD support
class WaveformVisualizationMPD : public Visualization {
private:
    static constexpr int WAVE_SAMPLES = 128;
    
    void drawWaveform(Display* display, AudioProcessor& audio, bool is_left) {
        display->clear();
        
        // Draw title with MPD info on same line
        drawTitleWithMPD(display, is_left ? "WAVEFORM L" : "WAVEFORM R", 5, is_left);
        
        // Get waveform data
        float* samples = new float[WAVE_SAMPLES];
        audio.getWaveformData(samples, WAVE_SAMPLES, is_left);
        
        // Now we can use almost the full height!
        int center_y = 37;  // Back to original center
        int wave_height = 25; // Full height
        
        // Draw center line
        display->drawLine(0, center_y, 127, center_y);
        
        // Draw waveform
        for (int i = 0; i < WAVE_SAMPLES - 1; i++) {
            int y1 = center_y - (int)(samples[i] * wave_height);
            int y2 = center_y - (int)(samples[i + 1] * wave_height);
            y1 = std::max(12, std::min(63, y1));
            y2 = std::max(12, std::min(63, y2));
            display->drawLine(i, y1, i + 1, y2);
        }
        
        delete[] samples;
        display->display();
    }
    
public:
    WaveformVisualizationMPD(Display* left, Display* right, MPDClient* mpd, FontManager* fm) 
        : Visualization(left, right, mpd, fm) {}
    
    void render(ControlState& state, AudioProcessor& audio) override {
        drawWaveform(left_display, audio, true);
        drawWaveform(right_display, audio, false);
    }
    
    const char* getName() const override { return "Waveform"; }
};

// Stereo field visualization with MPD support
class StereoFieldVisualizationMPD : public Visualization {
private:
    static constexpr int HISTORY_SIZE = 64;
    std::array<float, HISTORY_SIZE> phase_history{};
    std::array<float, HISTORY_SIZE> correlation_history{};
    int history_pos = 0;
    
    void drawStereoField(Display* display, AudioProcessor& audio, const char* title, bool is_left) {
        display->clear();
        
        // Draw title with MPD info on same line
        drawTitleWithMPD(display, title, 5, is_left);
        
        // Get stereo analysis
        float phase, correlation;
        audio.getStereoAnalysis(phase, correlation);
        
        // Update history
        phase_history[history_pos] = phase;
        correlation_history[history_pos] = correlation;
        history_pos = (history_pos + 1) % HISTORY_SIZE;
        
        // Full size phase meter!
        int center_x = 32;
        int center_y = 35;  // Original position
        int box_size = 23;  // Full size
        
        display->drawRect(center_x - box_size, center_y - box_size, box_size * 2, box_size * 2, false);
        
        // Draw crosshairs
        //display->drawLine(center_x - box_size, center_y, center_x + box_size, center_y);
        //display->drawLine(center_x, center_y - box_size, center_x, center_y + box_size);
        
        // Draw phase history as dots
        for (int i = 0; i < HISTORY_SIZE; i++) {
            float angle = phase_history[i] * M_PI;
            float radius = (box_size - 2) * (0.5f + correlation_history[i] * 0.5f);
            int x = center_x + (int)(radius * cosf(angle));
            int y = center_y + (int)(radius * sinf(angle));
            display->drawPixel(x, y);
        }
        
        // Draw correlation meter on the right
        int meter_x = 80;
        display->drawRect(meter_x, 12, 20, 45, false);
        int level = (int)(correlation * 22) + 22;
        if (level > 0) {
            display->drawRect(meter_x + 2, 57 - level, 16, level, true);
        }
        
        display->drawText(meter_x +22, 31, "CORR:", FontManager::SMALL);
        
        // Show correlation value
        char corr_text[8];
        snprintf(corr_text, sizeof(corr_text), "%+.2f", correlation);
        display->drawText(meter_x + 22, 38, corr_text, FontManager::SMALL);
        
        display->display();
    }
    
public:
    StereoFieldVisualizationMPD(Display* left, Display* right, MPDClient* mpd, FontManager* fm) 
        : Visualization(left, right, mpd, fm) {}
    
    void render(ControlState& state, AudioProcessor& audio) override {
        drawStereoField(left_display, audio, "STEREO", true);
        drawStereoField(right_display, audio, "PHASE", false);
    }
    
    const char* getName() const override { return "Stereo Field"; }
};

// Control handler with sleep mode
class ControlHandler {
private:
    ControlState& state;
    AudioProcessor& audio;
    uint8_t encoder1_state = 0;
    uint8_t encoder2_state = 0;
    static ControlHandler* instance;
    
    uint8_t readEncoder(int encoder) {
        uint8_t clk = bcm2835_gpio_lev(encoder == 1 ? GPIO::ROT1_CLK : GPIO::ROT2_CLK);
        uint8_t dt = bcm2835_gpio_lev(encoder == 1 ? GPIO::ROT1_DT : GPIO::ROT2_DT);
        return (clk << 1) | dt;
    }
    
    int getDirection(uint8_t old_state, uint8_t new_state) {
        static const int transitions[16] = {
            0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0
        };
        return transitions[(old_state << 2) | new_state];
    }
    
public:
    ControlHandler(ControlState& st, AudioProcessor& ap) : state(st), audio(ap) {
        instance = this;
        
        // Setup GPIO
        bcm2835_gpio_fsel(GPIO::POWER_LED, BCM2835_GPIO_FSEL_OUTP);
        bcm2835_gpio_write(GPIO::POWER_LED, HIGH);
        
        uint8_t inputs[] = {
            GPIO::ROT1_CLK, GPIO::ROT1_DT, GPIO::ROT1_SW,
            GPIO::ROT2_CLK, GPIO::ROT2_DT, GPIO::ROT2_SW,
            GPIO::POWER_SW
        };
        
        for (uint8_t pin : inputs) {
            bcm2835_gpio_fsel(pin, BCM2835_GPIO_FSEL_INPT);
            bcm2835_gpio_set_pud(pin, BCM2835_GPIO_PUD_UP);
        }
        
        encoder1_state = readEncoder(1);
        encoder2_state = readEncoder(2);
    }
    
    void poll() {
        // Encoder 1 - Sensitivity
        uint8_t new1 = readEncoder(1);
        if (new1 != encoder1_state) {
            int dir = getDirection(encoder1_state, new1);
            if (dir != 0) {
                int val = audio.getSensitivity() + dir * 10;
                audio.setSensitivity(std::max(10, std::min(300, val)));
            }
            encoder1_state = new1;
        }
        
        // Encoder 2 - Noise reduction
        uint8_t new2 = readEncoder(2);
        if (new2 != encoder2_state) {
            int dir = getDirection(encoder2_state, new2);
            if (dir != 0) {
                int val = audio.getNoiseReduction() + dir * 5;
                audio.setNoiseReduction(std::max(0, std::min(100, val)));
            }
            encoder2_state = new2;
        }
        
        // Buttons - Fixed button reading
        static bool btn1_last = true, btn2_last = true, pwr_last = true;
        
        bool btn1 = bcm2835_gpio_lev(GPIO::ROT1_SW);
        if (!btn1 && btn1_last) {
            state.current_viz = (state.current_viz + 1) % 6;
            if (state.is_sleeping) {
                state.is_sleeping = false;
            }
        }
        btn1_last = btn1;
        
        bool btn2 = bcm2835_gpio_lev(GPIO::ROT2_SW);
        if (!btn2 && btn2_last) {
            audio.setSensitivity(100);
            audio.setNoiseReduction(77);
            if (state.is_sleeping) {
                state.is_sleeping = false;
            }
        }
        btn2_last = btn2;
        
        bool pwr = bcm2835_gpio_lev(GPIO::POWER_SW);
        if (!pwr && pwr_last) {
            state.running = false;
        }
        pwr_last = pwr;
    }
    
    void setPowerLED(bool on) {
        bcm2835_gpio_write(GPIO::POWER_LED, on ? HIGH : LOW);
    }
};

ControlHandler* ControlHandler::instance = nullptr;

// Main application with sleep mode and MPD support
class VisualizerApp {
private:
    Display* left_display;
    Display* right_display;
    AudioProcessor audio;
    ControlState state;
    ControlHandler* controls;
    Visualization* visualizations[6];
    FontManager font_manager;
    MPDClient* mpd_client;
    
    void setSPISpeedSlow() {
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_256);  // Slower for sleep
    }
    
    void setSPISpeedNormal() {
        bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_64);   // Normal speed
    }
    
public:
    VisualizerApp() : left_display(nullptr), right_display(nullptr), 
                      controls(nullptr), mpd_client(nullptr) {
        
        // Initialize BCM2835
        if (!bcm2835_init()) {
            printf("Failed to init BCM2835\n");
            exit(1);
        }
        
        if (!bcm2835_spi_begin()) {
            printf("Failed to init SPI\n");
            bcm2835_close();
            exit(1);
        }
        
        bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);
        bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);
        bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_64);
        
        // Initialize displays
        left_display = new Display(GPIO::LEFT_CS, GPIO::LEFT_DC, GPIO::LEFT_RST);
        right_display = new Display(GPIO::RIGHT_CS, GPIO::RIGHT_DC, GPIO::RIGHT_RST);
        
        if (!left_display->begin() || !right_display->begin()) {
            printf("Failed to init displays\n");
            exit(1);
        }
        
        // Initialize font manager
        const char* font_paths[] = {
            "./trixel-square.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            nullptr
        };
        
        bool font_loaded = false;
        for (int i = 0; font_paths[i] && !font_loaded; i++) {
            if (font_manager.init(font_paths[i])) {
                font_loaded = true;
                left_display->setFont(&font_manager);
                right_display->setFont(&font_manager);
                printf("Using TTF font: %s\n", font_paths[i]);
            }
        }
        
        if (!font_loaded) {
            printf("WARNING: No TTF font found. Text will not be displayed.\n");
            printf("Please install fonts or place a .ttf file in current directory.\n");
        }
        
        // Initialize MPD client
        printf("Initializing MPD client...\n");
        mpd_client = new MPDClient("localhost", 6600);
        mpd_client->start();
        
        // Create visualizations - all with MPD support except VU Meter
        visualizations[0] = new VUMeterVisualization(left_display, right_display);
        visualizations[1] = new SpectrumVisualizationMPD(left_display, right_display, 
                                                        mpd_client, &font_manager);
        visualizations[2] = new EmptySpectrumVisualizationMPD(left_display, right_display, 
                                                        mpd_client, &font_manager);
        visualizations[3] = new TeubSpectrumVisualizationMPD(left_display, right_display, 
                                                        mpd_client, &font_manager);
        visualizations[4] = new WaveformVisualizationMPD(left_display, right_display,
                                                        mpd_client, &font_manager);
        visualizations[5] = new StereoFieldVisualizationMPD(left_display, right_display,
                                                            mpd_client, &font_manager);
        
        // Initialize controls last
        controls = new ControlHandler(state, audio);
        
        printf("Initialization complete\n");
    }
    
    ~VisualizerApp() {
        printf("Shutting down...\n");
        
        // Clean up in reverse order
        if (controls) delete controls;
        if (mpd_client) {
            mpd_client->stop();
            delete mpd_client;
        }
        
        for (int i = 0; i < 5; i++) {
            if (visualizations[i]) delete visualizations[i];
        }
        
        if (left_display) delete left_display;
        if (right_display) delete right_display;
        
        bcm2835_spi_end();
        bcm2835_close();
    }
    
void run() {
    printf("Dual OLED Audio Visualizer with MPD Support\n");
    printf("===========================================\n");
    printf("Rotary 1: Sensitivity | Rotary 2: Smoothing\n");
    printf("Press Rotary 1 to switch visualization\n");
    printf("Press Rotary 2 to reset settings\n");
    printf("Power button to exit\n");
    printf("Sleep mode after 10 seconds of silence\n\n");
    
    if (!audio.start()) {
        printf("Failed to init audio. Make sure ALSA is configured properly.\n");
        printf("Try: sudo modprobe snd-aloop\n");
        return;
    }
    
    int current_viz = 0;
    
    while (state.running) {
        controls->poll();
        
        // Check for audio and handle sleep mode
        bool has_audio = audio.checkForAudio();
        
        if (!has_audio && !state.is_sleeping) {
            // Enter sleep mode
            printf("Entering sleep mode - no audio detected\n");
            state.is_sleeping = true;
            
            // Notify audio processor about sleep state
            audio.setSleepState(true);
            
            // Notify MPD client about sleep state
            if (mpd_client) {
                mpd_client->setSleepState(true);
            }
            
            // Just turn off displays and LED
            left_display->sleep();
            right_display->sleep();
            controls->setPowerLED(false);
            
            // Reduce SPI speed for lower power consumption
            setSPISpeedSlow();

            
        } else if (has_audio && state.is_sleeping) {
            // Wake up
            printf("Waking up - audio detected\n");
            state.is_sleeping = false;
            
            // Restore normal SPI speed
            setSPISpeedNormal();
            
            // Notify audio processor about wake state
            audio.setSleepState(false);
            
            // Notify MPD client about wake state
            if (mpd_client) {
                mpd_client->setSleepState(false);
            }
            
            left_display->wake();
            right_display->wake();
            controls->setPowerLED(true);
            
            // Clear and redraw
            left_display->clear();
            right_display->clear();
            left_display->display();
            right_display->display();
        }
        
        // Handle visualization switching
        if (state.current_viz != current_viz) {
            current_viz = state.current_viz;
            printf("Switched to: %s\n", visualizations[current_viz]->getName());
            
            // Clear displays on switch
            left_display->clear();
            right_display->clear();
            left_display->display();
            right_display->display();
            
            // Wake if sleeping
            if (state.is_sleeping) {
                state.is_sleeping = false;

                // Restore normal SPI speed
                setSPISpeedNormal();  

                // Notify audio processor about wake state
                audio.setSleepState(false); 

                // Notify MPD client about wake state
                if (mpd_client) {
                    mpd_client->setSleepState(false);
                }
                
                left_display->wake();
                right_display->wake();
                controls->setPowerLED(true);
            }
        }
        
        // Render visualization if not sleeping
        if (!state.is_sleeping) {
            visualizations[current_viz]->render(state, audio);
            bcm2835_delay(10);  // ~100 FPS max
        } else {
            // During sleep, just poll controls occasionally
            static int sleep_counter = 0;
            if (++sleep_counter >= 10) {  // Check controls every second
                controls->poll();
                sleep_counter = 0;
            }
            bcm2835_delay(100);  // Sleep for 100ms
        }
            }
            
            printf("\nShutting down...\n");
            audio.stop();
            controls->setPowerLED(false);
            left_display->clear();
            right_display->clear();
            left_display->display();
            right_display->display();
        }
};

// Signal handler
VisualizerApp* app = nullptr;

void signalHandler(int sig) {
    printf("\nReceived signal %d\n", sig);
    if (app) {
        delete app;
        app = nullptr;
    }
    exit(0);
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    try {
        app = new VisualizerApp();
        app->run();
        delete app;
        app = nullptr;
    } catch (const std::exception& e) {
        printf("Exception: %s\n", e.what());
        if (app) {
            delete app;
            app = nullptr;
        }
        return 1;
    }
    
    return 0;
}