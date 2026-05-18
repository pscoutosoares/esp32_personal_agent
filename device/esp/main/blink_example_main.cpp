#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"

extern "C" {
#include "model_data.h"
}
#include "audio_preprocessor_int8_model_data.h"

static const char *TAG = "WAKEWORD";

// ─── Pinos ───────────────────────────────────────────────────────────────────
#define I2S_WS   15
#define I2S_SCK  14
#define I2S_SD   32
#define LED_PIN  GPIO_NUM_2

// ─── Protocolo serial ─────────────────────────────────────────────────────────
// Marcador enviado ao Node.js quando wake word é detectada.
// Seguido por PCM 16-bit LE a 16kHz (STREAM_FRAMES × 160 samples × 2 bytes).
static const uint8_t WAKE_MARKER[] = {0xDE, 0xAD, 0xBE, 0xEF};
static constexpr int  STREAM_FRAMES = 500;   // 500 × 10ms = 5 segundos de áudio

// ─── Áudio ───────────────────────────────────────────────────────────────────
static constexpr int kWinSamples    = 480;
static constexpr int kStrideSamples = 160;

// ─── Arenas ──────────────────────────────────────────────────────────────────
static uint8_t pp_arena[20 * 1024]  __attribute__((aligned(16)));
static uint8_t ww_arena[112 * 1024] __attribute__((aligned(16)));

// ─── Interpreters ────────────────────────────────────────────────────────────
static tflite::MicroInterpreter *pp_interp = nullptr;
static tflite::MicroInterpreter *ww_interp = nullptr;
static TfLiteTensor *pp_in  = nullptr;
static TfLiteTensor *pp_out = nullptr;
static TfLiteTensor *ww_in  = nullptr;
static TfLiteTensor *ww_out = nullptr;

static int16_t audio_window[kWinSamples];
static i2s_chan_handle_t rx_handle;

// ─── I2S ─────────────────────────────────────────────────────────────────────
static void setup_i2s() {
    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&ch, NULL, &rx_handle);
    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk=I2S_GPIO_UNUSED, .bclk=(gpio_num_t)I2S_SCK,
                      .ws=(gpio_num_t)I2S_WS, .dout=I2S_GPIO_UNUSED,
                      .din=(gpio_num_t)I2S_SD, .invert_flags={false,false,false} }
    };
    i2s_channel_init_std_mode(rx_handle, &std);
    i2s_channel_enable(rx_handle);
}

// ─── UART para streaming de áudio ─────────────────────────────────────────────
// Chamado APÓS toda a inicialização. Reconfigura UART0 para 921600 baud e
// desativa logs para não corromper o stream binário.
static void setup_uart_for_streaming() {
    uart_driver_delete(UART_NUM_0);   // remove driver do console se instalado

    uart_config_t cfg = {
        .baud_rate  = 921600,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_0, 256, 8192, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &cfg);

    esp_log_level_set("*", ESP_LOG_NONE);  // silencia logs para não poluir o stream
}

// ─── Preprocessador ──────────────────────────────────────────────────────────
static bool setup_preprocessor() {
    const tflite::Model *m = tflite::GetModel(g_audio_preprocessor_int8_tflite);
    if (m->version() != TFLITE_SCHEMA_VERSION) return false;

    static tflite::MicroMutableOpResolver<18> res;
    res.AddReshape(); res.AddCast(); res.AddStridedSlice(); res.AddConcatenation();
    res.AddMul(); res.AddAdd(); res.AddDiv(); res.AddMinimum(); res.AddMaximum();
    res.AddWindow(); res.AddFftAutoScale(); res.AddRfft(); res.AddEnergy();
    res.AddFilterBank(); res.AddFilterBankSquareRoot();
    res.AddFilterBankSpectralSubtraction(); res.AddPCAN(); res.AddFilterBankLog();

    static tflite::MicroInterpreter interp(m, res, pp_arena, sizeof(pp_arena));
    pp_interp = &interp;
    if (pp_interp->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "Preprocessor AllocateTensors falhou"); return false;
    }
    pp_in  = pp_interp->input(0);
    pp_out = pp_interp->output(0);
    ESP_LOGI(TAG, "Preprocessor OK (arena=%u)", pp_interp->arena_used_bytes());
    return true;
}

