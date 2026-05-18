/*
  Wake Word Detection com Edge Impulse + INMP441
  -----------------------------------------------
  Dependências:
    1. Biblioteca exportada do Edge Impulse (instalar via .ZIP)
       Sketch → Incluir Biblioteca → Adicionar biblioteca .ZIP
    2. Placa: ESP32 Dev Module

  Pinagem INMP441:
    VDD → 3.3V
    GND → GND
    SD  → GPIO 32  (dados)
    SCK → GPIO 14  (clock)
    WS  → GPIO 15  (word select)
    L/R → GND      (canal esquerdo)
*/

// Ajuste o nome abaixo para o nome do seu projeto no Edge Impulse
// Ex: projeto "alexa-wakeword" → <alexa-wakeword_inferencing.h>
#include <alexa-wakeword_inferencing.h>
#include <driver/i2s.h>

// ─── Pinos INMP441 ───────────────────────────────────────────────────────────

#define I2S_WS   15
#define I2S_SCK  14
#define I2S_SD   32
#define I2S_PORT I2S_NUM_0

// ─── Configuração de detecção ────────────────────────────────────────────────

// Score mínimo para considerar que a wake word foi detectada (0.0 a 1.0)
#define LIMIAR_DETECCAO 0.8f

// ─── Buffer de áudio ─────────────────────────────────────────────────────────

// O Edge Impulse define EI_CLASSIFIER_RAW_SAMPLE_COUNT (nº de samples por janela)
static float audio_buffer[EI_CLASSIFIER_RAW_SAMPLE_COUNT];
static int   buffer_pos = 0;

// ─── I2S ────────────────────────────────────────────────────────────────────

static void inicializar_i2s() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = EI_CLASSIFIER_FREQUENCY,  // 16000 Hz
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = 512,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };

    i2s_pin_config_t pinos = {
        .bck_io_num   = I2S_SCK,
        .ws_io_num    = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = I2S_SD
    };

    i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    i2s_set_pin(I2S_PORT, &pinos);
}

// ─── Callback de áudio exigido pelo Edge Impulse ─────────────────────────────

static int ei_microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = audio_buffer[offset + i];
    }
    return 0;
}

// ─── Setup ──────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("Inicializando...");
    inicializar_i2s();
    Serial.printf("Aguardando wake word '%s'...\n", EI_CLASSIFIER_LABEL_COUNT > 0 ? "alexa" : "?");
}

// ─── Loop ───────────────────────────────────────────────────────────────────

void loop() {
    // Lê samples do microfone e preenche o buffer
    int32_t raw;
    size_t bytes_lidos;

    while (buffer_pos < EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
        i2s_read(I2S_PORT, &raw, sizeof(raw), &bytes_lidos, portMAX_DELAY);
        if (bytes_lidos > 0) {
            // INMP441: converte 32-bit → float normalizado [-1.0, 1.0]
            audio_buffer[buffer_pos++] = (float)(raw >> 14) / 32768.0f;
        }
    }
    buffer_pos = 0;

    // Prepara sinal para o classificador
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data     = &ei_microphone_audio_signal_get_data;

    // Roda inferência
    ei_impulse_result_t resultado = {0};
    EI_IMPULSE_ERROR erro = run_classifier(&signal, &resultado, false);

    if (erro != EI_IMPULSE_OK) {
        Serial.printf("Erro na inferência: %d\n", erro);
        return;
    }

    // Verifica cada label retornado
    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (strcmp(resultado.classification[i].label, "alexa") == 0) {
            if (resultado.classification[i].value >= LIMIAR_DETECCAO) {
                Serial.printf(">>> Wake word detectada! (score: %.2f) <<<\n",
                              resultado.classification[i].value);
                // Adicione aqui o que fazer após detectar a wake word
            }
        }
    }
}
