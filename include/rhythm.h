#include <Arduino.h>
#include <arduinoFFT.h>
#include "driver/i2s_std.h"    // 新版 I2S 标准驱动
#include "driver/i2s_pdm.h"
#include <FastLED.h>
#include "common.h"
#include <LEDMatrix.h>
#include "gravity.h"

#define I2S_WS_PIN    10
#define I2S_SD_PIN    6
#define I2S_BCK_PIN   7

static i2s_chan_handle_t rx_handle = NULL; // I2S 接收通道句柄
static int16_t* dma_buffer = NULL;         // DMA 缓冲区指针
bool volatile is_exit=false;
int mem_subindex=-1;

#define DMA_BUF_COUNT    6     // DMA 缓冲区数量
#define DMA_BUF_LEN      512  // DMA 缓冲区长度 (采样点数)

const int SAMPLE_FREQ = 16000;

int gain=12;  //adjust it to set the gain   
uint16_t audio_data;

double vReal2[DMA_BUF_LEN];
double vImag2[DMA_BUF_LEN];
double fft_bin[DMA_BUF_LEN];

double fft_data[MATRIX_WIDTH];
int fft_result[MATRIX_WIDTH]; 

//adjust single frequency curves.
double fft_freq_boost[MATRIX_WIDTH] = {
  0.4,0.5,0.5,0.5,0.6,
  0.8,1.1,1.1,1.5,1.7,
  3,3.4,3.6,3.6,3.8,
  3.8,1.0,};

TaskHandle_t fft_task=NULL;

ArduinoFFT<double> FFT2 = ArduinoFFT<double>( vReal2, vImag2, DMA_BUF_LEN, SAMPLE_FREQ);

double fft_add( int from, int to) {
  int i = from;
  double result = 0;
  while ( i <= to) {
    result += fft_bin[i++];
  }
  return result;
}

void fft_code( void * parameter) {

  for(;;) {
    if (is_exit)
      break;

    delay(10);

    int32_t digitalSample = 0;
    size_t bytes_read;
    esp_err_t ret = i2s_channel_read(rx_handle, dma_buffer, DMA_BUF_LEN * sizeof(int16_t), &bytes_read, portMAX_DELAY);

    for (int i=0;i<DMA_BUF_LEN;i++){
      vReal2[i] = dma_buffer[i]; 
      vImag2[i] = 0;
    }

    FFT2.windowing( FFT_WIN_TYP_HAMMING, FFT_FORWARD ); 
    FFT2.compute( FFT_FORWARD ); 
    FFT2.complexToMagnitude(); 

    for (int i = 0; i < DMA_BUF_LEN; i++) {   
      double t = 0.0;
      t = abs(vReal2[i]);
      // t = t / 16.0;    
      fft_bin[i] = t;
    }

    fft_data[0]  = (fft_add(6,7))/2;
    fft_data[1]  = (fft_add(8,10))/3;
    fft_data[2]  = (fft_add(11,15))/5;
    fft_data[3]  = (fft_add(16,20))/5;
    fft_data[4]  = (fft_add(21,25))/5;
    fft_data[5]  = (fft_add(26,31))/6;
    fft_data[6]  = (fft_add(32,37))/6;
    fft_data[7]  = (fft_add(38,43))/6;
    fft_data[8]  = (fft_add(44,49))/6;
    fft_data[9]  = (fft_add(50,55))/6;
    fft_data[10] = (fft_add(56,61))/6;
    fft_data[11] = (fft_add(62,67))/6;
    fft_data[12] = (fft_add(68,73))/6;
    fft_data[13] = (fft_add(74,79))/6;
    fft_data[14] = (fft_add(80,85))/6;
    fft_data[15] = (fft_add(86,91))/6;
    // fft_data[16] = (fft_add(92,97))/6;

    double fft_m;
    fft_m=MAX(fft_add(92,97),fft_add(98,103));
    fft_m=MAX(fft_m,fft_add(104,109));
    fft_m=MAX(fft_m,fft_add(110,115));
    fft_m=MAX(fft_m,fft_add(116,121));
    fft_m=MAX(fft_m,fft_add(122,127));
    fft_m=MAX(fft_m,fft_add(128,133));
    fft_data[16]=fft_m/6;

    //adjust single frequency curves.
    for (int i=0; i < MATRIX_WIDTH; i++) {
      fft_data[i] = fft_data[i] * fft_freq_boost[i];
    }

    //adjust overall frequency curves.
    for (int i=0; i < MATRIX_WIDTH; i++) {
      fft_data[i] = fft_data[i] * gain / 50;
    }

    //constraint function
    for (int i=0; i < MATRIX_WIDTH; i++) {
      fft_result[i] = constrain((int)fft_data[i],0,255);
    }
  }
  vTaskDelete(NULL);
  fft_task=NULL;
}