// ─── Wake Word ────────────────────────────────────────────────────────────────
static bool setup_wakeword() {
    const tflite::Model *m = tflite::GetModel(g_model);
    if (m->version() != TFLITE_SCHEMA_VERSION) return false;

    static tflite::MicroMutableOpResolver<28> res;
    res.AddDepthwiseConv2D(); res.AddConv2D(); res.AddTransposeConv();
    res.AddFullyConnected(); res.AddAveragePool2D(); res.AddMaxPool2D(); res.AddMean();
    res.AddSoftmax(); res.AddLogistic(); res.AddRelu(); res.AddRelu6();
    res.AddReshape(); res.AddExpandDims(); res.AddTranspose();
    res.AddStridedSlice(); res.AddSlice(); res.AddConcatenation();
    res.AddAdd(); res.AddMul(); res.AddSub(); res.AddPad(); res.AddPadV2();
    res.AddQuantize(); res.AddDequantize();
    res.AddCallOnce(); res.AddVarHandle(); res.AddAssignVariable(); res.AddReadVariable();

    static tflite::MicroAllocator *alloc =
        tflite::MicroAllocator::Create(ww_arena, sizeof(ww_arena));
    static tflite::MicroResourceVariables *rv =
        tflite::MicroResourceVariables::Create(alloc, 20);
    static tflite::MicroInterpreter interp(m, res, alloc, rv);
    ww_interp = &interp;

    if (ww_interp->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "Wakeword AllocateTensors falhou"); return false;
    }
    ww_in  = ww_interp->input(0);
    ww_out = ww_interp->output(0);
    ESP_LOGI(TAG, "Wakeword OK (arena=%u)", ww_interp->arena_used_bytes());
    return true;
}

// ─── app_main ─────────────────────────────────────────────────────────────────
extern "C" void app_main() {
    ESP_LOGI(TAG, "Iniciando...");

    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);

    setup_i2s();
    if (!setup_preprocessor() || !setup_wakeword()) {
        ESP_LOGE(TAG, "Falha na inicialização"); return;
    }

    // Warmup: inicializa estados internos dos modelos com silêncio
    memset(audio_window, 0, sizeof(audio_window));
    memset(pp_in->data.i16, 0, kWinSamples * sizeof(int16_t));
    for (int i = 0; i < 50; i++) {
        pp_interp->Invoke();
        memcpy(ww_in->data.int8, pp_out->data.int8, 40);
        ww_interp->Invoke();
        vTaskDelay(1);
    }

    ESP_LOGI(TAG, "Pronto! Aguardando 'alexa'...");

    // Muda UART para 921600 baud e desativa logs — a partir daqui só PCM binário
    setup_uart_for_streaming();

    int32_t raw[kStrideSamples];
    int16_t pcm[kStrideSamples];

    static constexpr float RAW_THRESHOLD   = 0.06f;
    static constexpr int   DEBOUNCE_FRAMES = 1;

    int  detect_count      = 0;
    bool streaming         = false;
    int  stream_frames_left = 0;

    while (1) {
        // 1. Lê 10ms de áudio do INMP441
        size_t bytes_read;
        i2s_channel_read(rx_handle, raw, kStrideSamples * sizeof(int32_t),
                         &bytes_read, portMAX_DELAY);
        int n = (int)(bytes_read / sizeof(int32_t));

        for (int i = 0; i < n; i++)
            pcm[i] = (int16_t)(raw[i] >> 14);

        // 2. Atualiza janela deslizante de 30ms
        memmove(audio_window, audio_window + n, (kWinSamples - n) * sizeof(int16_t));
        memcpy(audio_window + kWinSamples - n, pcm, n * sizeof(int16_t));

        if (streaming) {
            // ── Modo STREAMING: transmite PCM bruto para o Node.js ──────────
            uart_write_bytes(UART_NUM_0, pcm, n * sizeof(int16_t));

            if (--stream_frames_left == 0) {
                streaming = false;
                gpio_set_level(LED_PIN, 0);
            }
        } else {
            // ── Modo LISTENING: roda inferência de wake word ─────────────────
            memcpy(pp_in->data.i16, audio_window, kWinSamples * sizeof(int16_t));
            if (pp_interp->Invoke() != kTfLiteOk) { vTaskDelay(1); continue; }

            memcpy(ww_in->data.int8, pp_out->data.int8, 40);
            if (ww_interp->Invoke() != kTfLiteOk) { vTaskDelay(1); continue; }

            float score = ww_out->data.uint8[0] * ww_out->params.scale;

            if (score >= RAW_THRESHOLD) {
                if (++detect_count >= DEBOUNCE_FRAMES) {
                    // Wake word detectada: acende LED, envia marcador, inicia streaming
                    gpio_set_level(LED_PIN, 1);
                    uart_write_bytes(UART_NUM_0, WAKE_MARKER, sizeof(WAKE_MARKER));
                    streaming         = true;
                    stream_frames_left = STREAM_FRAMES;
                    detect_count      = 0;
                }
            } else {
                detect_count = 0;
            }
        }

        vTaskDelay(1);
    }
}
