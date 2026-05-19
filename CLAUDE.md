# Alexa — Assistente Virtual com ESP32

## Objetivo
Construir um assistente virtual estilo Alexa usando ESP32 + microfone INMP441.
Projeto de aprendizado dirigido — cada etapa deve ensinar um conceito antes de avançar.

## Princípios de colaboração
- Manter a arquitetura existente ao máximo — sem grandes refatorações
- Caminho mais simples possível em cada etapa
- Ensinar o "por quê" antes do "como"
- Corrigir escolhas erradas com explicação
- Documentar todas as decisões e pivôs neste arquivo

---

## Hardware

| Componente | Detalhe |
|---|---|
| MCU | ESP32 (modelo original, não S2/S3) |
| Microfone | INMP441 (I2S PDM) |
| I2S WS (Word Select) | GPIO 15 |
| I2S SCK (Clock) | GPIO 14 |
| I2S SD (Data) | GPIO 32 |
| LED indicador | GPIO 2 |
| Porta serial | COM4 |

---

## Arquitetura atual

```
┌─────────────────────────────────────────┐
│  ESP32 (firmware — esp-idf)             │
│                                         │
│  INMP441 → I2S (16-bit PCM, 16 kHz)    │
│         → [wake word / trigger]         │
│         → UART0 @ 921600 baud           │
│           Marcador: 0xDEADBEEF          │
│           + 5s de PCM (500 frames)      │
└──────────────┬──────────────────────────┘
               │ USB-Serial (COM4)
               ▼
┌─────────────────────────────────────────┐
│  Node.js / TypeScript (PC)              │
│                                         │
│  device/esp32.ts  → lê serial           │
│  main.ts          → orquestra           │
│  Services/SpeechRecognition/google/     │
│    → Google Cloud STT                   │
│    → LINEAR16, 16 kHz, pt-BR            │
│    → onComplete(text) callback          │
└─────────────────────────────────────────┘
```

### Protocolo serial
- ESP32 envia `0xDEADBEEF` quando a trigger é ativada
- Seguido por `500 × 160 samples × 2 bytes` de PCM bruto (≈ 5s)
- Heartbeat: 1 byte `0x00` a cada 500ms para manter o bridge USB-UART aquecido
- Node.js filtra o marcador e passa o PCM diretamente para o stream do Google STT

---

## Stack

| Camada | Tecnologia |
|---|---|
| Firmware | **Arduino IDE**, C++ (framework arduino-esp32) |
| I2S driver | `driver/i2s.h` (API legada do Arduino-ESP32) |
| Wake word | **Edge Impulse** — modelo treinado, exportado como Arduino library (EON Compiler) |
| Backend | Node.js, TypeScript, `ts-node` |
| STT | `@google-cloud/speech` (streaming) |
| Serial | `serialport` v13 |

> A pasta `device/esp/` (ESP-IDF) está obsoleta e pode ser ignorada. O firmware ativo é `esp32/esp32.ino`.

---

## Estado atual (2026-05-19)

### O que funciona ✅
- Pipeline completa validada: ESP32 captura áudio → UART → Node.js → Google STT transcreve
- Testado com firmware simples (Arduino/`.ino`) que faz streaming contínuo sem wake word
- Google STT retorna transcrições em pt-BR com qualidade aceitável

### O que está sendo resolvido 🔧
- **Wake word detection**: o firmware atual usa TFLite Micro com dois modelos (preprocessador + detector)
  mas apresenta problemas sérios de reconhecimento — muitos falsos negativos

### Estrutura de arquivos relevantes
```
esp32/esp32.ino                         ← firmware ativo (Arduino IDE)
device/esp32.ts                         ← leitor serial (Node.js)
main.ts                                 ← entry point Node.js
Services/SpeechRecognition/google/      ← wrapper Google STT
todo.md                                 ← notas abertas

device/esp/  ← pasta legada (ESP-IDF), ignorar
```

### esp32/esp32.ino — o que faz hoje
- I2S configurado: WS=15, SCK=14, SD=32, 16kHz, 32-bit → converte para 16-bit (`sample >> 14`)
- Serial a 921600 baud
- Aguarda comando `'S'` para iniciar streaming, `'P'` para parar
- Lê 1 sample por iteração do loop e envia via `Serial.write()`
- **Não tem wake word ainda** — o Node.js controla quando transmitir

---

## Decisões e pivôs

### ESP-SR (WakeNet) — ❌ Descartado
- **Por quê tentou:** solução nativa da Espressif para wake word
- **Por quê descartou:** pesado demais para ESP32 original — estoura RAM/flash

### Home Assistant wake word (TFLite Micro, ESP-IDF) — ❌ Abandonado
- **O que é:** dois modelos TFLite em cadeia (preprocessador MFCC + detector CNN)
- **Por quê tentou:** referência conhecida, código disponível
- **Problema:** reconhecimento ruim (muitos falsos negativos), código longo e com muitas dependências de arquivos externos
- **Estado:** está em `device/esp/` (pasta legada), não usar

### Picovoice Porcupine — ❌ Descartado
- **Por quê era candidato:** leve, roda no ESP32 original, tier gratuito antes
- **Por quê descartou:** agora requer conta empresarial paga — inviável

---

## Próximos passos

### Decisão tomada (2026-05-19): Edge Impulse + Arduino IDE

- Wake word via **Edge Impulse** — modelo treinado com voz própria, EON Compiler, exportado como Arduino library
- Firmware em **Arduino IDE** (`esp32/esp32.ino`) — já captura INMP441 e transmite PCM via serial
- Node.js backend (device/esp32.ts, main.ts, Google STT) **não muda**
- Biblioteca Edge Impulse importada no Arduino IDE via `.zip`

**Próximo passo:** integrar `run_classifier()` do Edge Impulse no `esp32.ino` para detectar wake word antes de iniciar o streaming serial.

---

## Pendências abertas
- Investigar custo real das chamadas intermediárias ao Google STT (`todo.md`)
- Decidir e implementar o mecanismo de trigger (wake word / push-to-talk)
- Definir o que acontece após a transcrição (resposta, integração com API)
