import { SerialPort } from "serialport";
import * as fs from "fs";

const PORTA_SERIAL = "COM4";
const BAUD_RATE = 921600;
const TEMPO_GRAVACAO_MS = 5000;
const TAXA_AMOSTRAGEM = 16000;
const CANAIS = 1;
const BITS_POR_AMOSTRA = 16;

const port = new SerialPort({
  path: PORTA_SERIAL,
  baudRate: BAUD_RATE,
});

let capturando = false;
const chunks: Buffer[] = [];

function criarCabecalhoWav(tamanhoDados: number): Buffer {
  const buffer = Buffer.alloc(44);
  buffer.write("RIFF", 0);
  buffer.writeUInt32LE(36 + tamanhoDados, 4);
  buffer.write("WAVE", 8);
  buffer.write("fmt ", 12);
  buffer.writeUInt32LE(16, 16);
  buffer.writeUInt16LE(1, 20);
  buffer.writeUInt16LE(CANAIS, 22);
  buffer.writeUInt32LE(TAXA_AMOSTRAGEM, 24);
  buffer.writeUInt32LE(TAXA_AMOSTRAGEM * CANAIS * (BITS_POR_AMOSTRA / 8), 28);
  buffer.writeUInt16LE(CANAIS * (BITS_POR_AMOSTRA / 8), 32);
  buffer.writeUInt16LE(BITS_POR_AMOSTRA, 34);
  buffer.write("data", 36);
  buffer.writeUInt32LE(tamanhoDados, 40);
  return buffer;
}

port.on("open", () => {
  console.log(
    `Porta ${PORTA_SERIAL} aberta. Aguardando estabilizacao de rotinas internas...`,
  );

  setTimeout(() => {
    console.log(`Iniciando captura de ${TEMPO_GRAVACAO_MS / 1000} segundos...`);
    capturando = true;
    port.write("S");

    setTimeout(() => {
      port.write("P", () => {
        capturando = false;
        console.log("Gravacao concluida. Gerando arquivo fisico...");

        const dadosAudio = Buffer.concat(chunks);
        const cabecalho = criarCabecalhoWav(dadosAudio.length);
        const arquivoWav = Buffer.concat([cabecalho, dadosAudio]);

        fs.writeFileSync("gravacao.wav", arquivoWav);
        console.log("Arquivo salvo com sucesso: gravacao.wav");

        port.close();
        process.exit(0);
      });
    }, TEMPO_GRAVACAO_MS);
  }, 2000);
});

port.on("data", (data: Buffer) => {
  if (capturando) {
    chunks.push(data);
  }
});

process.on("SIGINT", () => {
  console.log("\nInterrupcao detectada. Encerrando porta serial...");
  port.write("P", () => {
    port.close();
    process.exit();
  });
});