void audio_receive_i2s() {
  is_exit=false;
  if (dma_buffer==NULL)
    dma_buffer = (int16_t*)malloc(DMA_BUF_LEN * sizeof(int16_t));
  
    Serial.println("begin audio setup");

    // 1. 配置 I2S 通道基本参数
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // 可选：手动指定 I2S 控制器编号，以提高确定性 (例如 I2S_NUM_0)
    chan_cfg.id = I2S_NUM_0;
    chan_cfg.dma_desc_num = DMA_BUF_COUNT; // 设置 DMA 描述符数量
    chan_cfg.dma_frame_num = DMA_BUF_LEN;  // 设置 DMA 缓冲区大小

    // 2. 分配 RX 通道
    if (i2s_new_channel(&chan_cfg, NULL, &rx_handle) != ESP_OK) {
        Serial.println("Error: Failed to allocate I2S RX channel");
        return ;
    }

    // 3. 配置 I2S 标准模式参数
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_FREQ),    // 时钟配置
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO), // 时隙配置 (16位，单声道)
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,       // 不使用主时钟
            .bclk = (gpio_num_t)I2S_BCK_PIN,
            .ws   = (gpio_num_t)I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,       // 不使用数据输出
            .din  = (gpio_num_t)I2S_SD_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    // 关键优化：将 I2S 的时钟源设置为 APLL
    // APLL 是一个高精度、低抖动的音频专用锁相环，与 CPU 时钟独立，
    // 这样当 CPU 动态调整频率以省电时，I2S 的采样时钟不会受到影响，保证了音频采集的稳定性。
    std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;

    // 4. 初始化 I2S 通道
    if (i2s_channel_init_std_mode(rx_handle, &std_cfg) != ESP_OK) {
        Serial.println("Error: Failed to initialize I2S channel");
        return ;
    }

    // Serial.println("I2S initialized successfully with APLL clock source.");

    // 5. 启用 I2S 通道
  if (i2s_channel_enable(rx_handle) != ESP_OK) {
      Serial.println("Error: Failed to enable I2S channel");
      return ;
  }
  xTaskCreatePinnedToCore(
        fft_code,                    //Task function
        "FFT",                       //Task name
        10000,                       //Task stack size(units:byte)
        NULL,                        //Parameters passed to the task function
        1,                           //Task priority
        &fft_task,                   //Task handle
        0);                          //Specifies the core to run the task
}

void audio_receive_pdm() {
  is_exit=false;
  if (dma_buffer==NULL)
    dma_buffer = (int16_t*)malloc(DMA_BUF_LEN * sizeof(int16_t));
  
    Serial.println("begin audio setup");

    // 1️⃣ 创建通道
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.id = I2S_NUM_0;
    chan_cfg.dma_desc_num = DMA_BUF_COUNT; // 设置 DMA 描述符数量
    chan_cfg.dma_frame_num = DMA_BUF_LEN;  // 设置 DMA 缓冲区大小
    i2s_new_channel(&chan_cfg, NULL, &rx_handle);

    // 2️⃣ PDM配置
    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_FREQ),
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_PDM_SLOT_LEFT,
        },
        .gpio_cfg = {
            .clk = (gpio_num_t)I2S_BCK_PIN,
            .din = (gpio_num_t)I2S_WS_PIN,
        },
    };

    // 4. 初始化 I2S 通道
    if (i2s_channel_init_pdm_rx_mode(rx_handle, &pdm_cfg) != ESP_OK) {
        Serial.println("Error: Failed to initialize I2S channel");
        return ;
    }

    // 5. 启用 I2S 通道
  if (i2s_channel_enable(rx_handle) != ESP_OK) {
      Serial.println("Error: Failed to enable I2S channel");
      return ;
  }
  xTaskCreatePinnedToCore(
        fft_code,                    //Task function
        "FFT",                       //Task name
        10000,                       //Task stack size(units:byte)
        NULL,                        //Parameters passed to the task function
        1,                           //Task priority
        &fft_task,                   //Task handle
        0);                          //Specifies the core to run the task


}


