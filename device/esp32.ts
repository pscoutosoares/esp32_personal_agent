import { SerialPort } from "serialport";

export function startDeviceProcess(SpeechObject: any) {
  let capturando = false;

  const port = new SerialPort({
    path: "COM4",
    baudRate: 921600,
  });

  port.on("open", () => {
    console.log("Porta COM4 aberta. Aguardando estabilizacao...");

    setTimeout(() => {
      console.log("Iniciando captura de audio...");
      port.write("S");
      capturando = true;
    }, 2000);
  });

  // O dado binario de 16 bits (Little Endian) esta disponivel na variavel 'data'
  // Local de implementacao para streaming de WebSockets ou buffers de transcricao
  port.on("data", (data: Buffer) => {
    if (capturando && data.length > 0) {
      SpeechObject.stream.write(data);
    }
  });

  process.on("SIGINT", () => {
    capturando = false;
    console.log("\nEncerrando captura...");
    port.write("P", () => {
      SpeechObject.stream.end();
      port.close();
      process.exit();
    });
  });
}
