#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_afe_sr_models.h"
#include "esp_ns.h"

#define I2S_WS 15
#define I2S_SCK 14
#define I2S_SD 32

i2s_chan_handle_t rx_handle;
esp_afe_sr_iface_t *afe_handle = NULL;
esp_afe_sr_data_t *afe_data = NULL;

void inicializar_i2s() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, NULL, &rx_handle);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_SCK,
            .ws = I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };
    i2s_channel_init_std_mode(rx_handle, &std_cfg);
    i2s_channel_enable(rx_handle);
}

void tarefa_alimentacao_audio(void *arg) {
    size_t bytes_lidos;
    int32_t buffer_bruto[512];
    int16_t buffer_convertido[512];
    int chunk_size = afe_handle->get_feed_chunksize(afe_data);

    while (1) {
        i2s_channel_read(rx_handle, buffer_bruto, chunk_size * sizeof(int32_t), &bytes_lidos, portMAX_DELAY);
        
        if (bytes_lidos > 0) {
            for (int i = 0; i < chunk_size; i++) {
                buffer_convertido[i] = (int16_t)(buffer_bruto[i] >> 14);
            }
            afe_handle->feed(afe_data, buffer_convertido);
        }
    }
}

void tarefa_deteccao_wakenet(void *arg) {
    while (1) {
        afe_fetch_result_t* resultado = afe_handle->fetch(afe_data);
        
        if (resultado && resultado->wakeup_state == WAKENET_DETECTED) {
            printf("\nWake Word Detectada localmente pelo ESP32!\n");
        }
    }
}

void app_main() {
    printf("Iniciando configuracao I2S...\n");
    inicializar_i2s();

    printf("Alocando modelo ESP-SR WakeNet...\n");
    afe_config_t afe_config = AFE_CONFIG_DEFAULT();
    afe_config.wakenet_init = true;
    afe_config.wakenet_model_name = "wn9_hiesp";
    afe_config.voice_communication_init = false;
    
    afe_handle = &ESP_AFE_SR_HANDLE;
    afe_data = afe_handle->create_from_config(&afe_config);

    if (afe_data == NULL) {
        printf("Falha critica: O hardware nao possui memoria RAM suficiente para carregar o modelo.\n");
        return;
    }

    xTaskCreatePinnedToCore(tarefa_alimentacao_audio, "alimentar_audio", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(tarefa_deteccao_wakenet, "detectar_wakenet", 8192, NULL, 4, NULL, 1);
}