uint8_t num_bands = MATRIX_WIDTH;
uint8_t num_vals = MATRIX_HEIGHT;
uint8_t color_timer = 0;
uint8_t direction = 0;
uint8_t mx,my;

uint8_t bar_height[]  = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
uint8_t peak_height[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
uint8_t prev_fft_value[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};


// Color Palette
DEFINE_GRADIENT_PALETTE(green_to_red) {
  0,   173,   255,    47,    //green
127,   255,   218,     0,    //yellow
255,   231,     0,     0 };  //red
DEFINE_GRADIENT_PALETTE(purple_to_blue) {
  0,   141,     0,   100,    //purple
127,   255,   192,     0,    //yellow
255,     0,     5,   255 };  //blue
DEFINE_GRADIENT_PALETTE(red_to_mistyrose) {
  0,   255,   228,   225,    //MistyRose
 64,   255,    69,     0,    //OrangeRed
127,   255,     0,     0,    //red
128,   255,     0,     0,    //red
192,   255,    69,     0,    //OrangeRed
255,   255,   228,   225 };  //MistyRose
CRGBPalette16 green_red_palette = green_to_red;
CRGBPalette16 purple_blue_palette = purple_to_blue;
CRGBPalette16 red_mistyrose_palette = red_to_mistyrose;
// end Color Palette

void get_cord(uint8_t x,uint8_t y){
  if (direction==0){
    mx=x;
    my=y;
  }
  else if (direction==2){
    mx=MATRIX_WIDTH-x-1;
    my=MATRIX_HEIGHT-y-1;
  }
  else if (direction==1){
    mx=MATRIX_WIDTH-y-1;
    my=x;
  }
  else if (direction==3){
    mx=y;
    my=MATRIX_HEIGHT-x-1;
  }
}

// bars patterns
void green_red_bars(int band, int bar) {
  for (int y = 0; y < bar; y++) {
    get_cord(band,y);
    leds(mx,my) = ColorFromPalette(green_red_palette, y * (255 / bar),180);
  }
  
}
void rainbow_bars(uint8_t band, uint8_t bar) {
  for (int y = 0; y < bar; y++) {
    get_cord(band,y);
    leds(mx,my) = CHSV( (band*(255/num_bands)), 255, 180);
  }
}
void half_rainbow_bars(uint8_t band, uint8_t bar) {
  if ((band%2)==0){
    for (int y = 0; y < bar; y++) {
      get_cord(band,y);
      leds(mx,my) = CHSV( (band*(255/num_bands)), 255, 180);
    }
  }
  else {
    for (int y = num_vals-1; y > num_vals-bar-1; y--) {
      get_cord(band,y);
      leds(mx,my) = CHSV( (band*(255/num_bands)), 255, 180);
    }
  }
}
void center_bars(int band, int bar) {
  if (bar % 2 == 0) bar--;
  int y_start = ((MATRIX_HEIGHT - bar) / 2 );
  for (int y = y_start; y <= (y_start + bar); y++) {
    int color_index = constrain((y - y_start) * (255 / bar), 0, 255);
    leds(band,y) = ColorFromPalette(red_mistyrose_palette, color_index, 180);
  }
}
void changing_bars(int band, int bar) {
  for (int y = 0; y < bar; y++) {
    get_cord(band,y);
    leds(mx,my) = CHSV(y * (255 / MATRIX_HEIGHT) + color_timer, 255, 180); 
  }
}// end bars patterns

// peaks patterns
void yellow_white_peak(int band) {
  get_cord(band,peak_height[band]);
  leds(mx,my) = 0xB4B4B4;//CRGB::White;
  if (peak_height[band] > 0) {
    get_cord(band,0);
    leds(mx,my) = 0x7AB421; //CRGB::GreenYellow;
  }
    
}
void white_peak(int band) {
  get_cord(band,peak_height[band]);
  leds(mx,my) = 0xB4B4B4;
}
void half_white_peak(int band) {
  if ((band%2)==0){
    get_cord(band,peak_height[band]);
    leds(mx,my) = 0xB4B4B4;
  }
  else {
    get_cord(band,num_vals-peak_height[band]-1);
    leds(mx,my) = 0xB4B4B4;
  }
}
void changing_peak(int band) {
  get_cord(band,peak_height[band]);
  leds(mx,my) = ColorFromPalette(purple_blue_palette, peak_height[band] * (255 / MATRIX_HEIGHT),180);
}

int draw_rtythm(){
  FastLED.clear();

  gravity_xy_t g = gravity_get();
  float gx = g.valid ? g.gx : 0.0f;
  float gy = g.valid ? g.gy : 0.0f;
  float gz = g.valid ? g.gz : 0.0f;

  if (gy>0.7){
    num_bands = MATRIX_HEIGHT;
    num_vals = MATRIX_WIDTH;
    direction=1;
  }
  else if (gy<-0.7){
    num_bands = MATRIX_HEIGHT;
    num_vals = MATRIX_WIDTH;
    direction=3;
  }
  if (gx>0.7){
    num_bands = MATRIX_WIDTH;
    num_vals = MATRIX_HEIGHT;
    direction=2;
  }
  else if (gx<-0.7){
    num_bands = MATRIX_WIDTH;
    num_vals = MATRIX_HEIGHT;
    direction=0;
  }
  
  if (direction % 2 ==1){ //竖着放
    for (int i = 0; i < MATRIX_HEIGHT; i++) {
      uint8_t fft_value;
      fft_value = (fft_result[i*2] + fft_result[i*2+1])/2;
      fft_value = ((prev_fft_value[i] * 3) + fft_value) / 4;   
      bar_height[i] = fft_value / (255 / (MATRIX_WIDTH-1));       // scale bar height
      if (bar_height[i] > peak_height[i])                          // peak up
        peak_height[i] = min((MATRIX_WIDTH-1), (int)bar_height[i]);
      prev_fft_value[i] = fft_value;        
    }
  }
  else {
    for (int i = 0; i < MATRIX_WIDTH; i++) {
      uint8_t fft_value;
      fft_value = fft_result[i];
      fft_value = ((prev_fft_value[i] * 3) + fft_value) / 4;   
      bar_height[i] = fft_value / (255 / (MATRIX_HEIGHT-1));       // scale bar height
      if (bar_height[i] > peak_height[i])                          // peak up
        peak_height[i] = min((MATRIX_HEIGHT-1), (int)bar_height[i]);
      prev_fft_value[i] = fft_value;        
    }
  }

  for (int band = 0; band < num_bands; band++) {
    switch (subpage_index % 4) {
      case 0:
        green_red_bars(band, bar_height[band]);
        yellow_white_peak(band);
        break;
      case 1:
        rainbow_bars(band, bar_height[band]);
        white_peak(band);
        break;
      case 2:
        half_rainbow_bars(band, bar_height[band]);
        half_white_peak(band);
        break;
      case 3:
        changing_bars(band, bar_height[band]);
        changing_peak(band);
        EVERY_N_MILLISECONDS(80) { color_timer++; }
        break;
      // case 4:
      //   center_bars(band, bar_height[band]);
      //   break;
    }
  }

  EVERY_N_MILLISECONDS(100) {
    for (uint8_t band = 0; band < num_bands; band++)
      if (peak_height[band] > 0) peak_height[band] -= 1;
  }

  FastLED.show();
  return 0;
}

int setup_rhythm(){
  int err = gravity_sensor_start();
  if (err != 0) {
    Serial.println("mpu start failed");
  }
  
  // audio_receive_i2s();
  audio_receive_pdm();
  FastLED.setBrightness(10);  //0~255
  brightness_max=10;

  if (mem_subindex<0)
    mem_subindex=load_config("sim_index");
  subpage_index=mem_subindex;

  return 0;
}

int unload_rhythm(){
  mem_subindex=subpage_index;
  save_config("rhythm_index",subpage_index);

  gravity_sensor_sleep();

  // if (fft_task!=NULL){
  //   vTaskDelete(fft_task);
  //   fft_task=NULL;
  // }
  is_exit=true;
  delay(50);

  if (rx_handle!=NULL){
    // 1. 停止 I2S 通道的硬件操作
    esp_err_t ret = i2s_channel_disable(rx_handle);
    if (ret != ESP_OK) {
      return -1;
    }

    // 2. 删除 I2S 通道并释放其占用的所有软件和硬件资源
    ret = i2s_del_channel(rx_handle);
    if (ret != ESP_OK) {
      return -2;
    } else {
        rx_handle = NULL; // 删除后将句柄置空，防止误用
    }
  }

  if (dma_buffer){
    free(dma_buffer);
    dma_buffer=NULL;
  }
  
  return 0;
}